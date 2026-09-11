// SPDX-License-Identifier: MIT
/*
 * uc_oneway - minimal UC SEND benchmark/probe.
 *
 * This is for Apple RDMA-over-Thunderbolt interop debugging. TCP is used only
 * to exchange QP metadata; data flows as UC IBV_WR_SEND messages.
 *
 * Example:
 *   # Mac receiver, connecting outbound to avoid macOS inbound firewall prompts
 *   ./uc_oneway --role recv --dev rdma_en1 --gid-index 1 \
 *       --connect 192.168.1.11 --port 18515 \
 *       --size 4096 --count 2048 --depth 512
 *
 *   # Linux sender, listening for the TCP metadata connection
 *   ./uc_oneway --role send --dev usb4_rdma0 --gid-index 1 \
 *       --port 18515 --size 4096 --count 2048 --depth 64
 *
 * For credit-window probes, the receiver can post more RECV WRs than the
 * number of messages expected:
 *   ./uc_oneway --role recv ... --count 64 --depth 512 --recv-posts 512
 *
 * Same-QP bidirectional probe, matching the traffic shape that JACCL allreduce
 * uses on the Apple-compatible path:
 *   ./uc_oneway --role bidi --dev rdma_en1 --gid-index 1 --port 18515 \
 *       --size 16384 --count 64 --depth 2 --recv-posts 2 --check
 *   ./uc_oneway --role bidi --dev usb4_apple0 --gid-index 1 \
 *       --connect 192.168.1.20 --port 18515 \
 *       --size 16384 --count 64 --depth 2 --recv-posts 2 --check
 *
 * Receiver-issued credits (--credits, both roles must agree):
 *
 *   Apple's RDMA-over-Thunderbolt provider enforces credit-based flow control
 *   in hardware on a Mac sender; a Linux sender has none. The moment the Mac
 *   receiver has no posted receive, frames land in the void and the Mac's ring
 *   pair is poisoned until the service is rebound. TN3205 also requires sender
 *   and receiver to post the same number of frames per message.
 *
 *   --credits carries receive grants over the existing TCP control socket:
 *   the receiver grants one message only after the corresponding
 *   ibv_post_recv() SUCCEEDS, and the sender reserves one grant before every
 *   post_send_slot(). --depth remains an independent LOCAL queue limit.
 *
 *     ./uc_oneway --role recv ... --credits --check
 *     ./uc_oneway --role send ... --credits --check
 *
 *   A sender that does not receive the receiver's READY record refuses to run.
 *   Run the built-in accounting tests with:  ./uc_oneway --selftest
 */

#include <arpa/inet.h>
#ifdef __APPLE__
#include <dlfcn.h>
#endif
#include <errno.h>
#include <infiniband/verbs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

struct opts {
	const char *role;
	const char *dev;
	const char *connect_host;
	int port;
	int gid_index;
	int ib_port;
	int depth;
	int send_slots;
	int recv_posts;
	int recv_post_delay_ms;
	int recv_repost_hold_ms;
	int count;
	int mtu;
	int check;
	int check_any_order;
	int recv_wr_id_base;
	int send_wr_id_base;
	int hold_after_rts_ms;
	int hold_before_destroy_ms;
	int credits;
	int credit_xport;	/* CR_XPORT_TCP | CR_XPORT_UC */
	int ctl_depth;		/* control receives the sender pre-posts (UC) */
	int ctl_send_slots;	/* control SEND slots the receiver owns (UC) */
	int grant_deadline_us;	/* maximum publication delay for a minted grant */
	int grant_batch;	/* max grants coalesced before a forced publish */
	int grant_resend_ms;	/* starved with no grant progress -> ask over TCP */
	int wr_gap_us;		/* completion gap that earns a stall-trace line */
	int recv_keep_posted;	/* resolved policy: hold the window at --recv-posts to FINAL */
	int recv_keep_posted_set;	/* --recv-keep-posted given explicitly */
	int recv_no_keep_posted;	/* --recv-no-keep-posted: the control arm */
	int recv_guard;		/* k: receives kept ungranted, G <= min(N, P - k) */
	int verbose;
	int drain_timeout_ms;
	int handshake_timeout_ms;
	size_t size;
};

#define PEER_FLAG_CREDITS 0x1u
#define PEER_FLAG_CREDITS_UC 0x2u

struct peer_info {
	uint32_t magic;
	uint32_t qpn;
	uint32_t psn;
	uint32_t lid;
	uint8_t gid[16];
	uint32_t flags;
};

static uint64_t now_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void debug_step(const char *step)
{
	if (getenv("UC_ONEWAY_DEBUG")) {
		fprintf(stderr, "uc_oneway: %s\n", step);
		fflush(stderr);
	}
}

static void print_gid(FILE *f, const union ibv_gid *gid)
{
	for (int i = 0; i < 16; i += 2)
		fprintf(f, "%02x%02x%s", gid->raw[i], gid->raw[i + 1],
			i == 14 ? "" : ":");
}

static int gid_is_zero(const union ibv_gid *gid)
{
	static const union ibv_gid zero;

	return !memcmp(gid, &zero, sizeof(*gid));
}

static int gid_is_ipv4_mapped(const union ibv_gid *gid)
{
	static const uint8_t prefix[12] = {
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff,
	};

	return !memcmp(gid->raw, prefix, sizeof(prefix));
}

static int select_gid(struct ibv_context *ctx, int port, int requested_index,
		      int gid_tbl_len, union ibv_gid *gid, int *selected_index)
{
	if (requested_index >= 0) {
		if (requested_index >= gid_tbl_len) {
			fprintf(stderr, "gid index %d exceeds gid table length %d\n",
				requested_index, gid_tbl_len);
			return -1;
		}
		memset(gid, 0, sizeof(*gid));
		if (ibv_query_gid(ctx, port, requested_index, gid)) {
			perror("ibv_query_gid");
			return -1;
		}
		if (gid_is_zero(gid)) {
			fprintf(stderr, "gid index %d is empty\n",
				requested_index);
			return -1;
		}
		*selected_index = requested_index;
		return 0;
	}

	for (int i = 0; i < gid_tbl_len; i++) {
		memset(gid, 0, sizeof(*gid));
		if (ibv_query_gid(ctx, port, i, gid) || !gid_is_ipv4_mapped(gid))
			continue;
		*selected_index = i;
		return 0;
	}

	for (int i = 0; i < gid_tbl_len; i++) {
		memset(gid, 0, sizeof(*gid));
		if (ibv_query_gid(ctx, port, i, gid) || gid_is_zero(gid))
			continue;
		*selected_index = i;
		return 0;
	}

	fprintf(stderr, "no non-zero GID found on port %d\n", port);
	return -1;
}

static size_t align_up(size_t v, size_t a)
{
	return (v + a - 1) & ~(a - 1);
}

static void usage(const char *argv0)
{
	fprintf(stderr,
		"usage: %s --role recv|send|bidi --dev DEV [--gid-index N|auto] --port P\n"
		"          [--connect HOST] [--size BYTES] [--count N]\n"
		"          [--depth N] [--send-slots N] [--recv-posts N]\n"
		"          [--mtu 256|512|1024|2048|4096]\n"
		"          [--recv-post-delay-ms N] [--recv-repost-hold-ms N]\n"
		"          [--recv-wr-id-base N] [--send-wr-id-base N]\n"
		"          [--hold-after-rts-ms N] [--hold-before-destroy-ms N]\n"
		"          [--credits[=tcp|uc]] [--drain-timeout-ms N]\n"
		"          [--ctl-depth N] [--ctl-send-slots N]\n"
		"          [--grant-deadline-us N] [--grant-batch N]\n"
		"          [--grant-resend-ms N] [--wr-gap-us N]\n"
		"          [--recv-keep-posted|--recv-no-keep-posted] [--recv-guard K]\n"
		"             (credited receivers keep the window posted by default and\n"
		"              never grant more than posted - K, K=4)\n"
		"          [--handshake-timeout-ms N] [-v|--verbose]\n"
		"          [--check] [--check-any-order]\n"
		"       %s --selftest\n",
		argv0, argv0);
}

static int parse_int(const char *s, int *out)
{
	char *end = NULL;
	long v;

	errno = 0;
	v = strtol(s, &end, 0);
	if (errno || !end || *end || v < 0 || v > 0x7fffffff)
		return -1;
	*out = (int)v;
	return 0;
}

static int parse_gid_index(const char *s, int *out)
{
	if (!strcmp(s, "auto")) {
		*out = -1;
		return 0;
	}
	return parse_int(s, out);
}

static int parse_size(const char *s, size_t *out)
{
	char *end = NULL;
	unsigned long long v;

	errno = 0;
	v = strtoull(s, &end, 0);
	if (errno || !end || *end || v == 0)
		return -1;
	*out = (size_t)v;
	return 0;
}

/* ------------------------------------------------------------------------
 * Receiver-issued credit protocol (--credits)
 *
 * Fixed-size little-endian records on the existing TCP control socket. The
 * invariant the wire records enforce is the one Apple's hardware enforces for
 * a Mac sender and our Linux sender does not have:
 *
 *   messages_started(generation) <= successfully_posted_receive_grants(gen)
 *
 * Only a SUCCESSFUL ibv_post_recv() on the receiver creates a grant. Local
 * send completion returns local descriptor capacity only, never credit.
 * ------------------------------------------------------------------------ */

#define CR_MAGIC 0x31445243u /* "CRD1" little-endian */
#define CR_VERSION 1u
#define CR_WIRE_LEN 64u
#define CR_FRAME_BYTES 4096u

enum cr_type {
	CR_TYPE_READY = 1,
	CR_TYPE_GRANT = 2,
	CR_TYPE_ABORT = 3,
	CR_TYPE_FINAL = 4,
	/* Sender -> receiver: how many control receives are posted for the
	 * on-link grant channel. Cumulative, so a lost one is recovered by the
	 * next. Always on TCP; the control window must never depend on the
	 * control window. */
	CR_TYPE_CTLWIN = 5,
	/* Sender -> receiver: "I am starved and have seen no grant progress";
	 * asks for a re-publication of the latest cumulative snapshot. Never
	 * invents capacity. */
	CR_TYPE_RESEND = 6,
};

/* Which transport carries the high-frequency GRANT records. READY, CTLWIN,
 * RESEND, ABORT and FINAL are always TCP. */
enum cr_xport {
	CR_XPORT_TCP = 0,
	CR_XPORT_UC = 1,
};

struct cr_record {
	uint32_t magic;
	uint16_t version;
	uint16_t type;
	uint32_t generation;
	uint32_t msg_size;
	uint32_t recv_requested;  /* receives asked for (READY) */
	uint32_t recv_accepted;   /* receives the provider actually took (READY) */
	uint32_t frames_per_msg;  /* ceil(msg_size / 4096) -- Apple counts frames */
	uint64_t cum_grants;      /* cumulative messages granted, monotonic */
	uint64_t cum_frames;      /* cumulative 4 KiB frames granted, monotonic */
	uint64_t aux;             /* FINAL: completed count. ABORT: reason code. */
	uint32_t status;          /* FINAL: 0 = OK, non-zero = failure */
};

enum cr_result {
	CR_OK = 0,
	CR_ERR_MAGIC,
	CR_ERR_VERSION,
	CR_ERR_TYPE,
	CR_ERR_GENERATION,
	CR_ERR_MSG_SIZE,
	CR_ERR_DECREASING,
	CR_ERR_FRAMES,
	CR_ERR_SHORT,
	CR_DUPLICATE,   /* well-formed, but adds no capacity */
	CR_STALE,       /* well-formed but older than the ledger (UC reorder) */
	CR_ABORTED,
};

struct credit_state {
	uint32_t generation;
	uint32_t msg_size;
	uint32_t frames_per_msg;
	uint64_t granted;   /* cumulative grants accepted from the receiver */
	uint64_t reserved;  /* grants consumed by posted sends */
	uint64_t frames_granted;
	int ready;
	int aborted;
	int closed;   /* sender: admission closed before FINAL; no more reservations */
	/* On an unreliable transport a smaller cumulative total is a reordered
	 * record, not a protocol violation: ignore it, never roll back. On TCP
	 * it stays an error, because TCP cannot reorder. */
	int stale_ok;
};

static const char *cr_result_str(enum cr_result r)
{
	switch (r) {
	case CR_OK:              return "ok";
	case CR_ERR_MAGIC:       return "bad magic";
	case CR_ERR_VERSION:     return "bad version";
	case CR_ERR_TYPE:        return "bad record type";
	case CR_ERR_GENERATION:  return "wrong generation";
	case CR_ERR_MSG_SIZE:    return "message size mismatch";
	case CR_ERR_DECREASING:  return "decreasing cumulative grant";
	case CR_ERR_FRAMES:      return "frame accounting mismatch";
	case CR_ERR_SHORT:       return "short record";
	case CR_DUPLICATE:       return "duplicate (no new capacity)";
	case CR_STALE:           return "stale (reordered on an unreliable transport)";
	case CR_ABORTED:         return "peer abort";
	}
	return "unknown";
}

static const char *cr_type_str(uint16_t t)
{
	switch (t) {
	case CR_TYPE_READY:  return "READY";
	case CR_TYPE_GRANT:  return "GRANT";
	case CR_TYPE_ABORT:  return "ABORT";
	case CR_TYPE_FINAL:  return "FINAL";
	case CR_TYPE_CTLWIN: return "CTLWIN";
	case CR_TYPE_RESEND: return "RESEND";
	}
	return "?";
}

static const char *cr_xport_str(int x)
{
	return x == CR_XPORT_UC ? "uc" : "tcp";
}

static uint32_t cr_frames_for(uint32_t msg_size)
{
	if (!msg_size)
		return 1;
	return (msg_size + CR_FRAME_BYTES - 1u) / CR_FRAME_BYTES;
}

static void cr_put32(uint8_t *p, uint32_t v)
{
	for (int i = 0; i < 4; i++)
		p[i] = (uint8_t)((v >> (i * 8)) & 0xffu);
}

static void cr_put16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)(v & 0xffu);
	p[1] = (uint8_t)((v >> 8) & 0xffu);
}

static void cr_put64(uint8_t *p, uint64_t v)
{
	for (int i = 0; i < 8; i++)
		p[i] = (uint8_t)((v >> (i * 8)) & 0xffu);
}

static uint32_t cr_get32(const uint8_t *p)
{
	uint32_t v = 0;

	for (int i = 0; i < 4; i++)
		v |= (uint32_t)p[i] << (i * 8);
	return v;
}

static uint16_t cr_get16(const uint8_t *p)
{
	return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint64_t cr_get64(const uint8_t *p)
{
	uint64_t v = 0;

	for (int i = 0; i < 8; i++)
		v |= (uint64_t)p[i] << (i * 8);
	return v;
}

/* Serialise into a fixed 64-byte frame; explicit so struct padding and host
 * endianness cannot leak onto the wire between a Mac and a Linux peer. */
static void cr_encode(const struct cr_record *r, uint8_t buf[CR_WIRE_LEN])
{
	memset(buf, 0, CR_WIRE_LEN);
	cr_put32(buf + 0, r->magic);
	cr_put16(buf + 4, r->version);
	cr_put16(buf + 6, r->type);
	cr_put32(buf + 8, r->generation);
	cr_put32(buf + 12, r->msg_size);
	cr_put32(buf + 16, r->recv_requested);
	cr_put32(buf + 20, r->recv_accepted);
	cr_put32(buf + 24, r->frames_per_msg);
	cr_put64(buf + 28, r->cum_grants);
	cr_put64(buf + 36, r->cum_frames);
	cr_put64(buf + 44, r->aux);
	cr_put32(buf + 52, r->status);
}

static enum cr_result cr_decode(const uint8_t buf[CR_WIRE_LEN],
				struct cr_record *r)
{
	memset(r, 0, sizeof(*r));
	r->magic = cr_get32(buf + 0);
	r->version = cr_get16(buf + 4);
	r->type = cr_get16(buf + 6);
	r->generation = cr_get32(buf + 8);
	r->msg_size = cr_get32(buf + 12);
	r->recv_requested = cr_get32(buf + 16);
	r->recv_accepted = cr_get32(buf + 20);
	r->frames_per_msg = cr_get32(buf + 24);
	r->cum_grants = cr_get64(buf + 28);
	r->cum_frames = cr_get64(buf + 36);
	r->aux = cr_get64(buf + 44);
	r->status = cr_get32(buf + 52);

	if (r->magic != CR_MAGIC)
		return CR_ERR_MAGIC;
	if (r->version != CR_VERSION)
		return CR_ERR_VERSION;
	if (r->type != CR_TYPE_READY && r->type != CR_TYPE_GRANT &&
	    r->type != CR_TYPE_ABORT && r->type != CR_TYPE_FINAL &&
	    r->type != CR_TYPE_CTLWIN && r->type != CR_TYPE_RESEND)
		return CR_ERR_TYPE;
	return CR_OK;
}

static void cr_record_init(struct cr_record *r, uint16_t type,
			   const struct credit_state *cs)
{
	memset(r, 0, sizeof(*r));
	r->magic = CR_MAGIC;
	r->version = (uint16_t)CR_VERSION;
	r->type = type;
	r->generation = cs->generation;
	r->msg_size = cs->msg_size;
	r->frames_per_msg = cs->frames_per_msg;
	r->cum_grants = cs->granted;
	r->cum_frames = cs->frames_granted;
}

/* Sender side: accept the receiver's opening READY record. */
static enum cr_result credit_accept_ready(struct credit_state *cs,
					  const struct cr_record *r,
					  uint32_t expected_msg_size)
{
	if (r->type == CR_TYPE_ABORT)
		return CR_ABORTED;
	if (r->type != CR_TYPE_READY)
		return CR_ERR_TYPE;
	if (r->msg_size != expected_msg_size)
		return CR_ERR_MSG_SIZE;
	if (r->frames_per_msg != cr_frames_for(expected_msg_size))
		return CR_ERR_FRAMES;
	/*
	 * recv_accepted is the window the receiver really posted; cum_grants
	 * is what it is willing to let us send, capped at its --count. Grants
	 * beyond the posted window would be an overrun (refused); grants below
	 * it are the cap at work (--count 1 with --recv-posts 60 advertises
	 * 60 receives and 1 grant). Frames must still match the grants.
	 */
	if (r->cum_grants > (uint64_t)r->recv_accepted)
		return CR_ERR_FRAMES;
	if (r->cum_frames != r->cum_grants * (uint64_t)r->frames_per_msg)
		return CR_ERR_FRAMES;

	cs->generation = r->generation;
	cs->msg_size = r->msg_size;
	cs->frames_per_msg = r->frames_per_msg;
	cs->granted = r->cum_grants;
	cs->frames_granted = r->cum_frames;
	cs->reserved = 0;
	cs->ready = 1;
	cs->aborted = 0;
	return CR_OK;
}

/* Sender side: fold a cumulative GRANT into the ledger.
 *
 * Cumulative totals mean a duplicate or a reordered-but-stale update can never
 * add capacity: only a strictly greater cumulative total moves the ledger, and
 * a smaller one is a protocol error rather than a rollback. */
static enum cr_result credit_apply_grant(struct credit_state *cs,
					 const struct cr_record *r)
{
	if (!cs->ready)
		return CR_ERR_TYPE;
	if (r->type == CR_TYPE_ABORT) {
		if (r->generation != cs->generation)
			return CR_ERR_GENERATION;
		cs->aborted = 1;
		return CR_ABORTED;
	}
	if (r->type != CR_TYPE_GRANT)
		return CR_ERR_TYPE;
	if (r->generation != cs->generation)
		return CR_ERR_GENERATION;
	if (r->msg_size != cs->msg_size)
		return CR_ERR_MSG_SIZE;
	if (r->frames_per_msg != cs->frames_per_msg)
		return CR_ERR_FRAMES;
	if (r->cum_frames != r->cum_grants * (uint64_t)r->frames_per_msg)
		return CR_ERR_FRAMES;
	if (r->cum_grants < cs->granted)
		return cs->stale_ok ? CR_STALE : CR_ERR_DECREASING;
	if (r->cum_grants == cs->granted)
		return CR_DUPLICATE;

	cs->granted = r->cum_grants;
	cs->frames_granted = r->cum_frames;
	return CR_OK;
}

static uint64_t credit_available(const struct credit_state *cs)
{
	return cs->granted > cs->reserved ? cs->granted - cs->reserved : 0;
}

/* Reserve exactly one message worth of receive capacity. Returns 0 when the
 * sender must wait: the caller must NOT post in that case. */
static int credit_reserve(struct credit_state *cs)
{
	if (!cs->ready || cs->aborted || cs->closed || !credit_available(cs))
		return 0;
	cs->reserved++;
	return 1;
}

/* Sender: close admission. Nothing is reserved or posted after this; it is
 * called before the FINAL record so FINAL means "no more sends", not "no
 * more sends so far". */
static void credit_close(struct credit_state *cs)
{
	cs->closed = 1;
}

/* Return a reservation whose ibv_post_send() did not happen. */
static void credit_unreserve(struct credit_state *cs)
{
	if (cs->reserved)
		cs->reserved--;
}

/* Receiver side: a successful ibv_post_recv() is the ONLY thing that mints a
 * grant. A failed post (ENOMEM) mints nothing. */
static void credit_grant_one(struct credit_state *cs)
{
	cs->granted++;
	cs->frames_granted += cs->frames_per_msg;
}

/*
 * The receiver-window invariant (Astra v10, T4):
 *
 *     D <= S <= G <= min(N, max(0, P - k))
 *
 *   P  cumulative SUCCESSFUL data receive posts, extras included
 *   G  cumulative grants issued
 *   S  sender reservations (sender-side; the sender logs granted/reserved)
 *   D  receives consumed by arriving frames, CQ-polled or not
 *   N  --count;  k  --recv-guard
 *
 * so the receiver always retains k untouched receives even when CQ polling
 * lags, and the sender can never be told about capacity that would overrun
 * the run. The guard is withheld from READY onward (initial grants are
 * min(N, posted - k)), not only at the tail.
 *
 * The control arm (window allowed to decay) cannot satisfy this at its own
 * tail: its last k receives ARE the last k messages and P stops at N, so
 * G <= P - k would leave the run stuck at N - k forever. There the guard is
 * released once the last real receive has been posted (P >= N), which is
 * exactly the decay that arm exists to measure. With keep-posted, extras
 * raise P past N and release the last k grants, and the guard holds through
 * FINAL -- which is why an extra post that fails must fail the row.
 */
static uint64_t guard_ceiling(uint64_t posted, uint64_t guard, uint64_t count,
			      int keep_posted)
{
	uint64_t c;

	if (!keep_posted && posted >= count)
		return count;
	c = posted > guard ? posted - guard : 0;
	return c < count ? c : count;
}

/* Raise the ledger to `ceiling`, never above it, never back down. Returns the
 * number of grants minted (0 or, in steady state, 1). */
static uint64_t credit_grant_to_ceiling(struct credit_state *cs,
					uint64_t ceiling)
{
	uint64_t minted = 0;

	while (cs->granted < ceiling) {
		credit_grant_one(cs);
		minted++;
	}
	return minted;
}

/* ------------------------------------------------------------------------
 * Control-queue ledger (--credits=uc)
 *
 * The grant channel is itself UC, so it needs its own admission: the receiver
 * may never have more control SENDs outstanding than the sender has posted
 * control receives. Bootstrapped by a CTLWIN record over TCP and replenished
 * the same way as the sender's control receives complete and are reposted.
 * Cumulative on both sides, so a lost CTLWIN costs a pause, never an overrun.
 * ------------------------------------------------------------------------ */

struct ctl_credit {
	uint64_t granted;      /* cumulative control receives posted by the peer */
	uint64_t used;         /* cumulative control SENDs issued */
	uint64_t stalls;       /* publications blocked by an empty control window */
	uint64_t stall_ns;
	uint64_t tcp_escapes;  /* snapshots that took the TCP escape hatch */
	uint64_t stall_since;  /* 0 = not currently blocked */
};

static uint64_t ctl_credit_avail(const struct ctl_credit *c)
{
	return c->granted > c->used ? c->granted - c->used : 0;
}

static int ctl_credit_take(struct ctl_credit *c)
{
	if (!ctl_credit_avail(c))
		return 0;
	c->used++;
	return 1;
}

/* A CTLWIN record is cumulative: only a strictly larger total adds capacity. */
static int ctl_credit_apply(struct ctl_credit *c, uint64_t cum)
{
	if (cum <= c->granted)
		return 0;
	c->granted = cum;
	return 1;
}

/* ------------------------------------------------------------------------
 * Bounded, non-blocking control output (TCP)
 *
 * cr_flush_grants() used send_all(), which blocks: a writable check is not a
 * promise that a whole 64-byte record fits, and the CQ loop must never park
 * inside a socket write. This queue holds at most one record mid-write plus
 * one pending snapshot. Because grants are CUMULATIVE, a newer snapshot simply
 * replaces the pending one -- the queue is bounded by construction and never
 * drops capacity, only redundant intermediate totals.
 * ------------------------------------------------------------------------ */

#ifdef MSG_NOSIGNAL
#define CR_SEND_FLAGS (MSG_DONTWAIT | MSG_NOSIGNAL)
#else
#define CR_SEND_FLAGS (MSG_DONTWAIT)
#endif

struct cr_out {
	int fd;
	uint8_t part[CR_WIRE_LEN];
	size_t part_off;
	size_t part_len;          /* 0 = nothing mid-write */
	uint64_t part_since;      /* mint time of the oldest grant in `part` */
	uint64_t part_total;      /* cumulative grant total `part` carries */
	int pending;
	uint8_t pend[CR_WIRE_LEN];
	uint64_t pend_since;
	uint64_t pend_total;
	uint64_t coalesced;       /* snapshots superseded before reaching the wire */
	uint64_t partial_writes;  /* records the socket took only part of */
};

struct cr_out_done {
	int records;
	uint64_t since;   /* mint time carried by the last completed record */
	uint64_t total;   /* cumulative grant total that reached the socket */
};

static void cr_out_init(struct cr_out *o, int fd)
{
	memset(o, 0, sizeof(*o));
	o->fd = fd;
}

/* Push whatever is queued. Returns -1 on socket error, else 0. */
static int cr_out_progress(struct cr_out *o, struct cr_out_done *d)
{
	for (;;) {
		if (!o->part_len) {
			if (!o->pending)
				return 0;
			memcpy(o->part, o->pend, CR_WIRE_LEN);
			o->part_len = CR_WIRE_LEN;
			o->part_off = 0;
			o->part_since = o->pend_since;
			o->part_total = o->pend_total;
			o->pending = 0;
		}
		while (o->part_off < o->part_len) {
			ssize_t n = send(o->fd, o->part + o->part_off,
					 o->part_len - o->part_off,
					 CR_SEND_FLAGS);

			if (n < 0) {
				if (errno == EINTR)
					continue;
				if (errno == EAGAIN || errno == EWOULDBLOCK) {
					if (o->part_off)
						o->partial_writes++;
					return 0;
				}
				return -1;
			}
			o->part_off += (size_t)n;
		}
		o->part_len = 0;
		d->records++;
		d->since = o->part_since;
		d->total = o->part_total;
	}
}

/* Queue one cumulative snapshot. Never blocks, never grows: a newer snapshot
 * replaces the pending one rather than queueing behind it. */
static int cr_out_submit(struct cr_out *o, const struct cr_record *r,
			 uint64_t since, struct cr_out_done *d)
{
	if (o->part_len) {
		if (o->pending)
			o->coalesced++;
		else
			o->pend_since = since;
		cr_encode(r, o->pend);
		o->pend_total = r->cum_grants;
		o->pending = 1;
		return cr_out_progress(o, d);
	}
	cr_encode(r, o->part);
	o->part_len = CR_WIRE_LEN;
	o->part_off = 0;
	o->part_since = since;
	o->part_total = r->cum_grants;
	return cr_out_progress(o, d);
}

static int cr_out_idle(const struct cr_out *o)
{
	return !o->part_len && !o->pending;
}

/* ------------------------------------------------------------------------
 * Grant publication
 *
 * Publication is driven from the successful-repost path, not from the end of a
 * CQ batch, and carries its OWN deadline: a minted grant reaches the transport
 * within grant_deadline_us regardless of how many completions the receive loop
 * is chewing through. Delay is measured mint -> transport handoff.
 * ------------------------------------------------------------------------ */

struct grant_pub {
	int xport;
	uint64_t last_sent;      /* cumulative total handed to the transport */
	uint64_t pending_since;  /* mint time of the oldest unpublished grant */
	uint64_t deadline_ns;
	uint64_t batch_max_grants;
	/* statistics */
	uint64_t pubs;
	uint64_t delay_sum;
	uint64_t delay_max;
	uint64_t delay_min;
	uint64_t deadline_pubs;
	uint64_t blocked;        /* attempts the transport could not take */
	uint64_t resends;        /* peer-requested re-publications */
	uint64_t uc_pubs;
	uint64_t tcp_pubs;
};

static void grant_pub_init(struct grant_pub *g, int xport, uint64_t seeded,
			   int deadline_us, int batch)
{
	memset(g, 0, sizeof(*g));
	g->xport = xport;
	g->last_sent = seeded;
	g->deadline_ns = (uint64_t)deadline_us * 1000ull;
	g->batch_max_grants = batch > 0 ? (uint64_t)batch : 1;
	g->delay_min = UINT64_MAX;
}

/* Called from the successful-repost path, once per minted grant. */
static void grant_pub_note(struct grant_pub *g, uint64_t now)
{
	if (!g->pending_since)
		g->pending_since = now;
}

/*
 * `since` is the mint time of the oldest grant in the record that just reached
 * the transport. Sample the clock HERE rather than reusing the caller's
 * timestamp: the caller's `now` is often the same reading that was stamped on
 * the mint, which would report a publication delay of exactly zero and measure
 * nothing.
 */
static void grant_pub_done(struct grant_pub *g, uint64_t total, uint64_t since,
			   int by_deadline)
{
	uint64_t now = now_ns();
	uint64_t delay;

	g->last_sent = total;
	if (!since)
		return;
	delay = now > since ? now - since : 0;
	g->pubs++;
	g->delay_sum += delay;
	if (delay > g->delay_max)
		g->delay_max = delay;
	if (delay < g->delay_min)
		g->delay_min = delay;
	if (by_deadline)
		g->deadline_pubs++;
}

/* Buffered record channel: never blocks the CQ loop, never assumes a record
 * arrives in one read(). */
struct cr_chan {
	int fd;
	uint8_t buf[CR_WIRE_LEN];
	size_t have;
	int eof;
};

static void cr_chan_init(struct cr_chan *c, int fd)
{
	memset(c, 0, sizeof(*c));
	c->fd = fd;
}

/*
 * Returns 1 when a full record was decoded into *out, 0 when nothing is
 * available within timeout_ms, -1 on socket/protocol error (*res carries the
 * decode verdict when the failure was a malformed record).
 */
static int cr_chan_poll(struct cr_chan *c, int timeout_ms,
			struct cr_record *out, enum cr_result *res)
{
	struct pollfd pfd;

	*res = CR_OK;
	for (;;) {
		if (c->have == CR_WIRE_LEN) {
			enum cr_result r = cr_decode(c->buf, out);

			c->have = 0;
			if (r != CR_OK) {
				*res = r;
				return -1;
			}
			return 1;
		}
		if (c->eof)
			return 0;

		pfd.fd = c->fd;
		pfd.events = POLLIN;
		pfd.revents = 0;
		int pr = poll(&pfd, 1, timeout_ms);

		if (pr < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (!pr)
			return 0;

		ssize_t n = recv(c->fd, c->buf + c->have,
				 CR_WIRE_LEN - c->have, 0);

		if (n < 0) {
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				return 0;
			return -1;
		}
		if (!n) {
			c->eof = 1;
			if (c->have) {
				*res = CR_ERR_SHORT;
				return -1;
			}
			return 0;
		}
		c->have += (size_t)n;
		timeout_ms = 0; /* only wait once per call */
	}
}

static int load_verbs_provider(void)
{
#ifdef __APPLE__
	void *handle;

	handle = dlopen("librdma.dylib", RTLD_NOW | RTLD_GLOBAL);
	if (!handle) {
		fprintf(stderr, "failed to load librdma.dylib: %s\n", dlerror());
		return -1;
	}
#endif
	return 0;
}

static enum ibv_mtu mtu_enum(int mtu)
{
	switch (mtu) {
	case 256:
		return IBV_MTU_256;
	case 512:
		return IBV_MTU_512;
	case 1024:
		return IBV_MTU_1024;
	case 2048:
		return IBV_MTU_2048;
	case 4096:
	default:
		return IBV_MTU_4096;
	}
}

static int parse_opts(int argc, char **argv, struct opts *o)
{
	memset(o, 0, sizeof(*o));
	o->port = 18515;
	o->gid_index = -1;
	o->ib_port = 1;
	o->depth = 64;
	o->count = 1000;
	o->mtu = 1024;
	o->recv_wr_id_base = -1;
	o->send_wr_id_base = -1;
	o->drain_timeout_ms = 10000;
	o->handshake_timeout_ms = 60000;
	o->size = 4096;
	o->credit_xport = CR_XPORT_TCP;
	o->ctl_depth = 32;
	o->ctl_send_slots = 8;
	o->grant_deadline_us = 200;
	o->grant_batch = 1;
	o->grant_resend_ms = 50;
	o->wr_gap_us = 5000;
	o->recv_guard = 4;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--role") && i + 1 < argc)
			o->role = argv[++i];
		else if (!strcmp(argv[i], "--dev") && i + 1 < argc)
			o->dev = argv[++i];
		else if (!strcmp(argv[i], "--connect") && i + 1 < argc)
			o->connect_host = argv[++i];
		else if (!strcmp(argv[i], "--port") && i + 1 < argc) {
			if (parse_int(argv[++i], &o->port))
				return -1;
		} else if (!strcmp(argv[i], "--gid-index") && i + 1 < argc) {
			if (parse_gid_index(argv[++i], &o->gid_index))
				return -1;
		} else if (!strcmp(argv[i], "--ib-port") && i + 1 < argc) {
			if (parse_int(argv[++i], &o->ib_port))
				return -1;
		} else if (!strcmp(argv[i], "--depth") && i + 1 < argc) {
			if (parse_int(argv[++i], &o->depth))
				return -1;
		} else if (!strcmp(argv[i], "--send-slots") && i + 1 < argc) {
			if (parse_int(argv[++i], &o->send_slots))
				return -1;
		} else if (!strcmp(argv[i], "--recv-posts") && i + 1 < argc) {
			if (parse_int(argv[++i], &o->recv_posts))
				return -1;
		} else if (!strcmp(argv[i], "--recv-post-delay-ms") && i + 1 < argc) {
			if (parse_int(argv[++i], &o->recv_post_delay_ms))
				return -1;
		} else if (!strcmp(argv[i], "--recv-wr-id-base") && i + 1 < argc) {
			if (parse_int(argv[++i], &o->recv_wr_id_base))
				return -1;
		} else if (!strcmp(argv[i], "--send-wr-id-base") && i + 1 < argc) {
			if (parse_int(argv[++i], &o->send_wr_id_base))
				return -1;
		} else if (!strcmp(argv[i], "--hold-after-rts-ms") && i + 1 < argc) {
			if (parse_int(argv[++i], &o->hold_after_rts_ms))
				return -1;
		} else if (!strcmp(argv[i], "--hold-before-destroy-ms") && i + 1 < argc) {
			if (parse_int(argv[++i], &o->hold_before_destroy_ms))
				return -1;
		} else if (!strcmp(argv[i], "--count") && i + 1 < argc) {
			if (parse_int(argv[++i], &o->count))
				return -1;
		} else if (!strcmp(argv[i], "--size") && i + 1 < argc) {
			if (parse_size(argv[++i], &o->size))
				return -1;
		} else if (!strcmp(argv[i], "--mtu") && i + 1 < argc) {
			if (parse_int(argv[++i], &o->mtu))
				return -1;
		} else if (!strcmp(argv[i], "--recv-repost-hold-ms") &&
			   i + 1 < argc) {
			if (parse_int(argv[++i], &o->recv_repost_hold_ms))
				return -1;
		} else if (!strcmp(argv[i], "--drain-timeout-ms") && i + 1 < argc) {
			if (parse_int(argv[++i], &o->drain_timeout_ms))
				return -1;
		} else if (!strcmp(argv[i], "--handshake-timeout-ms") &&
			   i + 1 < argc) {
			if (parse_int(argv[++i], &o->handshake_timeout_ms))
				return -1;
		} else if (!strcmp(argv[i], "--credits")) {
			o->credits = 1;
		} else if (!strcmp(argv[i], "--credits=tcp")) {
			o->credits = 1;
			o->credit_xport = CR_XPORT_TCP;
		} else if (!strcmp(argv[i], "--credits=uc")) {
			o->credits = 1;
			o->credit_xport = CR_XPORT_UC;
		} else if (!strcmp(argv[i], "--ctl-depth") && i + 1 < argc) {
			if (parse_int(argv[++i], &o->ctl_depth))
				return -1;
		} else if (!strcmp(argv[i], "--ctl-send-slots") && i + 1 < argc) {
			if (parse_int(argv[++i], &o->ctl_send_slots))
				return -1;
		} else if (!strcmp(argv[i], "--grant-deadline-us") &&
			   i + 1 < argc) {
			if (parse_int(argv[++i], &o->grant_deadline_us))
				return -1;
		} else if (!strcmp(argv[i], "--grant-batch") && i + 1 < argc) {
			if (parse_int(argv[++i], &o->grant_batch))
				return -1;
		} else if (!strcmp(argv[i], "--grant-resend-ms") && i + 1 < argc) {
			if (parse_int(argv[++i], &o->grant_resend_ms))
				return -1;
		} else if (!strcmp(argv[i], "--wr-gap-us") && i + 1 < argc) {
			if (parse_int(argv[++i], &o->wr_gap_us))
				return -1;
		} else if (!strcmp(argv[i], "--recv-keep-posted")) {
			o->recv_keep_posted_set = 1;
		} else if (!strcmp(argv[i], "--recv-no-keep-posted")) {
			o->recv_no_keep_posted = 1;
		} else if (!strcmp(argv[i], "--recv-guard") && i + 1 < argc) {
			if (parse_int(argv[++i], &o->recv_guard))
				return -1;
		} else if (!strcmp(argv[i], "-v") ||
			   !strcmp(argv[i], "--verbose")) {
			o->verbose = 1;
		} else if (!strcmp(argv[i], "--check")) {
			o->check = 1;
		} else if (!strcmp(argv[i], "--check-any-order")) {
			o->check = 1;
			o->check_any_order = 1;
		} else {
			return -1;
		}
	}

	if (!o->role || !o->dev || o->port <= 0 || o->port > 65535 ||
	    o->gid_index < -1 || o->ib_port <= 0 || o->depth <= 0 ||
	    o->depth > 4095 || o->send_slots < 0 || o->recv_posts < 0 ||
	    o->recv_posts > 4095 || o->count <= 0 ||
	    o->hold_after_rts_ms < 0 || o->hold_before_destroy_ms < 0)
		return -1;
	if (o->mtu != 256 && o->mtu != 512 && o->mtu != 1024 &&
	    o->mtu != 2048 && o->mtu != 4096)
		return -1;
	if (o->check_any_order && o->size < sizeof(uint64_t))
		return -1;
	if (strcmp(o->role, "send") && strcmp(o->role, "recv") &&
	    strcmp(o->role, "bidi"))
		return -1;
	if (o->credits && !strcmp(o->role, "bidi")) {
		fprintf(stderr,
			"--credits is not implemented for --role bidi (both peers grant and consume on one QP)\n");
		return -1;
	}
	if ((o->recv_keep_posted_set || o->recv_no_keep_posted) &&
	    strcmp(o->role, "recv")) {
		fprintf(stderr,
			"--recv-keep-posted / --recv-no-keep-posted are receiver options (--role recv)\n");
		return -1;
	}
	if (o->recv_keep_posted_set && o->recv_no_keep_posted) {
		fprintf(stderr,
			"--recv-keep-posted and --recv-no-keep-posted contradict each other\n");
		return -1;
	}
	/*
	 * Receive window policy. Measured 2026-09-06 at
	 * apple_tx_max_inflight_frames=16: window held open, 20/20 clean;
	 * window decaying at the tail, 4 stall events in 19 (two >= 5 s). So
	 * a credited receiver keeps its window posted until FINAL by DEFAULT;
	 * --recv-no-keep-posted is the control arm. An uncredited receiver is
	 * unchanged unless --recv-keep-posted is given explicitly.
	 */
	o->recv_keep_posted = !strcmp(o->role, "recv") &&
			      (o->recv_keep_posted_set ||
			       (o->credits && !o->recv_no_keep_posted));
	if (o->drain_timeout_ms <= 0 || o->handshake_timeout_ms <= 0)
		return -1;
	if (o->ctl_depth <= 0 || o->ctl_depth > 1024 ||
	    o->ctl_send_slots <= 0 || o->ctl_send_slots > 1024 ||
	    o->grant_deadline_us < 0 || o->grant_batch <= 0 ||
	    o->grant_resend_ms <= 0 || o->wr_gap_us < 0 || o->recv_guard < 0)
		return -1;
	if (o->credit_xport == CR_XPORT_UC && !o->credits)
		return -1;
	return 0;
}

static int send_all(int fd, const void *buf, size_t len)
{
	const char *p = buf;

	while (len) {
		ssize_t n = send(fd, p, len, 0);

		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		p += n;
		len -= (size_t)n;
	}
	return 0;
}

static int recv_all(int fd, void *buf, size_t len)
{
	char *p = buf;

	while (len) {
		ssize_t n = recv(fd, p, len, 0);

		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (!n)
			return -1;
		p += n;
		len -= (size_t)n;
	}
	return 0;
}

static int tcp_listen(int port)
{
	struct sockaddr_in addr;
	int fd;
	int one = 1;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons((uint16_t)port);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) ||
	    listen(fd, 1)) {
		perror("tcp listen");
		close(fd);
		return -1;
	}
	return fd;
}

static int tcp_connect(const char *host, int port)
{
	struct addrinfo hints;
	struct addrinfo *res = NULL, *ai;
	char port_s[16];
	int fd = -1;
	int gai;

	snprintf(port_s, sizeof(port_s), "%d", port);
	memset(&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_family = AF_UNSPEC;
	gai = getaddrinfo(host, port_s, &hints, &res);
	if (gai) {
		if (getenv("UC_ONEWAY_DEBUG")) {
			fprintf(stderr, "uc_oneway: getaddrinfo(%s,%s): %s\n",
				host, port_s, gai_strerror(gai));
			fflush(stderr);
		}
		return -1;
	}

	for (ai = res; ai; ai = ai->ai_next) {
		fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if (fd < 0)
			continue;
		if (getenv("UC_ONEWAY_DEBUG")) {
			fprintf(stderr, "uc_oneway: connect family=%d\n",
				ai->ai_family);
			fflush(stderr);
		}
		if (!connect(fd, ai->ai_addr, ai->ai_addrlen))
			break;
		if (getenv("UC_ONEWAY_DEBUG")) {
			fprintf(stderr, "uc_oneway: connect failed errno=%d\n",
				errno);
			fflush(stderr);
		}
		close(fd);
		fd = -1;
	}
	freeaddrinfo(res);
	return fd;
}

static struct ibv_context *open_dev(const char *name)
{
	struct ibv_device **list;
	struct ibv_context *ctx = NULL;
	int n = 0;

	list = ibv_get_device_list(&n);
	if (!list)
		return NULL;
	for (int i = 0; i < n; i++) {
		if (!strcmp(ibv_get_device_name(list[i]), name)) {
			ctx = ibv_open_device(list[i]);
			break;
		}
	}
	ibv_free_device_list(list);
	return ctx;
}

static int exchange_info(int fd, const struct peer_info *local,
			 struct peer_info *remote)
{
	if (send_all(fd, local, sizeof(*local)) ||
	    recv_all(fd, remote, sizeof(*remote))) {
		perror("metadata exchange");
		return -1;
	}
	if (remote->magic != local->magic) {
		fprintf(stderr, "bad remote magic 0x%x\n", remote->magic);
		return -1;
	}
	return 0;
}

static int qp_to_init(struct ibv_qp *qp, int port)
{
	struct ibv_qp_attr a;
	int tn3205 = getenv("UC_ONEWAY_TN3205") != NULL;
	int ret;

	memset(&a, 0, sizeof(a));
	a.qp_state = IBV_QPS_INIT;
	a.pkey_index = 0;
	a.port_num = (uint8_t)port;
	a.qp_access_flags = tn3205 ? 0 :
			    (IBV_ACCESS_LOCAL_WRITE |
			     IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE);
	errno = 0;
	ret = ibv_modify_qp(qp, &a, IBV_QP_STATE | IBV_QP_PKEY_INDEX |
			    IBV_QP_PORT | IBV_QP_ACCESS_FLAGS);
	fprintf(stderr, "modify INIT ret=%d errno=%d access_flags=0x%x\n",
		ret, errno, a.qp_access_flags);
	fflush(stderr);
	if (ret) {
		perror("modify INIT");
		return ret;
	}
	return 0;
}

static int qp_to_rts(struct ibv_qp *qp, int port, int sgid_index,
		     const union ibv_gid *dgid, uint32_t dlid,
		     uint32_t dest_qpn, uint32_t local_psn,
		     uint32_t remote_psn, enum ibv_mtu path_mtu)
{
	struct ibv_qp_attr a;
	int tn3205 = getenv("UC_ONEWAY_TN3205") != NULL;
	int ret;

	memset(&a, 0, sizeof(a));
	a.qp_state = IBV_QPS_RTR;
	a.path_mtu = path_mtu;
	a.rq_psn = remote_psn;
	a.dest_qp_num = dest_qpn;
	a.ah_attr.dlid = (uint16_t)dlid;
	a.ah_attr.sl = 0;
	a.ah_attr.src_path_bits = 0;
	a.ah_attr.port_num = (uint8_t)port;
	/* Match JACCL: only enable global routing when the dgid has a non-zero
	 * interface_id (i.e. it's a real GID, not the zero default). */
	a.ah_attr.is_global = tn3205 ? 1 : 0;
	if (tn3205 || dgid->global.interface_id) {
		a.ah_attr.is_global = 1;
		a.ah_attr.grh.dgid = *dgid;
		a.ah_attr.grh.sgid_index = sgid_index;
		a.ah_attr.grh.hop_limit = 1;
	}
	errno = 0;
	ret = ibv_modify_qp(qp, &a, IBV_QP_STATE | IBV_QP_AV |
			    IBV_QP_PATH_MTU | IBV_QP_DEST_QPN |
			    IBV_QP_RQ_PSN);
	fprintf(stderr,
		"modify RTR ret=%d errno=%d dest_qpn=%u sgid_index=%d dlid=%u is_global=%d\n",
		ret, errno, dest_qpn, sgid_index, dlid, a.ah_attr.is_global);
	fflush(stderr);
	if (ret) {
		perror("modify RTR");
		return ret;
	}

	memset(&a, 0, sizeof(a));
	a.qp_state = IBV_QPS_RTS;
	a.sq_psn = local_psn;
	errno = 0;
	ret = ibv_modify_qp(qp, &a, IBV_QP_STATE | IBV_QP_SQ_PSN);
	fprintf(stderr, "modify RTS ret=%d errno=%d\n", ret, errno);
	fflush(stderr);
	if (ret)
		perror("modify RTS");
	return ret;
}

static uint64_t wr_id_for_slot(int base, int slot)
{
	if (base >= 0)
		return (uint64_t)base | ((uint64_t)slot << 8);
	return (uint64_t)slot;
}

static int slot_from_wr_id(const struct opts *o, uint64_t wr_id)
{
	if (o->recv_wr_id_base >= 0)
		return (int)((wr_id >> 8) & 0xffu);
	return (int)wr_id;
}

static int post_recv_slot(const struct opts *o, struct ibv_qp *qp,
			  struct ibv_mr *mr, char *buf, size_t stride,
			  size_t size, int slot)
{
	struct ibv_sge sge;
	struct ibv_recv_wr wr;
	struct ibv_recv_wr *bad = NULL;

	memset(&sge, 0, sizeof(sge));
	sge.addr = (uintptr_t)(buf + (size_t)slot * stride);
	sge.length = (uint32_t)size;
	sge.lkey = mr->lkey;
	memset(&wr, 0, sizeof(wr));
	wr.wr_id = wr_id_for_slot(o->recv_wr_id_base, slot);
	wr.sg_list = &sge;
	wr.num_sge = 1;
	return ibv_post_recv(qp, &wr, &bad);
}

static void store_le64(char *p, uint64_t v)
{
	for (size_t i = 0; i < sizeof(v); i++)
		p[i] = (char)((v >> (i * 8)) & 0xff);
}

static uint64_t load_le64(const char *p)
{
	uint64_t v = 0;

	for (size_t i = 0; i < sizeof(v); i++)
		v |= (uint64_t)(unsigned char)p[i] << (i * 8);
	return v;
}

static unsigned char pattern_byte(uint64_t seq, size_t off)
{
	return (unsigned char)(0x40u ^ (unsigned int)seq ^
			       (unsigned int)(seq >> 8) ^
			       (unsigned int)(seq >> 16) ^
			       (unsigned int)(off * 131u) ^
			       (unsigned int)(off >> 8));
}

static void fill_pattern(char *p, size_t size, uint64_t seq)
{
	for (size_t i = 0; i < size; i++)
		p[i] = (char)pattern_byte(seq, i);
	if (size >= sizeof(seq))
		store_le64(p, seq);
}

static int check_pattern(const char *p, size_t size, uint64_t expected_seq,
			 int recv_slot, int completed, uint64_t wr_id,
			 uint32_t byte_len)
{
	uint64_t observed_seq = size >= sizeof(observed_seq) ? load_le64(p) : 0;

	for (size_t i = 0; i < size; i++) {
		unsigned char got = (unsigned char)p[i];
		unsigned char want;

		if (i < sizeof(expected_seq) && size >= sizeof(expected_seq))
			want = (unsigned char)((expected_seq >> (i * 8)) & 0xff);
		else
			want = pattern_byte(expected_seq, i);

		if (got != want) {
			fprintf(stderr,
				"check failed completed=%d wr_id=%llu recv_slot=%d expected_seq=%llu observed_seq=%llu byte_len=%u off=%zu got=0x%02x want=0x%02x\n",
				completed, (unsigned long long)wr_id,
				recv_slot, (unsigned long long)expected_seq,
				(unsigned long long)observed_seq, byte_len,
				i, got, want);
			return -1;
		}
	}
	return 0;
}

static int check_seen(uint8_t *seen, int count, uint64_t observed_seq,
		      int recv_slot, int completed, uint64_t wr_id)
{
	size_t byte;
	uint8_t bit;

	if (observed_seq >= (uint64_t)count) {
		fprintf(stderr,
			"check failed completed=%d wr_id=%llu recv_slot=%d observed_seq=%llu out_of_range count=%d\n",
			completed, (unsigned long long)wr_id, recv_slot,
			(unsigned long long)observed_seq, count);
		return -1;
	}

	byte = (size_t)(observed_seq / 8);
	bit = (uint8_t)(1u << (observed_seq % 8));
	if (seen[byte] & bit) {
		fprintf(stderr,
			"check failed completed=%d wr_id=%llu recv_slot=%d observed_seq=%llu duplicate\n",
			completed, (unsigned long long)wr_id, recv_slot,
			(unsigned long long)observed_seq);
		return -1;
	}

	seen[byte] |= bit;
	return 0;
}

static void print_rate(const char *role, int done, int count, size_t size,
		       uint64_t start, uint64_t *last_t, int *last_done)
{
	uint64_t now = now_ns();

	if (now - *last_t < 1000000000ull && done != count)
		return;
	if (done == count && done == *last_done)
		return;

	double dt = (now - *last_t) / 1e9;
	double total = (now - start) / 1e9;
	int delta = done - *last_done;
	double mbps = dt > 0 ? (double)delta * (double)size * 8.0 / dt / 1e6 : 0;

	printf("%s progress done=%d/%d delta=%d rate=%.2f Mbit/s elapsed=%.3f s\n",
	       role, done, count, delta, mbps, total);
	fflush(stdout);
	*last_t = now;
	*last_done = done;
}

static int cr_send_record(int fd, const struct cr_record *r)
{
	uint8_t buf[CR_WIRE_LEN];

	cr_encode(r, buf);
	return send_all(fd, buf, sizeof(buf));
}

static void cr_send_abort(int fd, const struct credit_state *cs,
			  uint64_t reason)
{
	struct cr_record r;

	if (fd < 0)
		return;
	cr_record_init(&r, CR_TYPE_ABORT, cs);
	r.aux = reason;
	r.status = 1;
	(void)cr_send_record(fd, &r);
}

/* `posted_at` is sampled immediately before ibv_post_send(), AFTER the
 * pattern fill: a 64 KiB fill is tens of microseconds and is not TX latency. */
static int post_send_slot(const struct opts *o, struct ibv_qp *qp,
			  struct ibv_mr *mr, char *send_buf, size_t stride,
			  int slot, uint64_t seq, uint64_t wr_id,
			  uint64_t *posted_at)
{
	struct ibv_sge sge;
	struct ibv_send_wr wr;
	struct ibv_send_wr *bad = NULL;

	if (o->check)
		fill_pattern(send_buf + (size_t)slot * stride, o->size, seq);

	memset(&sge, 0, sizeof(sge));
	sge.addr = (uintptr_t)(send_buf + (size_t)slot * stride);
	sge.length = (uint32_t)o->size;
	sge.lkey = mr->lkey;
	memset(&wr, 0, sizeof(wr));
	wr.wr_id = wr_id;
	wr.sg_list = &sge;
	wr.num_sge = 1;
	wr.opcode = IBV_WR_SEND;
	wr.send_flags = IBV_SEND_SIGNALED;
	*posted_at = now_ns();
	return ibv_post_send(qp, &wr, &bad);
}

/* ------------------------------------------------------------------------
 * Per-WR post -> completion latency
 *
 * The bench used to time only the POSTING path, so a sender that held credit
 * and had WRs in flight but whose completions came back late was invisible:
 * 2026-09-06 zeus->Mac delivered 1981 messages in 1.0 s and the last 19 in
 * 0.7 s with credit_stalls=0. Every WR is now stamped at post and at
 * completion. Fixed-size log-scale histogram; nothing is allocated on the
 * hot path; the cost is two clock_gettime() per WR.
 *
 *   data-send        sender:   SEND post -> completion (TX latency)
 *   data-recv-dwell  receiver: RECV post -> completion, i.e. how long a
 *                    posted buffer WAITED for a message. That is receive-side
 *                    dwell, not latency, and is labelled as such everywhere.
 *   ctl-send         receiver: grant SEND post -> completion (control TX)
 *   ctl-recv-dwell   sender:   control RECV dwell (grant inter-arrival)
 *
 * Buckets: 1 us wide below 64 us, then four per octave up to 64 us << 18
 * (16.8 s), one overflow bucket above. A percentile is reported as the UPPER
 * edge of the bucket that holds its rank, capped at the exact running max, so
 * it is always a bound ("p99 <= X"), never an interpolation.
 * ------------------------------------------------------------------------ */

#define WL_LIN_US 64u
#define WL_SUB_SHIFT 2u
#define WL_OCTAVES 18u
#define WL_TOP_US ((uint64_t)WL_LIN_US << WL_OCTAVES)
#define WL_NBUCKETS (WL_LIN_US + (WL_OCTAVES << WL_SUB_SHIFT) + 1u)
#define WL_TRACE_MAX 50

struct wr_lat {
	const char *kind;
	int trace;              /* gaps of this kind feed the stall trace */
	int outstanding;
	uint64_t last_event;    /* last completion, or the post ending an idle spell */
	uint64_t wait_marks;    /* threshold multiples already reported this spell */
	uint64_t gaps;          /* completion gaps over the threshold */
	uint64_t n;
	uint64_t sum_ns;
	uint64_t max_ns;
	uint64_t min_ns;
	uint64_t bucket[WL_NBUCKETS];
};

struct wr_trace {
	FILE *out;              /* NULL: count only (selftest) */
	const char *role;
	const struct credit_state *cs;
	uint64_t gap_ns;
	uint64_t t0;
	uint64_t total;
	int printed;
	uint64_t wait_total;    /* "still waiting" marks, printed or not */
	int wait_printed;
};

static void wl_init(struct wr_lat *w, const char *kind, int trace)
{
	memset(w, 0, sizeof(*w));
	w->kind = kind;
	w->trace = trace;
}

static unsigned wl_bucket(uint64_t ns)
{
	uint64_t us = ns / 1000u;
	unsigned k = 0;

	if (us < WL_LIN_US)
		return (unsigned)us;
	if (us >= WL_TOP_US)
		return WL_NBUCKETS - 1u;
	while (us >= ((uint64_t)WL_LIN_US << (k + 1u)))
		k++;
	/* octave k spans [64<<k, 128<<k); a sub-bucket is a quarter of it */
	return WL_LIN_US + (k << WL_SUB_SHIFT) +
	       (unsigned)((us - ((uint64_t)WL_LIN_US << k)) >>
			  (k + 6u - WL_SUB_SHIFT));
}

static uint64_t wl_bucket_upper_us(unsigned i)
{
	unsigned k, sub;

	if (i < WL_LIN_US)
		return (uint64_t)i + 1u;
	if (i >= WL_NBUCKETS - 1u)
		return UINT64_MAX;
	k = (i - WL_LIN_US) >> WL_SUB_SHIFT;
	sub = (i - WL_LIN_US) & ((1u << WL_SUB_SHIFT) - 1u);
	return ((uint64_t)WL_LIN_US << k) +
	       ((uint64_t)(sub + 1u) << (k + 6u - WL_SUB_SHIFT));
}

static void wl_record(struct wr_lat *w, uint64_t ns)
{
	w->bucket[wl_bucket(ns)]++;
	w->n++;
	w->sum_ns += ns;
	if (ns > w->max_ns)
		w->max_ns = ns;
	if (w->n == 1 || ns < w->min_ns)
		w->min_ns = ns;
}

static uint64_t wl_percentile_us(const struct wr_lat *w, unsigned pct)
{
	uint64_t rank, cum = 0, max_us;

	if (!w->n)
		return 0;
	rank = (w->n * pct + 99u) / 100u;
	if (rank < 1)
		rank = 1;
	max_us = (w->max_ns + 999u) / 1000u;
	for (unsigned i = 0; i < WL_NBUCKETS; i++) {
		cum += w->bucket[i];
		if (cum >= rank) {
			uint64_t up = wl_bucket_upper_us(i);

			return up < max_us ? up : max_us;
		}
	}
	return max_us;
}

static void wl_on_post(struct wr_lat *w, uint64_t now)
{
	if (w->outstanding == 0) {
		w->last_event = now;
		w->wait_marks = 0;
	}
	w->outstanding++;
}

static void wl_trace_gap(const struct wr_lat *w, struct wr_trace *t,
			 uint64_t gap, uint64_t lat, uint64_t now,
			 unsigned wc_status)
{
	if (!t->out)
		return;
	if (t->printed >= WL_TRACE_MAX) {
		if (t->printed == WL_TRACE_MAX) {
			fprintf(t->out,
				"wr-gap: %d lines printed; further gaps are counted only\n",
				WL_TRACE_MAX);
			t->printed++;
		}
		return;
	}
	t->printed++;
	fprintf(t->out,
		"wr-gap: t=%+.6f role=%s kind=%s gap_ms=%.3f lat_ms=%.3f wc_status=%u outstanding=%d granted=%llu reserved=%llu\n",
		(double)(int64_t)(now - t->t0) / 1e9, t->role, w->kind,
		(double)gap / 1e6, (double)lat / 1e6, wc_status, w->outstanding,
		(unsigned long long)(t->cs ? t->cs->granted : 0),
		(unsigned long long)(t->cs ? t->cs->reserved : 0));
}

/*
 * A "gap" is the time since the last completion of this kind, or since the
 * post that ended an idle spell -- so it only ever measures an interval in
 * which at least one WR of this kind was outstanding the whole time. The
 * trace fires on the ONSET of a stall; the per-WR latencies of everything
 * queued behind it land in the histogram.
 *
 * Error completions are stamped too. The 2026-09-06 ab-tcp-d8-1 stall ended
 * in wc status=12 after 5.3 s and left NO trace line, because the sender
 * rejected the completion before the hook ran. The status travels in the
 * line so a flush is never mistaken for a late success.
 */
static void wl_on_complete(struct wr_lat *w, struct wr_trace *t,
			   uint64_t posted_at, uint64_t now, unsigned wc_status)
{
	uint64_t lat = now > posted_at ? now - posted_at : 0;
	uint64_t gap = now > w->last_event ? now - w->last_event : 0;

	wl_record(w, lat);
	if (w->trace && gap > t->gap_ns) {
		w->gaps++;
		t->total++;
		wl_trace_gap(w, t, gap, lat, now, wc_status);
	}
	w->last_event = now;
	w->wait_marks = 0;
	if (w->outstanding > 0)
		w->outstanding--;
}

/*
 * Called from the poll loop when it found nothing. A stall that never
 * completes -- or completes only when the provider gives up -- is otherwise
 * invisible until the run ends. One line per threshold multiple of waiting
 * (5, 10, 15 ms ... at the default), so the log shows the stall WHILE it is
 * happening; capped like the trace, always counted. `waiting_ms` is since
 * the last completion of this kind or the post that opened the interval.
 */
static void wl_poll_idle(struct wr_lat *w, struct wr_trace *t, uint64_t now)
{
	uint64_t waited, mark;

	if (!w->trace || w->outstanding <= 0 || !t->gap_ns)
		return;
	waited = now > w->last_event ? now - w->last_event : 0;
	mark = waited / t->gap_ns;
	if (mark <= w->wait_marks)
		return;
	w->wait_marks = mark;
	t->wait_total++;
	if (!t->out)
		return;
	if (t->wait_printed >= WL_TRACE_MAX) {
		if (t->wait_printed == WL_TRACE_MAX) {
			fprintf(t->out,
				"wr-wait: %d lines printed; further waits are counted only\n",
				WL_TRACE_MAX);
			t->wait_printed++;
		}
		return;
	}
	t->wait_printed++;
	fprintf(t->out,
		"wr-wait: t=%+.6f role=%s kind=%s still waiting waiting_ms=%.3f outstanding=%d granted=%llu reserved=%llu\n",
		(double)(int64_t)(now - t->t0) / 1e9, t->role, w->kind,
		(double)waited / 1e6, w->outstanding,
		(unsigned long long)(t->cs ? t->cs->granted : 0),
		(unsigned long long)(t->cs ? t->cs->reserved : 0));
}

static void wl_print_config(FILE *f, const struct wr_trace *t, int is_sender,
			    int use_uc)
{
	fprintf(f,
		"wr-lat config: role=%s gap_threshold_us=%llu hist=1us/bucket<%uus,%u/octave to %lluus,overflow above pctl=bucket_upper_edge(capped at max) data=%s ctl=%s trace=%s\n",
		t->role, (unsigned long long)(t->gap_ns / 1000u), WL_LIN_US,
		1u << WL_SUB_SHIFT, (unsigned long long)WL_TOP_US,
		is_sender ? "data-send(post->completion)" :
			    "data-recv-dwell(post->completion,receive-side dwell)",
		!use_uc ? "n/a(tcp)" :
		is_sender ? "ctl-recv-dwell(post->completion)" :
			    "ctl-send(post->completion)",
		is_sender ? "data-send" : (use_uc ? "data-recv-dwell,ctl-send" :
						    "data-recv-dwell"));
}

static void wl_print(FILE *f, const char *role, const struct wr_lat *w)
{
	fprintf(f,
		"%s wr-lat: kind=%s p50_us=%llu p90_us=%llu p99_us=%llu max_us=%.1f mean_us=%.3f min_us=%.1f n=%llu gaps_over_threshold=%llu\n",
		role, w->kind, (unsigned long long)wl_percentile_us(w, 50),
		(unsigned long long)wl_percentile_us(w, 90),
		(unsigned long long)wl_percentile_us(w, 99),
		(double)w->max_ns / 1000.0,
		w->n ? (double)w->sum_ns / (double)w->n / 1000.0 : 0.0,
		w->n ? (double)w->min_ns / 1000.0 : 0.0,
		(unsigned long long)w->n, (unsigned long long)w->gaps);
}

static void wl_print_gaps(FILE *f, const struct wr_trace *t)
{
	fprintf(f,
		"%s wr-gaps: threshold_us=%llu total=%llu printed=%d waits=%llu waits_printed=%d\n",
		t->role, (unsigned long long)(t->gap_ns / 1000u),
		(unsigned long long)t->total,
		t->printed > WL_TRACE_MAX ? WL_TRACE_MAX : t->printed,
		(unsigned long long)t->wait_total,
		t->wait_printed > WL_TRACE_MAX ? WL_TRACE_MAX : t->wait_printed);
}

/* ------------------------------------------------------------------------
 * On-link control ring (--credits=uc)
 *
 * The grant channel reuses the EXISTING UC QP's otherwise unused reverse
 * direction. It gets its own registered 4 KiB buffers and its own wr_id space
 * so the CQ loop can route a control completion without ever consulting -- or
 * consuming -- an application work request.
 * ------------------------------------------------------------------------ */

#define CTL_WR_ID_FLAG 0x4000000000000000ull
#define CTL_WR_ID_SEND 0x2000000000000000ull
#define CTL_WR_ID_SLOT 0x00000000ffffffffull

static int ctl_wr_is_control(uint64_t wr_id)
{
	return (wr_id & CTL_WR_ID_FLAG) != 0;
}

static int ctl_wr_is_send(uint64_t wr_id)
{
	return (wr_id & CTL_WR_ID_SEND) != 0;
}

static uint32_t ctl_wr_slot(uint64_t wr_id)
{
	return (uint32_t)(wr_id & CTL_WR_ID_SLOT);
}

struct ctl_ring {
	char *buf;              /* n_recv + n_send frames of CR_FRAME_BYTES */
	struct ibv_mr *mr;
	struct ibv_qp *qp;
	int n_recv;
	int n_send;
	int next_send;          /* round-robin cursor over the send slots */
	uint8_t *send_busy;
	uint64_t recv_posted;   /* cumulative SUCCESSFUL control reposts */
	uint64_t recv_done;     /* cumulative control records received */
	uint64_t send_posted;
	uint64_t send_done;
	uint64_t send_fails;
	/* post timestamps for the WR kind this side owns (sender: receives,
	 * receiver: sends); the other array stays NULL */
	uint64_t *recv_ts;
	uint64_t *send_ts;
	struct wr_lat *lat;
};

static char *ctl_recv_frame(const struct ctl_ring *c, int slot)
{
	return c->buf + (size_t)slot * CR_FRAME_BYTES;
}

static char *ctl_send_frame(const struct ctl_ring *c, int slot)
{
	return c->buf + (size_t)(c->n_recv + slot) * CR_FRAME_BYTES;
}

static int ctl_post_recv(struct ctl_ring *c, int slot)
{
	struct ibv_sge sge;
	struct ibv_recv_wr wr;
	struct ibv_recv_wr *bad = NULL;
	uint64_t now;
	int ret;

	memset(&sge, 0, sizeof(sge));
	sge.addr = (uintptr_t)ctl_recv_frame(c, slot);
	sge.length = CR_FRAME_BYTES;
	sge.lkey = c->mr->lkey;
	memset(&wr, 0, sizeof(wr));
	wr.wr_id = CTL_WR_ID_FLAG | (uint64_t)(uint32_t)slot;
	wr.sg_list = &sge;
	wr.num_sge = 1;
	now = now_ns();
	ret = ibv_post_recv(c->qp, &wr, &bad);
	if (!ret) {
		c->recv_posted++;   /* only a SUCCESSFUL post is capacity */
		if (c->recv_ts) {
			c->recv_ts[slot] = now;
			wl_on_post(c->lat, now);
		}
	}
	return ret;
}

static int ctl_free_send_slot(const struct ctl_ring *c)
{
	for (int i = 0; i < c->n_send; i++) {
		int slot = (c->next_send + i) % c->n_send;

		if (!c->send_busy[slot])
			return slot;
	}
	return -1;
}

/* Post one 4 KiB control SEND carrying a 64-byte record. Returns 0 on success,
 * 1 when no slot is free (caller keeps the snapshot pending), -1 on error. */
static int ctl_post_send(struct ctl_ring *c, const struct cr_record *r)
{
	struct ibv_sge sge;
	struct ibv_send_wr wr;
	struct ibv_send_wr *bad = NULL;
	int slot = ctl_free_send_slot(c);
	uint64_t now;
	char *frame;

	if (slot < 0)
		return 1;
	frame = ctl_send_frame(c, slot);
	memset(frame, 0, CR_FRAME_BYTES);
	cr_encode(r, (uint8_t *)frame);

	memset(&sge, 0, sizeof(sge));
	sge.addr = (uintptr_t)frame;
	sge.length = CR_FRAME_BYTES;
	sge.lkey = c->mr->lkey;
	memset(&wr, 0, sizeof(wr));
	wr.wr_id = CTL_WR_ID_FLAG | CTL_WR_ID_SEND | (uint64_t)(uint32_t)slot;
	wr.sg_list = &sge;
	wr.num_sge = 1;
	wr.opcode = IBV_WR_SEND;
	wr.send_flags = IBV_SEND_SIGNALED;
	now = now_ns();
	if (ibv_post_send(c->qp, &wr, &bad)) {
		c->send_fails++;
		return -1;
	}
	c->send_busy[slot] = 1;
	c->next_send = (slot + 1) % c->n_send;
	c->send_posted++;
	if (c->send_ts) {
		c->send_ts[slot] = now;
		wl_on_post(c->lat, now);
	}
	return 0;
}

/* ------------------------------------------------------------------------
 * The publisher: one entry point, either transport.
 * ------------------------------------------------------------------------ */

struct pub_ctx {
	struct grant_pub *gp;
	struct credit_state *cs;
	struct cr_out *out;        /* TCP queue (also the UC escape hatch) */
	struct ctl_ring *ctl;      /* NULL on the TCP transport */
	struct ctl_credit *cc;     /* NULL on the TCP transport */
};

static void pub_snapshot(const struct pub_ctx *p, struct cr_record *rec)
{
	cr_record_init(rec, CR_TYPE_GRANT, p->cs);
}

/*
 * Hand the current cumulative snapshot to the transport.
 *
 *   force        publish regardless of the coalescing batch (deadline, idle
 *                CQ, peer RESEND, end of run)
 *   escape_tcp   the UC control window is empty and has been for too long;
 *                fall back to TCP for this snapshot rather than deadlock
 *
 * Returns -1 on a hard transport failure, else 0.
 */
static int pub_publish(struct pub_ctx *p, uint64_t now, int force,
		       int escape_tcp)
{
	struct grant_pub *g = p->gp;
	struct cr_out_done done = { 0, 0, 0 };
	struct cr_record rec;
	int by_deadline = 0;

	if (g->pending_since && g->deadline_ns &&
	    now - g->pending_since >= g->deadline_ns) {
		force = 1;
		by_deadline = 1;
	}

	if (g->xport == CR_XPORT_UC && !escape_tcp) {
		/* Drain any TCP escape traffic first so it cannot pile up. */
		if (cr_out_progress(p->out, &done) < 0)
			return -1;
		if (done.records)
			grant_pub_done(g, done.total, done.since, by_deadline);
		if (p->cs->granted == g->last_sent)
			return 0;
		if (!force &&
		    p->cs->granted - g->last_sent < g->batch_max_grants)
			return 0;
		if (!ctl_credit_avail(p->cc)) {
			if (!p->cc->stall_since) {
				p->cc->stall_since = now;
				p->cc->stalls++;
			}
			g->blocked++;
			return 0;
		}
		pub_snapshot(p, &rec);
		{
			int r = ctl_post_send(p->ctl, &rec);

			if (r < 0)
				return -1;
			if (r > 0) {   /* no free control SEND slot yet */
				g->blocked++;
				return 0;
			}
		}
		(void)ctl_credit_take(p->cc);
		if (p->cc->stall_since) {
			p->cc->stall_ns += now - p->cc->stall_since;
			p->cc->stall_since = 0;
		}
		g->uc_pubs++;
		grant_pub_done(g, rec.cum_grants, g->pending_since,
			       by_deadline);
		g->pending_since = 0;
		return 0;
	}

	/* TCP transport, or the UC escape hatch. */
	if (cr_out_progress(p->out, &done) < 0)
		return -1;
	if (done.records)
		grant_pub_done(g, done.total, done.since, by_deadline);
	if (p->cs->granted == g->last_sent)
		return 0;
	if (!force && p->cs->granted - g->last_sent < g->batch_max_grants)
		return 0;
	pub_snapshot(p, &rec);
	{
		uint64_t since = g->pending_since ? g->pending_since : now;

		done.records = 0;
		if (cr_out_submit(p->out, &rec, since, &done) < 0)
			return -1;
		g->pending_since = 0;
		if (done.records) {
			g->tcp_pubs++;
			if (escape_tcp && p->cc)
				p->cc->tcp_escapes++;
			grant_pub_done(g, done.total, done.since, by_deadline);
		}
	}
	return 0;
}

/* ------------------------------------------------------------------------
 * Sender-side control progress
 *
 * Both transports feed ONE monotonic ledger. The CQ loop routes by wr_id, so a
 * control record never consumes an application receive and an application
 * completion is never mistaken for credit.
 * ------------------------------------------------------------------------ */

struct sender_ctx {
	struct ibv_cq *cq;
	struct credit_state *cs;
	struct cr_chan *chan;
	struct cr_out *out;
	struct ctl_ring *ctl;
	int use_uc;
	int completed;
	int in_flight;
	uint64_t grants_uc;
	uint64_t grants_tcp;
	uint64_t grants_dup;
	uint64_t grants_stale;
	uint64_t last_grant_progress;
	uint64_t ctl_reposts;
	uint64_t ctl_repost_fail;
	uint64_t ctl_bad_records;
	uint64_t ctl_wc_errors;
	uint64_t ctlwin_sent;
	uint64_t ctlwin_last;
	uint64_t resend_requests;
	const char *fail_reason;
	/* Data SENDs complete in order on one SQ, so a FIFO of post
	 * timestamps (ring = --depth >= in_flight) pairs each completion
	 * with its post without depending on how wr_id is encoded. */
	struct wr_lat *lat_data;
	struct wr_trace *tr;
	uint64_t *send_ts;
	uint64_t *send_ids;     /* wr_id beside each post timestamp */
	unsigned ring;
	uint64_t post_seq;
	uint64_t done_seq;      /* advanced by success AND error completions */
	uint64_t wc_errors;
};

#define SX_OUTSTANDING_MAX 64

/*
 * A run that ends with posted != completed owes the list of WRs that never
 * completed at all: wr_id and post offset (seconds from `t0`, the same
 * origin as `elapsed=`). Error completions have already left the FIFO, so
 * this is the set the provider still owes. Returns that count; `f` may be
 * NULL (selftest).
 */
static uint64_t sender_print_outstanding(FILE *f, const struct sender_ctx *sx,
					 int posted, uint64_t t0)
{
	uint64_t n = sx->post_seq - sx->done_seq;

	if (!f)
		return n;
	fprintf(f,
		"send outstanding: posted=%d completed=%d wc_errors=%llu never_completed=%llu (wr_id@post_offset_s):",
		posted, sx->completed, (unsigned long long)sx->wc_errors,
		(unsigned long long)n);
	for (uint64_t i = 0; i < n && i < SX_OUTSTANDING_MAX; i++) {
		unsigned idx = (unsigned)((sx->done_seq + i) % sx->ring);

		fprintf(f, " %llu@%+.6f",
			(unsigned long long)sx->send_ids[idx],
			(double)(int64_t)(sx->send_ts[idx] - t0) / 1e9);
	}
	if (n > SX_OUTSTANDING_MAX)
		fprintf(f, " ... +%llu more",
			(unsigned long long)(n - SX_OUTSTANDING_MAX));
	fputc('\n', f);
	return n;
}

static int sender_apply_record(struct sender_ctx *sx, const struct cr_record *rec,
			       int from_uc, int verbose)
{
	enum cr_result r;

	/* Sender-originated types can only be an echo; the FINAL exchange has
	 * its own loop. Neither carries data capacity. */
	if (rec->type == CR_TYPE_CTLWIN || rec->type == CR_TYPE_RESEND ||
	    rec->type == CR_TYPE_FINAL)
		return 0;

	r = credit_apply_grant(sx->cs, rec);
	if (r == CR_OK) {
		if (from_uc)
			sx->grants_uc++;
		else
			sx->grants_tcp++;
		sx->last_grant_progress = now_ns();
		if (verbose)
			fprintf(stderr,
				"credits: grant(%s) -> %llu messages / %llu frames (available=%llu)\n",
				from_uc ? "uc" : "tcp",
				(unsigned long long)sx->cs->granted,
				(unsigned long long)sx->cs->frames_granted,
				(unsigned long long)credit_available(sx->cs));
		return 0;
	}
	if (r == CR_DUPLICATE) {
		sx->grants_dup++;
		return 0;
	}
	if (r == CR_STALE) {
		/* Cumulative totals make a reordered record harmless: the
		 * ledger keeps the larger one and the next record recovers
		 * anything the lost one carried. */
		sx->grants_stale++;
		return 0;
	}
	fprintf(stderr, "credits: %s record type=%s; aborting\n",
		cr_result_str(r), cr_type_str(rec->type));
	sx->fail_reason = cr_result_str(r);
	return -1;
}

/* Poll the CQ once, routing control completions by wr_id. */
static int sender_poll_cq(struct sender_ctx *sx, int verbose)
{
	struct ibv_wc wc[32];
	int n = ibv_poll_cq(sx->cq, 32, wc);

	if (n < 0) {
		fprintf(stderr, "ibv_poll_cq failed: %d\n", n);
		sx->fail_reason = "ibv_poll_cq failed";
		return -1;
	}
	for (int i = 0; i < n; i++) {
		uint64_t id = wc[i].wr_id;

		if (sx->use_uc && ctl_wr_is_control(id)) {
			int slot = (int)ctl_wr_slot(id);

			if (ctl_wr_is_send(id))
				continue;   /* the sender owns no control SENDs */
			if (sx->ctl->recv_ts && slot < sx->ctl->n_recv)
				wl_on_complete(sx->ctl->lat, sx->tr,
					       sx->ctl->recv_ts[slot], now_ns(),
					       wc[i].status);
			if (wc[i].status == IBV_WC_SUCCESS) {
				struct cr_record rec;
				enum cr_result r;

				sx->ctl->recv_done++;
				r = cr_decode((const uint8_t *)
					      ctl_recv_frame(sx->ctl, slot),
					      &rec);
				if (r == CR_OK) {
					if (sender_apply_record(sx, &rec, 1,
								verbose))
						return -1;
				} else {
					sx->ctl_bad_records++;
				}
			} else {
				sx->ctl_wc_errors++;
			}
			/* Replenish immediately: a control receive that is not
			 * reposted permanently shrinks the peer's permission to
			 * send grants. */
			if (ctl_post_recv(sx->ctl, slot))
				sx->ctl_repost_fail++;
			else
				sx->ctl_reposts++;
			continue;
		}
		/* Stamp BEFORE judging the status: an error completion that
		 * ends a stall is the one line the trace must not lose. */
		if (sx->send_ts && sx->done_seq < sx->post_seq) {
			wl_on_complete(sx->lat_data, sx->tr,
				       sx->send_ts[sx->done_seq % sx->ring],
				       now_ns(), wc[i].status);
			sx->done_seq++;
		}
		if (wc[i].status != IBV_WC_SUCCESS) {
			sx->wc_errors++;
			fprintf(stderr,
				"send wc error wr_id=%llu status=%u opcode=%u\n",
				(unsigned long long)id, wc[i].status,
				wc[i].opcode);
			sx->fail_reason = "send wc error";
			return -1;
		}
		sx->completed++;
		sx->in_flight--;
	}
	return n;
}

static int sender_drain_tcp(struct sender_ctx *sx, int timeout_ms, int verbose)
{
	struct cr_record rec;
	enum cr_result r;
	int cr;

	while ((cr = cr_chan_poll(sx->chan, timeout_ms, &rec, &r)) == 1) {
		timeout_ms = 0;
		if (sender_apply_record(sx, &rec, 0, verbose))
			return -1;
	}
	if (cr < 0) {
		fprintf(stderr, "credits: control channel failure (%s)\n",
			cr_result_str(r));
		sx->fail_reason = "credit channel failure";
		return -1;
	}
	return 0;
}

/*
 * Publish the control-receive window over TCP. Cumulative, so a lost CTLWIN
 * costs the receiver a pause and never an overrun; the control window is never
 * itself gated on the control window.
 */
static int sender_publish_ctlwin(struct sender_ctx *sx, int force)
{
	struct cr_out_done d = { 0, 0, 0 };
	struct cr_record cw;
	uint64_t batch;

	if (cr_out_progress(sx->out, &d) < 0) {
		sx->fail_reason = "control window send failed";
		return -1;
	}
	if (!sx->use_uc)
		return 0;
	batch = (uint64_t)(sx->ctl->n_recv / 4);
	if (batch < 1)
		batch = 1;
	if (sx->ctl->recv_posted == sx->ctlwin_last)
		return 0;
	if (!force && sx->ctl->recv_posted - sx->ctlwin_last < batch)
		return 0;
	cr_record_init(&cw, CR_TYPE_CTLWIN, sx->cs);
	cw.recv_accepted = (uint32_t)sx->ctl->n_recv;
	cw.cum_grants = sx->ctl->recv_posted;
	cw.cum_frames = sx->ctl->recv_posted;
	if (cr_out_submit(sx->out, &cw, 0, &d) < 0) {
		sx->fail_reason = "control window send failed";
		return -1;
	}
	sx->ctlwin_last = sx->ctl->recv_posted;
	sx->ctlwin_sent++;
	return 0;
}

/*
 * Starved with no grant progress: ask over TCP for the latest cumulative
 * snapshot. This requests a re-publication; it never invents capacity.
 */
static int sender_request_resend(struct sender_ctx *sx)
{
	struct cr_out_done d = { 0, 0, 0 };
	struct cr_record rr;

	cr_record_init(&rr, CR_TYPE_RESEND, sx->cs);
	rr.aux = sx->cs->granted;   /* the total the sender has actually seen */
	if (cr_out_submit(sx->out, &rr, 0, &d) < 0) {
		sx->fail_reason = "resend request send failed";
		return -1;
	}
	sx->resend_requests++;
	return 0;
}

/* ------------------------------------------------------------------------
 * Receiver-side TCP control
 *
 * The grant channel may be on the link, but ABORT, the control-window
 * bootstrap (CTLWIN) and the lost-record repair request (RESEND) always come
 * back over TCP. Returns 1 on peer ABORT, 0 normally, -1 on failure.
 * ------------------------------------------------------------------------ */
static int recv_drain_tcp(struct cr_chan *chan, struct pub_ctx *pub,
			  struct ctl_credit *cc, int timeout_ms,
			  uint64_t *abort_aux, uint64_t *ctlwin_records,
			  enum cr_result *res)
{
	struct cr_record rec;
	int cr;

	while ((cr = cr_chan_poll(chan, timeout_ms, &rec, res)) == 1) {
		timeout_ms = 0;
		if (rec.type == CR_TYPE_ABORT) {
			*abort_aux = rec.aux;
			return 1;
		}
		if (rec.type == CR_TYPE_CTLWIN) {
			if (cc && ctl_credit_apply(cc, rec.cum_grants))
				(*ctlwin_records)++;
			continue;
		}
		if (rec.type == CR_TYPE_RESEND) {
			/*
			 * The sender is starved and has seen no grant progress.
			 * Rewind our notion of what it knows to the total IT
			 * reports, then re-publish over TCP -- the reliable
			 * repair channel. This re-sends a snapshot; it never
			 * mints capacity.
			 */
			uint64_t now = now_ns();

			if (rec.aux < pub->gp->last_sent)
				pub->gp->last_sent = rec.aux;
			pub->gp->resends++;
			if (pub_publish(pub, now, 1, 1) < 0)
				return -1;
			continue;
		}
		/* READY/GRANT/FINAL are not expected inbound here. */
	}
	if (cr < 0)
		return -1;
	return 0;
}

/* Drain the non-blocking output queue before any blocking write on the same
 * socket; interleaving the two would corrupt the record stream. */
static int cr_out_drain(struct cr_out *o, int timeout_ms)
{
	struct cr_out_done d = { 0, 0, 0 };
	uint64_t deadline = now_ns() + (uint64_t)timeout_ms * 1000000ull;

	while (!cr_out_idle(o)) {
		if (cr_out_progress(o, &d) < 0)
			return -1;
		if (cr_out_idle(o))
			break;
		if (now_ns() >= deadline)
			return -1;
	}
	return 0;
}

/* ------------------------------------------------------------------------
 * Completion handshake
 *
 * A run is evidence of peer quiescence only if the peer's FINAL carries the
 * expected type, THIS generation, completed == --count and status 0. A FINAL
 * that fails any of those, an ABORT, or no FINAL before the deadline is a row
 * FAIL with a reason, never a log line.
 *
 * The control channel does NOT go quiet just because we sent FINAL. The peer's
 * in-flight GRANT tail, a CTLWIN replenishment, or a RESEND it issued while
 * still starved can all arrive first, and on 2026-09-06 a RESEND read first
 * failed a run that transferred 2000/2000 bit-perfect. Every non-terminal
 * control record is consumed, counted and ignored; only FINAL and ABORT end
 * the wait.
 * ------------------------------------------------------------------------ */

static int final_is_terminal(const struct cr_record *rec)
{
	return rec->type == CR_TYPE_FINAL || rec->type == CR_TYPE_ABORT;
}

static int final_validate(const struct cr_record *rec, uint32_t generation,
			  uint64_t count, const char **reason)
{
	if (rec->type != CR_TYPE_FINAL) {
		*reason = rec->type == CR_TYPE_ABORT ?
			  "peer ABORT instead of FINAL" :
			  "peer record is not FINAL";
		return -1;
	}
	if (rec->generation != generation) {
		*reason = "peer FINAL generation mismatch";
		return -1;
	}
	if (rec->aux != count) {
		*reason = "peer FINAL completed != count";
		return -1;
	}
	if (rec->status) {
		*reason = "peer FINAL status != 0";
		return -1;
	}
	*reason = "peer FINAL valid";
	return 0;
}

/*
 * Wait for a terminal record. `next` yields one: 1 = got it, 0 = deadline
 * reached, -1 = channel failure. The socket loop and the selftest drive the
 * SAME walk; only the source differs. Returns 0 only on a valid peer FINAL.
 */
typedef int (*final_next_fn)(void *ctx, struct cr_record *out);

static int final_wait(final_next_fn next, void *ctx, FILE *log,
		      uint32_t generation, uint64_t count, uint64_t *ignored,
		      const char **reason)
{
	struct cr_record rec;

	for (;;) {
		int cr = next(ctx, &rec);

		if (cr < 0) {
			*reason = "FINAL exchange channel failure";
			return -1;
		}
		if (!cr) {
			*reason = "no peer FINAL record within 5000 ms";
			return -1;
		}
		if (!final_is_terminal(&rec)) {
			/* A legitimate late control record, not a verdict. */
			(*ignored)++;
			if (log)
				fprintf(log,
					"credits: FINAL wait: ignoring peer %s generation=0x%08x (still waiting for FINAL)\n",
					cr_type_str(rec.type), rec.generation);
			continue;
		}
		{
			int ok = final_validate(&rec, generation, count, reason);

			if (log)
				fprintf(log,
					"credits: peer %s generation=0x%08x completed=%llu status=%u expected_generation=0x%08x expected_count=%llu ignored_records=%llu -> %s\n",
					cr_type_str(rec.type), rec.generation,
					(unsigned long long)rec.aux, rec.status,
					generation, (unsigned long long)count,
					(unsigned long long)*ignored, *reason);
			return ok;
		}
	}
}

struct final_sock_ctx {
	struct cr_chan *chan;
	uint64_t deadline;
};

static int final_next_sock(void *vctx, struct cr_record *out)
{
	struct final_sock_ctx *c = vctx;
	enum cr_result r;

	while (now_ns() < c->deadline) {
		int cr = cr_chan_poll(c->chan, 50, out, &r);

		if (cr < 0) {
			fprintf(stderr, "credits: control channel (%s)\n",
				cr_result_str(r));
			return -1;
		}
		if (cr)
			return 1;
	}
	return 0;
}

/* Send our FINAL, then wait up to 5 s for a VALID peer FINAL. Returns 0 only
 * when the peer confirmed the same generation and the full count. */
static int credit_final_exchange(int sock, struct cr_chan *chan,
				 struct cr_out *out, const struct credit_state *cs,
				 uint64_t my_completed, uint32_t my_status,
				 uint32_t recv_accepted, uint32_t recv_requested,
				 uint64_t count, const char **reason)
{
	struct final_sock_ctx ctx = { chan, now_ns() + 5000000000ull };
	uint64_t ignored = 0;
	struct cr_record rec;
	int ok;

	/* A half-written record still in the non-blocking queue would splice
	 * itself into the middle of the blocking FINAL write. */
	if (cr_out_drain(out, 1000))
		fprintf(stderr,
			"credits: control output queue did not drain before FINAL\n");
	cr_record_init(&rec, CR_TYPE_FINAL, cs);
	rec.aux = my_completed;
	rec.status = my_status;
	rec.recv_accepted = recv_accepted;
	rec.recv_requested = recv_requested;
	if (cr_send_record(sock, &rec)) {
		*reason = "FINAL send failed";
		fprintf(stderr, "credits: %s\n", *reason);
		return -1;
	}
	ok = final_wait(final_next_sock, &ctx, stderr, cs->generation, count,
			&ignored, reason);
	if (ok)
		fprintf(stderr, "credits: %s (ignored_records=%llu)\n", *reason,
			(unsigned long long)ignored);
	fflush(stderr);
	return ok;
}

/* Did the DATA arrive intact and complete? This is independent of the control
 * plane: a late RESEND or a missing FINAL says nothing about the bytes. */
static int recv_data_ok(int mismatches, int wc_errors, int repost_failures,
			int completed, int count)
{
	return !(mismatches || wc_errors || repost_failures ||
		 completed < count);
}

/* The ONE definition of a failed receive row. A window that was not
 * maintained (extra_post_failures) or a peer that did not confirm the run
 * (final_failed) is not evidence, whatever the data said -- but the summary
 * prints data_ok= and final= separately so the two are never confused. */
static int recv_failed(int mismatches, int wc_errors, int repost_failures,
		       int extra_post_failures, int completed, int count,
		       int final_failed)
{
	return !recv_data_ok(mismatches, wc_errors, repost_failures, completed,
			     count) || extra_post_failures || final_failed;
}

/* ---------------------------- selftest ---------------------------------- */

static int st_fail;

static void st_check(int cond, const char *what)
{
	if (!cond) {
		st_fail++;
		printf("selftest FAIL: %s\n", what);
	}
}

/* A scripted record source for final_wait(): the same walk the socket loop
 * runs, fed from an array. `fail_at` injects a channel failure; running off
 * the end is the deadline. */
struct final_script {
	struct cr_record rec[8];
	int n;
	int pos;
	int fail_at;                    /* 1-based index, 0 = never */
	const struct credit_state *cs;
};

static void st_script(struct final_script *s, const struct credit_state *cs)
{
	memset(s, 0, sizeof(*s));
	s->cs = cs;
}

static void st_add(struct final_script *s, uint16_t type, uint32_t generation,
		   uint64_t aux, uint32_t status)
{
	struct cr_record *r;

	if (s->n >= (int)(sizeof(s->rec) / sizeof(s->rec[0])))
		return;
	r = &s->rec[s->n++];
	cr_record_init(r, type, s->cs);
	r->generation = generation;
	r->aux = aux;
	r->status = status;
}

static int final_next_script(void *vctx, struct cr_record *out)
{
	struct final_script *s = vctx;

	if (s->fail_at && s->pos + 1 == s->fail_at) {
		s->pos++;
		return -1;
	}
	if (s->pos >= s->n)
		return 0;               /* deadline */
	*out = s->rec[s->pos++];
	return 1;
}

static void st_grant(struct cr_record *r, const struct credit_state *cs,
		     uint64_t cum)
{
	cr_record_init(r, CR_TYPE_GRANT, cs);
	r->cum_grants = cum;
	r->cum_frames = cum * (uint64_t)cs->frames_per_msg;
}

/* Histogram math and the stall-trace gap rule, with no verbs and no clock. */
static void wrlat_selftest(void)
{
	struct wr_lat w;
	struct wr_trace t;

	/* bucket boundaries: 1 us linear below 64 us, four per octave above */
	st_check(wl_bucket(0) == 0, "wl: 0 ns -> bucket 0");
	st_check(wl_bucket(63999) == 63, "wl: 63.999 us -> bucket 63");
	st_check(wl_bucket(64000) == 64, "wl: 64 us -> first octave bucket");
	st_check(wl_bucket(127000) == 67, "wl: 127 us -> last sub-bucket of octave 0");
	st_check(wl_bucket(128000) == 68, "wl: 128 us -> octave 1");
	st_check(wl_bucket(8738000) == 64 + 7 * 4, "wl: 8738 us -> octave 7 sub 0");
	st_check(wl_bucket((WL_TOP_US - 1) * 1000) == WL_NBUCKETS - 2,
		 "wl: just under the top edge -> last real bucket");
	st_check(wl_bucket(WL_TOP_US * 1000) == WL_NBUCKETS - 1,
		 "wl: top edge -> overflow bucket");
	st_check(wl_bucket(UINT64_MAX) == WL_NBUCKETS - 1, "wl: huge -> overflow");

	/* upper edges are what a percentile reports */
	st_check(wl_bucket_upper_us(0) == 1, "wl: upper(0) == 1 us");
	st_check(wl_bucket_upper_us(63) == 64, "wl: upper(63) == 64 us");
	st_check(wl_bucket_upper_us(64) == 80, "wl: upper(64) == 80 us");
	st_check(wl_bucket_upper_us(67) == 128, "wl: upper(67) == 128 us");
	st_check(wl_bucket_upper_us(68) == 160, "wl: upper(68) == 160 us");
	st_check(wl_bucket_upper_us(64 + 7 * 4) == 10240,
		 "wl: upper(octave 7 sub 0) == 10240 us");
	st_check(wl_bucket_upper_us(WL_NBUCKETS - 1) == UINT64_MAX,
		 "wl: overflow has no upper edge");
	for (unsigned i = 0; i + 1 < WL_NBUCKETS - 1; i++)
		st_check(wl_bucket(wl_bucket_upper_us(i) * 1000) == i + 1 &&
			 wl_bucket((wl_bucket_upper_us(i) - 1) * 1000) == i,
			 "wl: every upper edge is the next bucket's lower edge");

	/* percentiles: 1..100 us once each */
	wl_init(&w, "t", 1);
	st_check(wl_percentile_us(&w, 50) == 0, "wl: empty histogram reports 0");
	for (uint64_t us = 1; us <= 100; us++)
		wl_record(&w, us * 1000);
	st_check(w.n == 100 && w.max_ns == 100000 && w.min_ns == 1000,
		 "wl: n/max/min after 100 samples");
	st_check(wl_percentile_us(&w, 50) == 51, "wl: p50 of 1..100 us -> 51 (bucket [50,51))");
	st_check(wl_percentile_us(&w, 90) == 96, "wl: p90 of 1..100 us -> 96 (bucket [80,96))");
	st_check(wl_percentile_us(&w, 99) == 100,
		 "wl: p99 of 1..100 us -> capped at the exact max, not the 112 edge");
	st_check(wl_percentile_us(&w, 100) == 100, "wl: p100 == max");

	/* one sample: every percentile is the exact max, not the bucket edge */
	wl_init(&w, "t", 1);
	wl_record(&w, 8738000);
	st_check(wl_percentile_us(&w, 50) == 8738 &&
		 wl_percentile_us(&w, 99) == 8738,
		 "wl: single 8738 us sample reports 8738, not 10240");

	/* overflow: the percentile falls back to the exact max */
	wl_init(&w, "t", 1);
	wl_record(&w, 40000000000ull);
	st_check(w.bucket[WL_NBUCKETS - 1] == 1, "wl: 40 s lands in overflow");
	st_check(wl_percentile_us(&w, 50) == 40000000,
		 "wl: overflow percentile reports the exact max");

	/* the gap rule: only intervals with a WR outstanding the whole time */
	memset(&t, 0, sizeof(t));
	t.gap_ns = 5000000;                 /* 5 ms, the default */
	wl_init(&w, "t", 1);
	wl_on_post(&w, 1000000000ull);
	st_check(w.outstanding == 1 && w.last_event == 1000000000ull,
		 "wl: first post opens the interval");
	wl_on_complete(&w, &t, 1000000000ull, 1006000000ull, 0);
	st_check(t.total == 1 && w.gaps == 1 && w.outstanding == 0,
		 "wl: 6 ms gap with a WR outstanding is traced");
	wl_on_post(&w, 2000000000ull);      /* idle for ~1 s: NOT a gap */
	wl_on_complete(&w, &t, 2000000000ull, 2001000000ull, 0);
	st_check(t.total == 1, "wl: an idle spell before a post is not a gap");
	wl_on_post(&w, 3000000000ull);
	wl_on_post(&w, 3000000000ull);
	wl_on_complete(&w, &t, 3000000000ull, 3002000000ull, 0);
	wl_on_complete(&w, &t, 3000000000ull, 3009000000ull, 0);
	st_check(t.total == 2 && w.gaps == 2 && w.outstanding == 0 &&
		 w.n == 4 && w.max_ns == 9000000,
		 "wl: 7 ms between two completions is traced; 2 ms is not");
	st_check(t.printed == 0, "wl: a NULL trace sink counts without printing");

	/* an ERROR completion that ends a stall is traced like any other
	 * (status=12 after 5.3 s, 2026-09-06, left no line before this) */
	wl_on_post(&w, 4000000000ull);
	wl_on_complete(&w, &t, 4000000000ull, 9300000000ull, 12);
	st_check(t.total == 3 && w.gaps == 3 && w.n == 5 &&
		 w.max_ns == 5300000000ull && w.outstanding == 0,
		 "wl: a 5.3 s gap ending in wc status 12 is traced and measured");

	/* an untraced kind still measures but never feeds the trace */
	memset(&t, 0, sizeof(t));
	t.gap_ns = 5000000;
	wl_init(&w, "t", 0);
	wl_on_post(&w, 0);
	wl_on_complete(&w, &t, 0, 50000000ull, 0);
	st_check(w.n == 1 && w.gaps == 0 && t.total == 0,
		 "wl: trace=0 kind records latency but no gaps");

	/* "still waiting": one mark per threshold multiple, only with WRs
	 * outstanding, reset by a completion, never for an untraced kind */
	memset(&t, 0, sizeof(t));
	t.gap_ns = 5000000;
	wl_init(&w, "t", 1);
	wl_poll_idle(&w, &t, 3000000ull);
	st_check(t.wait_total == 0, "wait: nothing outstanding -> no mark");
	wl_on_post(&w, 0);
	wl_poll_idle(&w, &t, 3000000ull);
	st_check(t.wait_total == 0, "wait: 3 ms under a 5 ms threshold -> no mark");
	wl_poll_idle(&w, &t, 6000000ull);
	st_check(t.wait_total == 1 && w.wait_marks == 1, "wait: 6 ms -> first mark");
	wl_poll_idle(&w, &t, 7000000ull);
	wl_poll_idle(&w, &t, 9999999ull);
	st_check(t.wait_total == 1, "wait: same multiple is reported once");
	wl_poll_idle(&w, &t, 12000000ull);
	st_check(t.wait_total == 2 && w.wait_marks == 2, "wait: 12 ms -> second mark");
	wl_poll_idle(&w, &t, 31000000ull);
	st_check(t.wait_total == 3 && w.wait_marks == 6,
		 "wait: a slow poll jumps to the current multiple in one line");
	wl_on_complete(&w, &t, 0, 32000000ull, 0);
	st_check(w.wait_marks == 0 && t.total == 1,
		 "wait: a completion resets the marks (and the 32 ms gap is traced)");
	wl_on_post(&w, 32000000ull);
	wl_poll_idle(&w, &t, 36000000ull);
	st_check(t.wait_total == 3, "wait: a fresh interval starts from zero");
	st_check(t.wait_printed == 0, "wait: NULL sink counts without printing");
	t.gap_ns = 0;
	wl_poll_idle(&w, &t, 99000000000ull);
	st_check(t.wait_total == 3, "wait: threshold 0 disables marks (no divide)");
	wl_init(&w, "t", 0);
	t.gap_ns = 5000000;
	wl_on_post(&w, 0);
	wl_poll_idle(&w, &t, 60000000ull);
	st_check(t.wait_total == 3, "wait: untraced kind never marks");

	/* outstanding list at the end of a short run: what the FIFO still
	 * holds after success and error completions have both left it */
	{
		struct sender_ctx sx;
		uint64_t ts[8], ids[8];

		memset(&sx, 0, sizeof(sx));
		sx.send_ts = ts;
		sx.send_ids = ids;
		sx.ring = 8;
		sx.post_seq = 10;
		sx.done_seq = 7;
		st_check(sender_print_outstanding(NULL, &sx, 10, 0) == 3,
			 "outstanding: 10 posted, 7 left the FIFO -> 3 never completed");
		st_check((sx.done_seq % sx.ring) == 7 &&
			 ((sx.done_seq + 1) % sx.ring) == 0 &&
			 ((sx.done_seq + 2) % sx.ring) == 1,
			 "outstanding: ring wraps 7,0,1");
		sx.done_seq = 10;
		st_check(sender_print_outstanding(NULL, &sx, 10, 0) == 0,
			 "outstanding: fully completed run owes nothing");
	}
}

static int credit_selftest(void)
{
	struct credit_state cs;
	struct cr_record r, d;
	uint8_t buf[CR_WIRE_LEN];

	st_fail = 0;
	wrlat_selftest();

	/* frames per message: Apple counts 4 KiB frames, not messages */
	st_check(cr_frames_for(4096) == 1, "frames_for(4096) == 1");
	st_check(cr_frames_for(4097) == 2, "frames_for(4097) == 2");
	st_check(cr_frames_for(65536) == 16, "frames_for(64 KiB) == 16");
	st_check(cr_frames_for(1048576) == 256, "frames_for(1 MiB) == 256");

	/* encode/decode round trip */
	memset(&cs, 0, sizeof(cs));
	cs.generation = 0xdeadbeefu;
	cs.msg_size = 65536;
	cs.frames_per_msg = 16;
	cs.granted = 60;
	cs.frames_granted = 960;
	cr_record_init(&r, CR_TYPE_READY, &cs);
	r.recv_requested = 512;
	r.recv_accepted = 60;
	cr_encode(&r, buf);
	st_check(cr_decode(buf, &d) == CR_OK, "decode round trip ok");
	st_check(d.generation == r.generation && d.msg_size == r.msg_size &&
		 d.cum_grants == r.cum_grants && d.cum_frames == r.cum_frames &&
		 d.recv_requested == 512 && d.recv_accepted == 60 &&
		 d.type == CR_TYPE_READY,
		 "decode round trip fields");

	/* malformed records */
	cr_encode(&r, buf);
	buf[0] ^= 0xffu;
	st_check(cr_decode(buf, &d) == CR_ERR_MAGIC, "bad magic rejected");
	cr_encode(&r, buf);
	buf[4] = 99;
	st_check(cr_decode(buf, &d) == CR_ERR_VERSION, "bad version rejected");
	cr_encode(&r, buf);
	buf[6] = 77;
	st_check(cr_decode(buf, &d) == CR_ERR_TYPE, "bad type rejected");

	/* READY acceptance and unit checks */
	memset(&cs, 0, sizeof(cs));
	st_check(credit_accept_ready(&cs, &r, 65536) == CR_OK,
		 "READY accepted");
	st_check(cs.granted == 60 && cs.frames_granted == 960 &&
		 credit_available(&cs) == 60,
		 "READY seeds 60 grants / 960 frames");
	memset(&cs, 0, sizeof(cs));
	st_check(credit_accept_ready(&cs, &r, 4096) == CR_ERR_MSG_SIZE,
		 "READY with wrong message size refused");

	/* a READY whose frame total does not match its message count is a
	 * units bug on the wire, not capacity */
	memset(&cs, 0, sizeof(cs));
	d = r;
	d.cum_frames = 60;
	st_check(credit_accept_ready(&cs, &d, 65536) == CR_ERR_FRAMES,
		 "READY with frames==messages refused");

	/* --count below --recv-posts: the receiver posts its whole window but
	 * the cap holds grants at count. READY must be accepted with
	 * grants < accepted (the 2026-09-06 stale probe, count 1 vs 60, was
	 * refused as a frame mismatch and the run never started). */
	memset(&cs, 0, sizeof(cs));
	d = r;                          /* recv_accepted = 60 */
	d.cum_grants = 1;
	d.cum_frames = 16;
	st_check(credit_accept_ready(&cs, &d, 65536) == CR_OK,
		 "READY count 1 vs recv-posts 60 accepted");
	st_check(cs.granted == 1 && cs.frames_granted == 16 &&
		 credit_available(&cs) == 1,
		 "READY 1/60 seeds exactly one grant / 16 frames");
	st_check(credit_reserve(&cs) == 1 && credit_reserve(&cs) == 0,
		 "READY 1/60: the sender can send exactly one message");
	memset(&cs, 0, sizeof(cs));
	d = r;
	d.recv_accepted = 8;
	d.cum_grants = 5;
	d.cum_frames = 80;
	st_check(credit_accept_ready(&cs, &d, 65536) == CR_OK &&
		 cs.granted == 5 && cs.frames_granted == 80,
		 "READY count 5 vs recv-posts 8 accepted with 5 grants");
	memset(&cs, 0, sizeof(cs));
	d = r;
	d.cum_grants = 61;              /* more grants than posted receives */
	d.cum_frames = 61 * 16;
	st_check(credit_accept_ready(&cs, &d, 65536) == CR_ERR_FRAMES,
		 "READY with grants beyond the posted window refused (overrun)");
	memset(&cs, 0, sizeof(cs));
	d = r;
	d.cum_grants = 1;
	d.cum_frames = 960;             /* frames of the window, not the grants */
	st_check(credit_accept_ready(&cs, &d, 65536) == CR_ERR_FRAMES,
		 "READY 1/60 with the window's frame total refused");

	/* grants: monotonic, generation-scoped, duplicate-safe */
	memset(&cs, 0, sizeof(cs));
	st_check(credit_accept_ready(&cs, &r, 65536) == CR_OK, "re-ready");
	st_grant(&d, &cs, 70);
	st_check(credit_apply_grant(&cs, &d) == CR_OK, "grant 70 accepted");
	st_check(credit_available(&cs) == 70, "available == 70");
	st_check(credit_apply_grant(&cs, &d) == CR_DUPLICATE,
		 "duplicate grant reported");
	st_check(credit_available(&cs) == 70,
		 "duplicate grant adds no capacity");
	st_grant(&d, &cs, 65);
	st_check(credit_apply_grant(&cs, &d) == CR_ERR_DECREASING,
		 "decreasing grant refused");
	st_check(credit_available(&cs) == 70,
		 "decreasing grant leaves ledger intact");
	st_grant(&d, &cs, 80);
	d.generation ^= 0x1u;
	st_check(credit_apply_grant(&cs, &d) == CR_ERR_GENERATION,
		 "wrong-generation grant refused");
	st_check(credit_available(&cs) == 70,
		 "wrong-generation grant adds no capacity");
	st_grant(&d, &cs, 80);
	d.cum_frames = 80; /* messages passed off as frames */
	st_check(credit_apply_grant(&cs, &d) == CR_ERR_FRAMES,
		 "grant with mismatched frame total refused");

	/* reservation accounting */
	memset(&cs, 0, sizeof(cs));
	cs.generation = 1;
	cs.msg_size = 4096;
	cs.frames_per_msg = 1;
	st_check(credit_reserve(&cs) == 0, "no reserve before READY");
	cs.ready = 1;
	st_check(credit_reserve(&cs) == 0, "no reserve with zero grants");
	credit_grant_one(&cs);
	credit_grant_one(&cs);
	st_check(cs.granted == 2 && cs.frames_granted == 2,
		 "two grants minted by successful posts");
	st_check(credit_reserve(&cs) == 1 && credit_reserve(&cs) == 1,
		 "two reservations succeed");
	st_check(credit_reserve(&cs) == 0, "third reservation refused");
	credit_unreserve(&cs);
	st_check(credit_reserve(&cs) == 1, "unreserve returns capacity");
	cs.aborted = 1;
	credit_grant_one(&cs);
	st_check(credit_reserve(&cs) == 0, "abort stops reservations");

	/* the receive-capacity guard: G <= min(N, max(0, P - k)) */
	st_check(guard_ceiling(60, 4, 2000, 1) == 56,
		 "guard: P=60 k=4 N=2000 -> 56 initial grants");
	st_check(guard_ceiling(61, 4, 2000, 1) == 57 &&
		 guard_ceiling(100, 4, 2000, 1) == 96,
		 "guard: every further successful post releases one grant");
	st_check(guard_ceiling(2004, 4, 2000, 1) == 2000 &&
		 guard_ceiling(9999, 4, 2000, 1) == 2000,
		 "guard: N caps the ceiling");
	st_check(guard_ceiling(3, 4, 2000, 1) == 0 &&
		 guard_ceiling(4, 4, 2000, 1) == 0 &&
		 guard_ceiling(0, 4, 2000, 1) == 0,
		 "guard: P <= k grants nothing (no underflow)");
	st_check(guard_ceiling(60, 0, 2000, 1) == 60,
		 "guard: k=0 is the pre-guard ledger");
	st_check(guard_ceiling(60, 4, 1, 1) == 1 && guard_ceiling(8, 4, 5, 1) == 4,
		 "guard: count below the window is still capped at count");
	st_check(guard_ceiling(1999, 4, 2000, 0) == 1995 &&
		 guard_ceiling(2000, 4, 2000, 0) == 2000 &&
		 guard_ceiling(2000, 4, 2000, 1) == 1996 &&
		 guard_ceiling(2004, 4, 2000, 1) == 2000,
		 "guard: decay releases at P>=N; keep-posted needs k extras for the last k grants");

	memset(&cs, 0, sizeof(cs));
	cs.generation = 1;
	cs.msg_size = 65536;
	cs.frames_per_msg = 16;
	cs.ready = 1;
	st_check(credit_grant_to_ceiling(&cs, 56) == 56 && cs.granted == 56 &&
		 cs.frames_granted == 896,
		 "guard: mint to the initial ceiling 56 / 896 frames");
	st_check(credit_grant_to_ceiling(&cs, 56) == 0 && cs.granted == 56,
		 "guard: the same ceiling mints nothing");
	st_check(credit_grant_to_ceiling(&cs, 57) == 1 && cs.granted == 57,
		 "guard: P grows by one -> exactly one more grant");
	st_check(credit_grant_to_ceiling(&cs, 10) == 0 && cs.granted == 57,
		 "guard: a lower ceiling never rolls the ledger back");
	st_check(credit_grant_to_ceiling(&cs, 2000) == 1943 && cs.granted == 2000 &&
		 credit_grant_to_ceiling(&cs, 2000) == 0,
		 "guard: mint to N, then N caps");

	/* admission closed before FINAL: grants in hand, no reservation */
	memset(&cs, 0, sizeof(cs));
	cs.ready = 1;
	cs.frames_per_msg = 1;
	credit_grant_one(&cs);
	credit_grant_one(&cs);
	st_check(credit_reserve(&cs) == 1, "close: reserve allowed while open");
	credit_close(&cs);
	st_check(credit_reserve(&cs) == 0 && credit_available(&cs) == 1,
		 "close: closed admission refuses with capacity still granted");

	/* strict FINAL acceptance */
	{
		struct credit_state g;
		struct cr_record f;
		const char *why = NULL;

		memset(&g, 0, sizeof(g));
		g.generation = 0x5a5a1234u;
		cr_record_init(&f, CR_TYPE_FINAL, &g);
		f.aux = 2000;
		f.status = 0;
		st_check(final_validate(&f, g.generation, 2000, &why) == 0,
			 "final: FINAL/generation/count/status 0 accepted");
		f.aux = 1999;
		st_check(final_validate(&f, g.generation, 2000, &why) < 0 &&
			 strstr(why, "completed"),
			 "final: completed 1999 != 2000 rejected");
		f.aux = 2000;
		st_check(final_validate(&f, g.generation ^ 1u, 2000, &why) < 0 &&
			 strstr(why, "generation"),
			 "final: wrong generation rejected");
		f.status = 1;
		st_check(final_validate(&f, g.generation, 2000, &why) < 0 &&
			 strstr(why, "status"),
			 "final: peer status 1 rejected");
		cr_record_init(&f, CR_TYPE_GRANT, &g);
		f.aux = 2000;
		st_check(final_validate(&f, g.generation, 2000, &why) < 0,
			 "final: a non-FINAL record is not a FINAL");
		cr_record_init(&f, CR_TYPE_ABORT, &g);
		f.aux = 2000;
		st_check(final_validate(&f, g.generation, 2000, &why) < 0 &&
			 strstr(why, "ABORT"),
			 "final: ABORT is named, not accepted");
	}

	/*
	 * The FINAL wait tolerates late control records. On 2026-09-06 a
	 * RESEND read first failed a run that transferred 2000/2000
	 * bit-perfect, and the valid FINAL arrived later in the same log.
	 */
	{
		struct credit_state g;
		struct final_script sc;
		const char *why = NULL;
		uint64_t ignored;

		memset(&g, 0, sizeof(g));
		g.generation = 0xeae4ca50u;

		/* RESEND (completed=1509, the real log) then a valid FINAL */
		st_script(&sc, &g);
		st_add(&sc, CR_TYPE_RESEND, g.generation, 1509, 0);
		st_add(&sc, CR_TYPE_FINAL, g.generation, 2000, 0);
		ignored = 0;
		st_check(final_wait(final_next_script, &sc, NULL, g.generation,
				    2000, &ignored, &why) == 0 &&
			 ignored == 1 && strstr(why, "valid"),
			 "final wait: RESEND first, then FINAL -> accepted");

		/* every non-terminal type is ignored, in any order */
		st_script(&sc, &g);
		st_add(&sc, CR_TYPE_GRANT, g.generation, 0, 0);
		st_add(&sc, CR_TYPE_CTLWIN, g.generation, 0, 0);
		st_add(&sc, CR_TYPE_RESEND, g.generation, 1509, 0);
		st_add(&sc, CR_TYPE_GRANT, g.generation, 0, 0);
		st_add(&sc, CR_TYPE_FINAL, g.generation, 2000, 0);
		ignored = 0;
		st_check(final_wait(final_next_script, &sc, NULL, g.generation,
				    2000, &ignored, &why) == 0 && ignored == 4,
			 "final wait: GRANT/CTLWIN/RESEND all ignored, FINAL accepted");

		/* RESEND then nothing: the deadline is still a failure */
		st_script(&sc, &g);
		st_add(&sc, CR_TYPE_RESEND, g.generation, 1509, 0);
		ignored = 0;
		st_check(final_wait(final_next_script, &sc, NULL, g.generation,
				    2000, &ignored, &why) < 0 && ignored == 1 &&
			 strstr(why, "no peer FINAL"),
			 "final wait: RESEND then deadline -> FAIL, deadline reason kept");

		/* a bad FINAL behind late records still fails, with ITS reason */
		st_script(&sc, &g);
		st_add(&sc, CR_TYPE_RESEND, g.generation, 1509, 0);
		st_add(&sc, CR_TYPE_FINAL, g.generation, 1999, 0);
		ignored = 0;
		st_check(final_wait(final_next_script, &sc, NULL, g.generation,
				    2000, &ignored, &why) < 0 &&
			 strstr(why, "completed"),
			 "final wait: a short FINAL behind a RESEND still fails on the count");

		/* ABORT ends the wait even behind late records */
		st_script(&sc, &g);
		st_add(&sc, CR_TYPE_GRANT, g.generation, 0, 0);
		st_add(&sc, CR_TYPE_ABORT, g.generation, 0, 1);
		st_add(&sc, CR_TYPE_FINAL, g.generation, 2000, 0);
		ignored = 0;
		st_check(final_wait(final_next_script, &sc, NULL, g.generation,
				    2000, &ignored, &why) < 0 &&
			 strstr(why, "ABORT") && sc.pos == 2,
			 "final wait: ABORT is terminal, the later FINAL is not read");

		/* a channel failure is reported as such */
		st_script(&sc, &g);
		sc.fail_at = 1;
		st_add(&sc, CR_TYPE_RESEND, g.generation, 1509, 0);
		ignored = 0;
		st_check(final_wait(final_next_script, &sc, NULL, g.generation,
				    2000, &ignored, &why) < 0 &&
			 strstr(why, "channel failure"),
			 "final wait: a channel failure keeps its own reason");
	}

	/* data health is independent of the control plane */
	st_check(recv_data_ok(0, 0, 0, 2000, 2000) == 1,
		 "data_ok: clean complete run");
	st_check(recv_data_ok(1, 0, 0, 2000, 2000) == 0 &&
		 recv_data_ok(0, 1, 0, 2000, 2000) == 0 &&
		 recv_data_ok(0, 0, 1, 2000, 2000) == 0 &&
		 recv_data_ok(0, 0, 0, 1999, 2000) == 0,
		 "data_ok: mismatch/wc error/repost failure/short run are data failures");
	st_check(recv_data_ok(0, 0, 0, 2000, 2000) == 1 &&
		 recv_failed(0, 0, 0, 0, 2000, 2000, 1) != 0,
		 "data_ok: a control-plane hiccup fails the row with data_ok still yes");

	/* the one definition of a failed receive row */
	st_check(recv_failed(0, 0, 0, 0, 2000, 2000, 0) == 0,
		 "row: clean, complete, confirmed -> OK");
	st_check(recv_failed(0, 0, 0, 1, 2000, 2000, 0) != 0,
		 "row: an extra keep-posted post failure FAILS the row");
	st_check(recv_failed(0, 0, 0, 0, 2000, 2000, 1) != 0,
		 "row: a missing or mismatched FINAL FAILS the row");
	st_check(recv_failed(0, 0, 0, 0, 1999, 2000, 0) != 0,
		 "row: a short run FAILS");

	/* receive window policy resolution: default ON for a credited
	 * receiver, OFF without credits, opt-out honoured, sender never */
	{
		struct opts po;
		char *a_cred[] = { "t", "--role", "recv", "--dev", "d", "--credits" };
		char *a_plain[] = { "t", "--role", "recv", "--dev", "d" };
		char *a_optout[] = { "t", "--role", "recv", "--dev", "d",
				     "--credits=uc", "--recv-no-keep-posted" };
		char *a_explicit[] = { "t", "--role", "recv", "--dev", "d",
				       "--recv-keep-posted" };
		char *a_send[] = { "t", "--role", "send", "--dev", "d", "--credits" };

		st_check(parse_opts(6, a_cred, &po) == 0 && po.recv_keep_posted == 1,
			 "policy: credited receiver keeps its window by default");
		st_check(parse_opts(5, a_plain, &po) == 0 && po.recv_keep_posted == 0,
			 "policy: uncredited receiver unchanged (decay)");
		st_check(parse_opts(7, a_optout, &po) == 0 &&
			 po.recv_keep_posted == 0 && po.credits,
			 "policy: --recv-no-keep-posted is the control arm");
		st_check(parse_opts(6, a_explicit, &po) == 0 &&
			 po.recv_keep_posted == 1 && !po.credits,
			 "policy: explicit --recv-keep-posted works without credits");
		st_check(parse_opts(6, a_send, &po) == 0 && po.recv_keep_posted == 0,
			 "policy: a sender never keeps a receive window");
	}

	/* abort record folds into the ledger as a terminal state */
	memset(&cs, 0, sizeof(cs));
	st_check(credit_accept_ready(&cs, &r, 65536) == CR_OK, "ready again");
	cr_record_init(&d, CR_TYPE_ABORT, &cs);
	st_check(credit_apply_grant(&cs, &d) == CR_ABORTED, "abort applied");
	st_check(cs.aborted == 1 && credit_reserve(&cs) == 0,
		 "aborted state refuses reservations");

	/* ---- control-RQ accounting (--credits=uc) ----
	 *
	 * The grant channel is itself UC: the receiver may never have more
	 * control SENDs outstanding than the sender has posted control
	 * receives. The bootstrap and every replenishment are cumulative. */
	{
		struct ctl_credit c;

		memset(&c, 0, sizeof(c));
		st_check(ctl_credit_avail(&c) == 0,
			 "ctl: no control sends before CTLWIN");
		st_check(ctl_credit_take(&c) == 0,
			 "ctl: take refused with an empty window");

		st_check(ctl_credit_apply(&c, 4) == 1, "ctl: CTLWIN 4 accepted");
		st_check(ctl_credit_avail(&c) == 4, "ctl: window is 4");
		st_check(ctl_credit_take(&c) && ctl_credit_take(&c) &&
			 ctl_credit_take(&c) && ctl_credit_take(&c),
			 "ctl: four control sends allowed");
		st_check(ctl_credit_take(&c) == 0,
			 "ctl: fifth control send refused (window exhausted)");
		st_check(c.used == 4, "ctl: used == 4 after exhaustion");

		/* A duplicate or reordered CTLWIN adds nothing; it must never
		 * roll the window back either. */
		st_check(ctl_credit_apply(&c, 4) == 0,
			 "ctl: duplicate CTLWIN adds no capacity");
		st_check(ctl_credit_apply(&c, 2) == 0,
			 "ctl: stale CTLWIN adds no capacity");
		st_check(c.granted == 4, "ctl: stale CTLWIN did not roll back");
		st_check(ctl_credit_take(&c) == 0,
			 "ctl: still refused after duplicate/stale");

		/* Replenishment as the sender's control receives complete. */
		st_check(ctl_credit_apply(&c, 9) == 1, "ctl: CTLWIN 9 accepted");
		st_check(ctl_credit_avail(&c) == 5,
			 "ctl: five more control sends released");

		/* A LOST CTLWIN is recovered by the next one: 9 -> (12 lost)
		 * -> 20 leaves exactly the same window as 9 -> 12 -> 20. */
		st_check(ctl_credit_apply(&c, 20) == 1,
			 "ctl: CTLWIN 20 after a lost 12 accepted");
		st_check(ctl_credit_avail(&c) == 16,
			 "ctl: lost CTLWIN cost nothing (window == 20 - 4)");
	}

	/* ---- lost-record recovery on the grant ledger ----
	 *
	 * Cumulative totals mean a dropped GRANT is recovered by the next one,
	 * and a REORDERED one is stale rather than a protocol violation --
	 * but only on a transport that can actually reorder. */
	{
		struct credit_state u;
		struct cr_record g;

		memset(&u, 0, sizeof(u));
		u.stale_ok = 1;                 /* UC transport */
		st_check(credit_accept_ready(&u, &r, 65536) == CR_OK,
			 "loss: READY accepted (60 grants)");

		/* Drop the grants for 61..69 entirely: the 70 record carries
		 * every one of them. */
		st_grant(&g, &u, 70);
		st_check(credit_apply_grant(&u, &g) == CR_OK,
			 "loss: cumulative 70 accepted after 61..69 were lost");
		st_check(credit_available(&u) == 70,
			 "loss: all ten missing grants recovered by one record");

		/* A reordered older record arrives late. */
		st_grant(&g, &u, 65);
		st_check(credit_apply_grant(&u, &g) == CR_STALE,
			 "loss: reordered older total reported stale on UC");
		st_check(credit_available(&u) == 70 && u.granted == 70,
			 "loss: stale record did not roll the ledger back");

		/* The same record on TCP is a genuine protocol error: TCP
		 * cannot reorder, so a decrease means the peer is broken. */
		{
			struct credit_state t;

			memset(&t, 0, sizeof(t));
			st_check(credit_accept_ready(&t, &r, 65536) == CR_OK,
				 "loss: TCP ledger ready");
			st_grant(&g, &t, 70);
			st_check(credit_apply_grant(&t, &g) == CR_OK,
				 "loss: TCP grant 70 accepted");
			st_grant(&g, &t, 65);
			st_check(credit_apply_grant(&t, &g) == CR_ERR_DECREASING,
				 "loss: decreasing total is still an error on TCP");
		}

		/* Loss can only ever cost the sender capacity, never grant it
		 * more: reservations stay bounded by the largest total seen. */
		for (int i = 0; i < 70; i++)
			st_check(credit_reserve(&u) == 1, "loss: reserve 70");
		st_check(credit_reserve(&u) == 0,
			 "loss: reservations bounded by the observed total");
	}

	/* ---- bounded, coalescing control output ----
	 *
	 * A newer cumulative snapshot REPLACES the pending one, so the queue
	 * can never grow past one in-flight record plus one snapshot. */
	{
		struct cr_out ob;
		struct cr_out_done d = { 0, 0, 0 };
		struct credit_state q;

		memset(&q, 0, sizeof(q));
		q.msg_size = 65536;
		q.frames_per_msg = 16;
		q.granted = 10;
		q.frames_granted = 160;

		cr_out_init(&ob, -1);           /* fd -1: writes always fail */
		st_check(cr_out_idle(&ob), "out: starts idle");
		cr_record_init(&r, CR_TYPE_GRANT, &q);
		st_check(cr_out_submit(&ob, &r, 1, &d) < 0,
			 "out: a failing socket is reported, not swallowed");
		st_check(!cr_out_idle(&ob),
			 "out: the unwritten record is retained, not dropped");
		q.granted = 11;
		cr_record_init(&r, CR_TYPE_GRANT, &q);
		(void)cr_out_submit(&ob, &r, 2, &d);
		q.granted = 12;
		cr_record_init(&r, CR_TYPE_GRANT, &q);
		(void)cr_out_submit(&ob, &r, 3, &d);
		st_check(ob.part_len <= CR_WIRE_LEN && ob.pending <= 1,
			 "out: queue bounded at one record plus one snapshot");
		st_check(ob.pend_total == 12 || ob.part_total == 12,
			 "out: the pending snapshot is the LATEST total");
		st_check(ob.pend_since == 2 || ob.part_since == 1,
			 "out: coalescing keeps the OLDEST mint time");
	}

	printf("selftest: %s (%d failure%s)\n", st_fail ? "FAIL" : "OK",
	       st_fail, st_fail == 1 ? "" : "s");
	return st_fail ? 1 : 0;
}

int main(int argc, char **argv)
{
	struct opts o;
	struct ibv_context *ctx;
	struct ibv_port_attr port_attr;
	union ibv_gid local_gid, remote_gid;
	struct ibv_pd *pd;
	struct ibv_cq *cq;
	struct ibv_qp *qp;
	struct ibv_qp_init_attr qpia;
	struct peer_info local, remote;
	int is_sender;
	int is_bidi;
	int sock = -1, listen_fd = -1;
	int ret = 1;
	uint32_t psn;
	size_t page_size, stride, bufsz, send_region_size = 0;
	char *buf = NULL;
	char *send_buf = NULL;
	char *recv_buf = NULL;
	struct ibv_mr *mr = NULL;
	uint64_t start, last_t;
	int last_done = 0;
	int initial_recvs = 0;
	int requested_recvs = 0;
	int posted_initial_recvs = 0;
	int wr_depth;
	int send_depth;
	int send_slots;
	int sgid_index;
	uint8_t *seen = NULL;
	struct credit_state cs;
	struct cr_chan chan;
	struct cr_out out;
	struct grant_pub gp;
	struct ctl_credit cc;
	struct ctl_ring ctl;
	char *ctl_buf = NULL;
	int use_uc_credits;
	int ctl_recv_slots = 0, ctl_send_slots = 0;
	uint32_t frames_per_msg;
	uint64_t final_completed = 0;
	uint32_t final_status = 0;
	const char *early_reason = "aborted before the data phase";
	int recv_summary_printed = 0;
	struct wr_lat lat_data, lat_ctl;
	struct wr_trace tr;
	uint64_t *data_ts = NULL;   /* sender: FIFO ring; receiver: per slot */
	uint64_t *data_ids = NULL;  /* sender only: wr_id beside each stamp */
	uint64_t rx_posted_total = 0;   /* P: successful data receive posts */
	int final_done = 0;         /* strict FINAL exchange already performed */

	memset(&cs, 0, sizeof(cs));
	memset(&tr, 0, sizeof(tr));
	memset(&out, 0, sizeof(out));
	memset(&gp, 0, sizeof(gp));
	memset(&cc, 0, sizeof(cc));
	memset(&ctl, 0, sizeof(ctl));
	cr_chan_init(&chan, -1);

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--selftest"))
			return credit_selftest();
	}

	if (parse_opts(argc, argv, &o)) {
		usage(argv[0]);
		return 2;
	}
	if (load_verbs_provider())
		return 1;
	is_sender = !strcmp(o.role, "send");
	is_bidi = !strcmp(o.role, "bidi");
	setvbuf(stdout, NULL, _IOLBF, 0);
	if (o.size > 0xffffffffu) {
		fprintf(stderr, "--size exceeds 32-bit message size\n");
		return 2;
	}
	frames_per_msg = cr_frames_for((uint32_t)o.size);
	cs.msg_size = (uint32_t)o.size;
	cs.frames_per_msg = frames_per_msg;
	use_uc_credits = o.credits && o.credit_xport == CR_XPORT_UC && !is_bidi;
	/* On UC a reordered cumulative total is stale, not a violation. */
	cs.stale_ok = use_uc_credits;
	if (use_uc_credits) {
		/* The sender owns the control RECEIVE queue; the receiver owns
		 * the control SEND slots. Each side allocates only its half. */
		ctl_recv_slots = is_sender ? o.ctl_depth : 0;
		ctl_send_slots = is_sender ? 0 : o.ctl_send_slots;
	}
	/* The receiver owns the generation: it is the side that mints grants,
	 * so a stale sender cannot resurrect a previous run's ledger. */
	cs.generation = (uint32_t)(now_ns() ^ ((uint64_t)getpid() << 32) ^
				   ((uint64_t)getpid() << 3));
	if (!is_sender || is_bidi) {
		initial_recvs = o.recv_posts ? o.recv_posts : o.depth;
		if (initial_recvs > o.count && !o.recv_posts)
			initial_recvs = o.count;
	}
	wr_depth = o.depth;
	send_depth = o.depth;
	if (!is_sender && initial_recvs > wr_depth)
		wr_depth = initial_recvs;
	send_slots = (is_sender || is_bidi) ?
		     (o.send_slots ? o.send_slots : o.depth) : 0;
	if (send_slots < 0 || send_slots > o.count)
		send_slots = o.count;
	if ((is_sender || is_bidi) && o.check && send_slots < o.depth) {
		fprintf(stderr,
			"uc_oneway: --check requires --send-slots >= --depth; "
			"otherwise in-flight SEND buffers may be overwritten "
			"(send_slots=%d depth=%d)\n",
			send_slots, o.depth);
		return 2;
	}

	/* Per-WR latency state. The sender's ring is --depth deep because
	 * in_flight never exceeds send_depth <= --depth; the receiver indexes
	 * by slot. The grant inter-arrival gaps on the sender's control RQ are
	 * the receiver's schedule, not a TX stall, so that kind is not traced.
	 * --role bidi is not instrumented. */
	wl_init(&lat_data, is_sender ? "data-send" : "data-recv-dwell", 1);
	wl_init(&lat_ctl, is_sender ? "ctl-recv-dwell" : "ctl-send", !is_sender);
	tr.out = stderr;
	tr.role = o.role;
	tr.cs = &cs;
	tr.gap_ns = (uint64_t)o.wr_gap_us * 1000ull;
	tr.t0 = now_ns();
	if (!is_bidi) {
		data_ts = calloc((size_t)(is_sender ? o.depth : wr_depth),
				 sizeof(*data_ts));
		if (is_sender)
			data_ids = calloc((size_t)o.depth, sizeof(*data_ids));
		if (!data_ts || (is_sender && !data_ids)) {
			perror("calloc wr timestamps");
			return 1;
		}
	}

	debug_step("open device");
	ctx = open_dev(o.dev);
	if (!ctx) {
		fprintf(stderr, "failed to open RDMA device %s\n", o.dev);
		return 1;
	}
	debug_step("query port");
	if (ibv_query_port(ctx, (uint8_t)o.ib_port, &port_attr)) {
		perror("ibv_query_port");
		goto out_ctx;
	}
	debug_step("query gid");
	if (select_gid(ctx, o.ib_port, o.gid_index, port_attr.gid_tbl_len,
		       &local_gid, &sgid_index))
		goto out_ctx;
	fprintf(stderr, "selected sgid_index=%d local_gid=", sgid_index);
	print_gid(stderr, &local_gid);
	fprintf(stderr, "\n");

	debug_step("alloc pd");
	pd = ibv_alloc_pd(ctx);
	if (!pd) {
		perror("ibv_alloc_pd");
		goto out_ctx;
	}

	long sys_page_size = sysconf(_SC_PAGESIZE);
	if (sys_page_size <= 0) {
		perror("sysconf(_SC_PAGESIZE)");
		goto out_pd;
	}
	page_size = (size_t)sys_page_size;
	stride = align_up(o.size, page_size);
	if (is_bidi) {
		send_region_size = stride * (size_t)send_slots;
		bufsz = align_up(send_region_size + stride * (size_t)wr_depth,
				 page_size);
	} else if (is_sender) {
		bufsz = align_up(stride * (size_t)send_slots, page_size);
	} else {
		bufsz = align_up(stride * (size_t)wr_depth, page_size);
	}
	if (posix_memalign((void **)&buf, page_size, bufsz)) {
		fprintf(stderr, "posix_memalign failed\n");
		goto out_pd;
	}
	memset(buf, is_sender ? 0x5a : 0xcc, bufsz);
	send_buf = buf;
	recv_buf = is_bidi ? buf + send_region_size : buf;
	debug_step("register mr");
	int mr_access = getenv("UC_ONEWAY_TN3205") ?
			IBV_ACCESS_LOCAL_WRITE :
			(IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ |
			 IBV_ACCESS_REMOTE_WRITE);
	fprintf(stderr, "register mr len=%zu access=0x%x\n", bufsz,
		mr_access);
	mr = ibv_reg_mr(pd, buf, bufsz, mr_access);
	if (!mr) {
		perror("ibv_reg_mr");
		goto out_buf;
	}

	if (use_uc_credits) {
		/* SEPARATE registered 4 KiB buffers: the grant channel never
		 * borrows an application buffer or an application WQE. */
		size_t ctl_bytes = (size_t)(ctl_recv_slots + ctl_send_slots) *
				   CR_FRAME_BYTES;

		if (posix_memalign((void **)&ctl_buf, page_size,
				   align_up(ctl_bytes, page_size))) {
			fprintf(stderr, "posix_memalign (control ring) failed\n");
			goto out_mr;
		}
		memset(ctl_buf, 0, align_up(ctl_bytes, page_size));
		ctl.buf = ctl_buf;
		ctl.n_recv = ctl_recv_slots;
		ctl.n_send = ctl_send_slots;
		ctl.lat = &lat_ctl;
		if (ctl.n_send) {
			ctl.send_busy = calloc((size_t)ctl.n_send, 1);
			ctl.send_ts = calloc((size_t)ctl.n_send,
					     sizeof(*ctl.send_ts));
			if (!ctl.send_busy || !ctl.send_ts) {
				perror("calloc control send slots");
				goto out_mr;
			}
		}
		if (ctl.n_recv) {
			ctl.recv_ts = calloc((size_t)ctl.n_recv,
					     sizeof(*ctl.recv_ts));
			if (!ctl.recv_ts) {
				perror("calloc control recv slots");
				goto out_mr;
			}
		}
		ctl.mr = ibv_reg_mr(pd, ctl_buf, align_up(ctl_bytes, page_size),
				    mr_access);
		if (!ctl.mr) {
			perror("ibv_reg_mr (control ring)");
			goto out_mr;
		}
		fprintf(stderr,
			"credits: on-link control ring recv_slots=%d send_slots=%d frame=%u bytes=%zu\n",
			ctl.n_recv, ctl.n_send, CR_FRAME_BYTES, ctl_bytes);
		fflush(stderr);
	}

	if (o.check_any_order && (!is_sender || is_bidi)) {
		seen = calloc(((size_t)o.count + 7u) / 8u, 1);
		if (!seen) {
			perror("calloc seen");
			goto out_mr;
		}
	}

	debug_step("create cq");
	cq = ibv_create_cq(ctx, wr_depth + ctl_recv_slots + ctl_send_slots + 16,
			   NULL, NULL, 0);
	if (!cq) {
		perror("ibv_create_cq");
		goto out_mr;
	}

	debug_step("create qp");
	memset(&qpia, 0, sizeof(qpia));
	qpia.qp_context = ctx;
	qpia.send_cq = cq;
	qpia.recv_cq = cq;
	/* The on-link grant channel rides the reverse direction of THIS QP, so
	 * the otherwise-unused queue must be sized for it. */
	qpia.cap.max_send_wr = (is_sender || is_bidi) ? (uint32_t)send_depth :
			       (ctl_send_slots ? (uint32_t)ctl_send_slots : 1);
	qpia.cap.max_recv_wr = (!is_sender || is_bidi) ? (uint32_t)wr_depth :
			       (ctl_recv_slots ? (uint32_t)ctl_recv_slots : 1);
	qpia.cap.max_send_sge = 1;
	qpia.cap.max_recv_sge = 1;
	qpia.qp_type = IBV_QPT_UC;
	qp = ibv_create_qp(pd, &qpia);
	if (!qp) {
		perror("ibv_create_qp");
		goto out_cq;
	}
	fprintf(stderr,
		"created QP qp_num=%u actual_cap send_wr=%u recv_wr=%u send_sge=%u recv_sge=%u\n",
		qp->qp_num, qpia.cap.max_send_wr, qpia.cap.max_recv_wr,
		qpia.cap.max_send_sge, qpia.cap.max_recv_sge);
	fflush(stderr);
	if ((is_sender || is_bidi) && !qpia.cap.max_send_wr) {
		fprintf(stderr, "provider returned max_send_wr=0\n");
		goto out_qp;
	}
	if ((!is_sender || is_bidi) && !qpia.cap.max_recv_wr) {
		fprintf(stderr, "provider returned max_recv_wr=0\n");
		goto out_qp;
	}
	if ((is_sender || is_bidi) &&
	    send_depth > (int)qpia.cap.max_send_wr) {
		fprintf(stderr,
			"clamping send depth %d to provider max_send_wr %u\n",
			send_depth, qpia.cap.max_send_wr);
		send_depth = (int)qpia.cap.max_send_wr;
	}
	if ((!is_sender || is_bidi) &&
	    initial_recvs > (int)qpia.cap.max_recv_wr) {
		fprintf(stderr,
			"clamping initial recv posts %d to provider max_recv_wr %u\n",
			initial_recvs, qpia.cap.max_recv_wr);
		initial_recvs = (int)qpia.cap.max_recv_wr;
	}
	if (use_uc_credits) {
		if (is_sender && ctl_recv_slots > (int)qpia.cap.max_recv_wr) {
			fprintf(stderr,
				"clamping control recv slots %d to provider max_recv_wr %u\n",
				ctl_recv_slots, qpia.cap.max_recv_wr);
			ctl_recv_slots = (int)qpia.cap.max_recv_wr;
			ctl.n_recv = ctl_recv_slots;
		}
		if (!is_sender && ctl_send_slots > (int)qpia.cap.max_send_wr) {
			fprintf(stderr,
				"clamping control send slots %d to provider max_send_wr %u\n",
				ctl_send_slots, qpia.cap.max_send_wr);
			ctl_send_slots = (int)qpia.cap.max_send_wr;
			ctl.n_send = ctl_send_slots;
		}
		if ((is_sender && ctl_recv_slots < 1) ||
		    (!is_sender && ctl_send_slots < 1)) {
			fprintf(stderr,
				"credits=uc: provider left no room for the control queue on the reverse direction\n");
			early_reason = "no control queue capacity";
			goto out_qp;
		}
		ctl.qp = qp;
	}
	debug_step("modify init");
	if (qp_to_init(qp, o.ib_port))
		goto out_qp;

	/* Match JACCL's fixed initial PSN to rule it out as a delivery factor. */
	psn = 7;
	memset(&local, 0, sizeof(local));
	local.magic = 0x55433157u; /* UC1W */
	local.qpn = qp->qp_num;
	local.psn = psn;
	local.lid = port_attr.lid;
	if (getenv("UC_ONEWAY_LID_OVERRIDE"))
		local.lid = (uint32_t)strtoul(getenv("UC_ONEWAY_LID_OVERRIDE"),
					      NULL, 0);
	memcpy(local.gid, local_gid.raw, sizeof(local.gid));
	local.flags = o.credits ? PEER_FLAG_CREDITS : 0;
	if (use_uc_credits)
		local.flags |= PEER_FLAG_CREDITS_UC;

	if (o.connect_host) {
		debug_step("tcp connect");
		sock = tcp_connect(o.connect_host, o.port);
		if (sock < 0) {
			perror("tcp connect");
			goto out_qp;
		}
		debug_step("tcp connected");
	} else {
		debug_step("tcp listen");
		listen_fd = tcp_listen(o.port);
		if (listen_fd < 0)
			goto out_qp;
		printf("listening on TCP port %d\n", o.port);
		fflush(stdout);
		debug_step("tcp accept");
		{
			/* Bounded accept. An unbounded one keeps the listening
			 * port -- and this process -- alive long after the run
			 * it belonged to, and the NEXT run's connector then
			 * fails to bind or is refused. */
			struct pollfd pfd = { listen_fd, POLLIN, 0 };
			int pr = poll(&pfd, 1, o.handshake_timeout_ms);

			if (pr <= 0) {
				fprintf(stderr,
					"no peer connected within %d ms; giving up\n",
					o.handshake_timeout_ms);
				early_reason = "no peer connected";
				goto out_qp;
			}
		}
		sock = accept(listen_fd, NULL, NULL);
		if (sock < 0) {
			perror("accept");
			early_reason = "accept failed";
			goto out_qp;
		}
		debug_step("tcp accepted");
		close(listen_fd);
		listen_fd = -1;
	}

	{
		int one = 1;
		int got = 0;
		socklen_t gotlen = sizeof(got);
		struct timeval tv;

		/* Credit records are 64 bytes and latency-critical: a grant
		 * sitting in Nagle's buffer is a stalled sender. */
		if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one,
			       sizeof(one)))
			perror("setsockopt TCP_NODELAY");
		/* Assert it from the socket rather than trusting the call. */
		if (getsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &got, &gotlen) ||
		    !got)
			fprintf(stderr,
				"warning: TCP_NODELAY not in effect on the control socket (readback=%d)\n",
				got);
		else if (o.verbose)
			fprintf(stderr, "control socket: TCP_NODELAY=%d\n", got);

		/* Every blocking control read is bounded, so a peer that dies
		 * mid-handshake cannot leave this process resident. */
		tv.tv_sec = o.handshake_timeout_ms / 1000;
		tv.tv_usec = (o.handshake_timeout_ms % 1000) * 1000;
		if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)))
			perror("setsockopt SO_RCVTIMEO");
	}
	debug_step("exchange info");
	if (exchange_info(sock, &local, &remote)) {
		early_reason = "metadata exchange failed";
		goto out_qp;
	}
	debug_step("exchange done");
	if (((remote.flags & PEER_FLAG_CREDITS) != 0) != (o.credits != 0)) {
		fprintf(stderr,
			"credit mode mismatch: local --credits=%d, peer --credits=%d; both roles must agree\n",
			o.credits ? 1 : 0,
			(remote.flags & PEER_FLAG_CREDITS) ? 1 : 0);
		early_reason = "credit mode mismatch";
		goto out_qp;
	}
	if (((remote.flags & PEER_FLAG_CREDITS_UC) != 0) !=
	    (use_uc_credits != 0)) {
		fprintf(stderr,
			"credit transport mismatch: local --credits=%s, peer --credits=%s; both roles must agree\n",
			cr_xport_str(use_uc_credits ? CR_XPORT_UC : CR_XPORT_TCP),
			(remote.flags & PEER_FLAG_CREDITS_UC) ? "uc" : "tcp");
		early_reason = "credit transport mismatch";
		goto out_qp;
	}
	cr_chan_init(&chan, sock);
	cr_out_init(&out, sock);
	if (getenv("UC_ONEWAY_REMOTE_QPN_OVERRIDE"))
		remote.qpn = (uint32_t)strtoul(getenv("UC_ONEWAY_REMOTE_QPN_OVERRIDE"),
					       NULL, 0);
	memcpy(remote_gid.raw, remote.gid, sizeof(remote.gid));
	printf("%s local_qpn=%u remote_qpn=%u size=%zu count=%d depth=%d send_slots=%d recv_posts=%d mtu=%d\n",
	       o.role, local.qpn, remote.qpn, o.size, o.count, o.depth,
	       send_slots, initial_recvs, o.mtu);
	fprintf(stderr, "local_gid=");
	print_gid(stderr, &local_gid);
	fprintf(stderr, " remote_gid=");
	print_gid(stderr, &remote_gid);
	fprintf(stderr, "\n");

	fprintf(stderr, "transitioning QP to RTR/RTS\n");
	fflush(stderr);
	if (qp_to_rts(qp, o.ib_port, sgid_index, &remote_gid, remote.lid,
		      remote.qpn, local.psn, remote.psn, mtu_enum(o.mtu))) {
		early_reason = "QP did not reach RTS";
		goto out_qp;
	}
	fprintf(stderr, "QP is RTS\n");
	fflush(stderr);
	if (o.hold_after_rts_ms > 0) {
		fprintf(stderr, "holding after RTS for %d ms\n",
			o.hold_after_rts_ms);
		fflush(stderr);
		usleep((useconds_t)o.hold_after_rts_ms * 1000u);
	}

	if (use_uc_credits && is_sender) {
		/* Pre-post the dedicated control receive queue BEFORE telling
		 * the receiver anything about it. The count we then advertise
		 * is the number of posts the provider actually accepted. */
		int accepted = 0;

		for (int i = 0; i < ctl_recv_slots; i++) {
			errno = 0;
			if (ctl_post_recv(&ctl, i)) {
				fprintf(stderr,
					"credits=uc: control ibv_post_recv[%d/%d] failed errno=%d; continuing with %d\n",
					i, ctl_recv_slots, errno, accepted);
				break;
			}
			accepted++;
		}
		if (!accepted) {
			fprintf(stderr,
				"credits=uc: provider accepted no control receives; refusing to run\n");
			early_reason = "no control receives accepted";
			goto out_qp;
		}
		ctl_recv_slots = accepted;
		ctl.n_recv = accepted;
		fprintf(stderr,
			"credits=uc: control RQ posted=%d cumulative=%llu\n",
			accepted, (unsigned long long)ctl.recv_posted);
		fflush(stderr);
	}

	if (!is_sender || is_bidi) {
		if (o.recv_post_delay_ms > 0) {
			fprintf(stderr, "delaying receive posts by %d ms\n",
				o.recv_post_delay_ms);
			fflush(stderr);
			usleep((useconds_t)o.recv_post_delay_ms * 1000u);
		}
		fprintf(stderr, "posting %d receives\n", initial_recvs);
		fflush(stderr);
		requested_recvs = initial_recvs;
		for (int i = 0; i < initial_recvs; i++) {
			errno = 0;
			int post_ret = post_recv_slot(&o, qp, mr, recv_buf,
						      stride, o.size, i);
			if (post_ret) {
				if (i > 0) {
					fprintf(stderr,
						"provider accepted only %d/%d initial receives; continuing with repost window %d (ret=%d errno=%d)\n",
						i, initial_recvs, i, post_ret,
						errno);
					initial_recvs = i;
					break;
				}
				fprintf(stderr,
					"ibv_post_recv[%d/%d] ret=%d errno=%d\n",
					i, initial_recvs, post_ret, errno);
				early_reason = "initial ibv_post_recv failed";
				goto out_qp;
			}
			posted_initial_recvs++;
			rx_posted_total++;
			if (data_ts) {
				data_ts[i] = now_ns();
				wl_on_post(&lat_data, data_ts[i]);
			}
		}
		fprintf(stderr, "posted %d receives\n", posted_initial_recvs);
		if (o.recv_keep_posted && !o.credits)
			fprintf(stderr,
				"recv keep-posted: window held at %d receives until the run ends (uncredited, explicit)\n",
				posted_initial_recvs);
		/* Apple reports queue capacities in 4 KiB FRAMES, not messages.
		 * Print both so a "60 receive" window is never silently read as
		 * 60 messages of an arbitrary size. */
		fprintf(stderr,
			"recv window: requested=%d accepted=%d provider_max_recv_wr=%u msg_size=%zu frames_per_msg=%u accepted_frames=%llu\n",
			requested_recvs, posted_initial_recvs,
			qpia.cap.max_recv_wr, o.size, frames_per_msg,
			(unsigned long long)posted_initial_recvs *
				frames_per_msg);
		fflush(stderr);
		if (o.credits) {
			/* Initial grants: min(N, posted - k). The guard is
			 * withheld from the very first grant, not at the tail. */
			uint64_t ceiling = guard_ceiling(rx_posted_total,
							 (uint64_t)o.recv_guard,
							 (uint64_t)o.count,
							 o.recv_keep_posted);

			(void)credit_grant_to_ceiling(&cs, ceiling);
			if (!ceiling) {
				fprintf(stderr,
					"credits: receive window %d does not exceed --recv-guard %d; nothing could ever be granted (raise --recv-posts or lower --recv-guard)\n",
					posted_initial_recvs, o.recv_guard);
				early_reason = "receive window within guard";
				goto out_qp;
			}
			cs.ready = 1;
			initial_recvs = posted_initial_recvs;
		}
	}

	if (is_bidi) {
		char ready = 'B';
		char peer_ready;

		if (send_all(sock, &ready, 1)) {
			perror("bidi ready send");
			goto out_qp;
		}
		if (recv_all(sock, &peer_ready, 1) || peer_ready != 'B') {
			fprintf(stderr, "peer did not signal bidi ready\n");
			goto out_qp;
		}
	} else if (is_sender) {
		char ready;
		char ack = 'A';

		if (o.credits) {
			uint8_t rbuf[CR_WIRE_LEN];
			struct cr_record rec;
			enum cr_result r;

			/* A --credits sender REFUSES to post anything without
			 * the receiver's READY record: with no grant ledger it
			 * would be exactly the unprotected sender this flag
			 * exists to replace. */
			if (recv_all(sock, rbuf, sizeof(rbuf))) {
				fprintf(stderr,
					"credits: no READY record from receiver; refusing to send\n");
				goto out_qp;
			}
			r = cr_decode(rbuf, &rec);
			if (r == CR_OK)
				r = credit_accept_ready(&cs, &rec,
							(uint32_t)o.size);
			if (r != CR_OK) {
				fprintf(stderr,
					"credits: rejecting READY record (%s); refusing to send\n",
					cr_result_str(r));
				cr_send_abort(sock, &cs, (uint64_t)r);
				goto out_qp;
			}
			fprintf(stderr,
				"credits: READY generation=0x%08x msg_size=%u frames_per_msg=%u recv_requested=%u recv_accepted=%u initial_grants=%llu (%llu frames)\n",
				cs.generation, cs.msg_size, cs.frames_per_msg,
				rec.recv_requested, rec.recv_accepted,
				(unsigned long long)cs.granted,
				(unsigned long long)cs.frames_granted);
			fflush(stderr);
			if (use_uc_credits) {
				/* The CTLWIN record replaces the ack byte: it is
				 * both "I saw your READY" and the bootstrap of
				 * the control-receive window the receiver must
				 * respect before it may send one on-link grant. */
				struct cr_record cw;

				cr_record_init(&cw, CR_TYPE_CTLWIN, &cs);
				cw.recv_requested = (uint32_t)o.ctl_depth;
				cw.recv_accepted = (uint32_t)ctl_recv_slots;
				cw.cum_grants = ctl.recv_posted;
				cw.cum_frames = ctl.recv_posted;
				if (cr_send_record(sock, &cw)) {
					perror("credits=uc: CTLWIN send");
					goto out_qp;
				}
				fprintf(stderr,
					"credits=uc: sent CTLWIN control_recvs=%d cumulative=%llu\n",
					ctl_recv_slots,
					(unsigned long long)ctl.recv_posted);
				fflush(stderr);
				goto handshake_done;
			}
			if (send_all(sock, &ack, 1)) {
				perror("ack send");
				goto out_qp;
			}
			goto handshake_done;
		}
		if (recv_all(sock, &ready, 1) || ready != 'R') {
			fprintf(stderr, "receiver did not signal ready\n");
			goto out_qp;
		}
		/* Two-way handshake: confirm to receiver that we saw 'R'
		 * before posting the first SEND. This pins down a kernel-
		 * side QP-registration race window where the first SENDs
		 * arrive at the receiver before its lookup_qp can find the
		 * QP, charging up rx_no_qp and poisoning credit accounting. */
		if (send_all(sock, &ack, 1)) {
			perror("ack send");
			goto out_qp;
		}
	} else {
		char ready = 'R';
		char ack;

		if (o.credits) {
			struct cr_record rec;

			cr_record_init(&rec, CR_TYPE_READY, &cs);
			rec.recv_requested = (uint32_t)requested_recvs;
			rec.recv_accepted = (uint32_t)posted_initial_recvs;
			if (cr_send_record(sock, &rec)) {
				perror("credits: READY send");
				early_reason = "READY send failed";
				goto out_qp;
			}
			fprintf(stderr,
				"credits: sent READY generation=0x%08x grants=%llu (%llu frames)\n",
				cs.generation,
				(unsigned long long)cs.granted,
				(unsigned long long)cs.frames_granted);
			/* The policy is part of the protocol now; it belongs in
			 * the log beside every number it affects. */
			fprintf(stderr,
				"credits: receive window policy=%s window=%d cap=%d guard=%d P=%llu G=%llu ceiling=%llu\n",
				o.recv_keep_posted ? "keep-posted" : "decay",
				posted_initial_recvs, o.count, o.recv_guard,
				(unsigned long long)rx_posted_total,
				(unsigned long long)cs.granted,
				(unsigned long long)guard_ceiling(rx_posted_total,
					(uint64_t)o.recv_guard, (uint64_t)o.count,
					o.recv_keep_posted));
			fflush(stderr);
		} else if (send_all(sock, &ready, 1)) {
			perror("ready send");
			early_reason = "ready send failed";
			goto out_qp;
		}
		if (use_uc_credits) {
			uint8_t rbuf[CR_WIRE_LEN];
			struct cr_record cw;
			enum cr_result r;

			if (recv_all(sock, rbuf, sizeof(rbuf))) {
				fprintf(stderr,
					"credits=uc: no CTLWIN record from sender\n");
				early_reason = "sender never sent CTLWIN";
				goto out_qp;
			}
			r = cr_decode(rbuf, &cw);
			if (r != CR_OK || cw.type != CR_TYPE_CTLWIN ||
			    cw.generation != cs.generation) {
				fprintf(stderr,
					"credits=uc: rejecting CTLWIN record (%s type=%s)\n",
					cr_result_str(r), cr_type_str(cw.type));
				early_reason = "bad CTLWIN record";
				goto out_qp;
			}
			(void)ctl_credit_apply(&cc, cw.cum_grants);
			fprintf(stderr,
				"credits=uc: CTLWIN control_recvs=%u cumulative=%llu send_slots=%d\n",
				cw.recv_accepted,
				(unsigned long long)cc.granted, ctl_send_slots);
			fflush(stderr);
		} else if (recv_all(sock, &ack, 1) || ack != 'A') {
			fprintf(stderr,
				"sender did not ack ready within %d ms\n",
				o.handshake_timeout_ms);
			early_reason = "sender never acked READY";
			goto out_qp;
		}
	}

handshake_done:
	start = now_ns();
	last_t = start;
	/* Trace offsets share the origin of every `elapsed=` in this log, and a
	 * gap is only ever measured inside the data phase: receives (and the
	 * sender's control RQ) are posted before the handshake, and the wait
	 * for a peer that is not yet allowed to send is dwell, not a stall. */
	tr.t0 = start;
	if (lat_data.outstanding)
		lat_data.last_event = start;
	if (lat_ctl.outstanding)
		lat_ctl.last_event = start;
	if (!is_bidi)
		wl_print_config(stderr, &tr, is_sender, use_uc_credits);
	if (is_bidi) {
		struct ibv_wc wc[32];
		int send_posted = 0, send_completed = 0, send_in_flight = 0;
		int recv_posted = initial_recvs, recv_completed = 0;

		while (send_completed < o.count || recv_completed < o.count) {
			while (send_posted < o.count &&
			       send_in_flight < send_depth) {
				int slot = send_posted % send_slots;
				struct ibv_sge sge;
				struct ibv_send_wr wr;
				struct ibv_send_wr *bad = NULL;

				memset(&sge, 0, sizeof(sge));
				if (o.check)
					fill_pattern(send_buf + (size_t)slot * stride,
						     o.size, (uint64_t)send_posted);
				sge.addr = (uintptr_t)(send_buf + (size_t)slot * stride);
				sge.length = (uint32_t)o.size;
				sge.lkey = mr->lkey;
				memset(&wr, 0, sizeof(wr));
				wr.wr_id = o.send_wr_id_base >= 0 ?
					   wr_id_for_slot(o.send_wr_id_base, slot) :
					   0x8000000000000000ull |
						   (uint64_t)send_posted;
				wr.sg_list = &sge;
				wr.num_sge = 1;
				wr.opcode = IBV_WR_SEND;
				wr.send_flags = IBV_SEND_SIGNALED;
				if (ibv_post_send(qp, &wr, &bad)) {
					if (send_in_flight > 0 &&
					    send_depth > send_in_flight) {
						fprintf(stderr,
							"provider accepted only %d/%d in-flight sends; continuing with send depth %d\n",
							send_in_flight, send_depth,
							send_in_flight);
						send_depth = send_in_flight;
						break;
					}
					perror("ibv_post_send");
					goto out_qp;
				}
				send_posted++;
				send_in_flight++;
			}

			int n = ibv_poll_cq(cq, 32, wc);
			if (n < 0) {
				fprintf(stderr, "ibv_poll_cq failed: %d\n", n);
				goto out_qp;
			}
			for (int i = 0; i < n; i++) {
				if (wc[i].status != IBV_WC_SUCCESS) {
					fprintf(stderr,
						"bidi wc error wr_id=%llu status=%u opcode=%u byte_len=%u sent=%d/%d recv=%d/%d\n",
						(unsigned long long)wc[i].wr_id,
						wc[i].status, wc[i].opcode,
						wc[i].byte_len, send_completed,
						o.count, recv_completed,
						o.count);
					goto out_qp;
				}
				if (wc[i].opcode == IBV_WC_SEND ||
				    (wc[i].wr_id & 0x8000000000000000ull)) {
					send_completed++;
					send_in_flight--;
					continue;
				}

				int slot = slot_from_wr_id(&o, wc[i].wr_id);

				if (o.check) {
					char *p = recv_buf + (size_t)slot * stride;
					uint64_t seq = o.check_any_order ?
						load_le64(p) :
						(uint64_t)recv_completed;

					if (o.check_any_order &&
					    check_seen(seen, o.count, seq, slot,
						       recv_completed, wc[i].wr_id))
						goto out_qp;
					if (check_pattern(p, o.size, seq, slot,
							  recv_completed,
							  wc[i].wr_id,
							  wc[i].byte_len))
						goto out_qp;
				}
				recv_completed++;
				if (recv_posted < o.count) {
					errno = 0;
					int post_ret = post_recv_slot(&o, qp, mr,
								      recv_buf,
								      stride,
								      o.size,
								      slot);
					if (post_ret) {
						fprintf(stderr,
							"ibv_post_recv[repost slot=%d posted=%d count=%d] ret=%d errno=%d\n",
							slot, recv_posted,
							o.count, post_ret,
							errno);
						goto out_qp;
					}
					recv_posted++;
				}
			}
			print_rate("bidi-recv", recv_completed, o.count, o.size,
				   start, &last_t, &last_done);
		}
		printf("bidi done send=%d/%d recv=%d/%d\n", send_completed,
		       o.count, recv_completed, o.count);
		fflush(stdout);
		{
			char done = 'D';
			char peer_done;

			if (send_all(sock, &done, 1)) {
				perror("bidi done send");
				goto out_qp;
			}
			if (recv_all(sock, &peer_done, 1) || peer_done != 'D') {
				fprintf(stderr, "peer did not signal bidi done\n");
				goto out_qp;
			}
		}
	} else if (is_sender) {
		struct sender_ctx sx;
		int posted = 0;
		int starved = 0;
		uint64_t starve_start = 0, starve_ns = 0;
		int starve_events = 0;
		/*
		 * Two different things were being summed as one number. Split
		 * them: "posting starved while data is still in flight" is the
		 * sender running out of PERMISSION with the wire still busy;
		 * "SQ idle waiting for credit" is the only interval in which
		 * the link is genuinely doing nothing because of credit.
		 */
		int stall_bucket = 0;   /* 0 = posting starved, 1 = SQ idle */
		uint64_t seg_start = 0;
		uint64_t stall_post_ns = 0, stall_idle_ns = 0;
		int stall_post_events = 0, stall_idle_events = 0;
		uint64_t last_resend = 0;
		uint64_t resend_ns = (uint64_t)o.grant_resend_ms * 1000000ull;
		const char *reason = "complete";
		int failed = 0;

		memset(&sx, 0, sizeof(sx));
		sx.cq = cq;
		sx.cs = &cs;
		sx.chan = &chan;
		sx.out = &out;
		sx.ctl = &ctl;
		sx.use_uc = use_uc_credits;
		sx.lat_data = &lat_data;
		sx.tr = &tr;
		sx.send_ts = data_ts;
		sx.send_ids = data_ids;
		sx.ring = (unsigned)o.depth;

		fprintf(stderr,
			"send window: requested depth=%d accepted=%u msg_size=%zu frames_per_msg=%u credits=%s transport=%s ctl_recv_slots=%d\n",
			o.depth, qpia.cap.max_send_wr, o.size, frames_per_msg,
			o.credits ? "on" : "off",
			o.credits ? cr_xport_str(o.credit_xport) : "-",
			ctl_recv_slots);
		fflush(stderr);

		while (sx.completed < o.count) {
			while (posted < o.count && sx.in_flight < send_depth) {
				int slot = posted % send_slots;
				uint64_t wr_id = o.send_wr_id_base >= 0 ?
					wr_id_for_slot(o.send_wr_id_base, slot) :
					(uint64_t)posted;

				/* Reserve receiver capacity BEFORE the post.
				 * Local completion never refills this: only a
				 * successful remote ibv_post_recv() does. */
				if (o.credits) {
					int have = credit_reserve(&cs);

					if (!have) {
						/* Consume grants that are ALREADY
						 * here before calling this a
						 * stall; otherwise a delivered-
						 * but-unread grant is recorded as
						 * starvation. */
						if (sender_drain_tcp(&sx, 0,
								     o.verbose)) {
							reason = sx.fail_reason;
							failed = 1;
							goto send_finish;
						}
						if (sx.use_uc &&
						    sender_poll_cq(&sx, o.verbose) < 0) {
							reason = sx.fail_reason;
							failed = 1;
							goto send_finish;
						}
						have = credit_reserve(&cs);
					}
					if (!have) {
						uint64_t now = now_ns();

						if (!starved) {
							starved = 1;
							starve_start = now;
							seg_start = now;
							starve_events++;
							stall_bucket = sx.in_flight > 0 ? 0 : 1;
							if (stall_bucket)
								stall_idle_events++;
							else
								stall_post_events++;
							if (o.verbose)
								fprintf(stderr,
									"credits: starved at posted=%d granted=%llu reserved=%llu in_flight=%d (%s)\n",
									posted,
									(unsigned long long)cs.granted,
									(unsigned long long)cs.reserved,
									sx.in_flight,
									stall_bucket ? "sq idle" : "posting");
						}
						break;
					}
				}
				if (starved) {
					uint64_t now = now_ns();

					if (stall_bucket)
						stall_idle_ns += now - seg_start;
					else
						stall_post_ns += now - seg_start;
					starve_ns += now - starve_start;
					starved = 0;
					if (o.verbose)
						fprintf(stderr,
							"credits: resumed at posted=%d granted=%llu after %.3f ms\n",
							posted,
							(unsigned long long)cs.granted,
							(now - starve_start) / 1e6);
				}
				uint64_t posted_at = 0;

				if (post_send_slot(&o, qp, mr, send_buf, stride,
						   slot, (uint64_t)posted,
						   wr_id, &posted_at)) {
					if (o.credits)
						credit_unreserve(&cs);
					if (sx.in_flight > 0 &&
					    send_depth > sx.in_flight) {
						fprintf(stderr,
							"provider accepted only %d/%d in-flight sends; continuing with send depth %d\n",
							sx.in_flight, send_depth,
							sx.in_flight);
						send_depth = sx.in_flight;
						break;
					}
					perror("ibv_post_send");
					reason = "ibv_post_send failed";
					failed = 1;
					goto send_finish;
				}
				posted++;
				sx.in_flight++;
				sx.send_ts[sx.post_seq % sx.ring] = posted_at;
				sx.send_ids[sx.post_seq % sx.ring] = wr_id;
				sx.post_seq++;
				wl_on_post(&lat_data, posted_at);
			}

			if (o.credits) {
				/* Only ever wait on the socket when there is
				 * nothing else to make progress on; the CQ loop
				 * is never blocked behind credit delivery. On
				 * the UC transport grants arrive through the CQ,
				 * so never park in poll() at all. */
				int timeout = (!sx.use_uc && sx.in_flight == 0 &&
					       posted < o.count) ? 2 : 0;

				if (sender_drain_tcp(&sx, timeout, o.verbose) ||
				    sender_publish_ctlwin(&sx, 0)) {
					reason = sx.fail_reason;
					failed = 1;
					goto send_finish;
				}
			}

			{
				int polled = sender_poll_cq(&sx, o.verbose);

				if (polled < 0) {
					reason = sx.fail_reason;
					failed = 1;
					goto send_finish;
				}
				/* Nothing came back: say so while it is
				 * happening, not only after the run. */
				if (!polled && sx.in_flight > 0)
					wl_poll_idle(&lat_data, &tr, now_ns());
			}

			if (starved) {
				uint64_t now = now_ns();
				int bucket = sx.in_flight > 0 ? 0 : 1;
				uint64_t quiet = sx.last_grant_progress >
						 starve_start ?
						 sx.last_grant_progress :
						 starve_start;

				if (bucket != stall_bucket) {
					if (stall_bucket)
						stall_idle_ns += now - seg_start;
					else
						stall_post_ns += now - seg_start;
					stall_bucket = bucket;
					seg_start = now;
					if (bucket)
						stall_idle_events++;
					else
						stall_post_events++;
				}
				/* No grant progress while starved: ask for a
				 * re-publication of the latest cumulative
				 * snapshot. Never invent capacity. */
				if (now - quiet >= resend_ns &&
				    now - last_resend >= resend_ns) {
					if (sender_request_resend(&sx)) {
						reason = sx.fail_reason;
						failed = 1;
						goto send_finish;
					}
					last_resend = now;
				}
			}
			print_rate("send", sx.completed, o.count, o.size, start,
				   &last_t, &last_done);
		}

send_finish:
		if (starved) {
			uint64_t now = now_ns();

			if (stall_bucket)
				stall_idle_ns += now - seg_start;
			else
				stall_post_ns += now - seg_start;
			starve_ns += now - starve_start;
		}
		final_completed = (uint64_t)sx.completed;
		final_status = failed ? 1u : 0u;
		/*
		 * Close admission, THEN FINAL. The strict exchange runs before
		 * the summary so `send summary:` carries the verdict: FINAL
		 * means no further send can ever be reserved, TX is retired
		 * (posted == completed), and the peer confirmed this generation
		 * and the full count. A run that already failed sends ABORT
		 * (below) and the loose exchange in out_final only logs.
		 */
		if (!failed && o.credits && sock >= 0 && cs.ready) {
			const char *why = NULL;

			credit_close(&cs);
			if (posted != sx.completed) {
				failed = 1;
				final_status = 1;
				reason = "TX not retired at FINAL";
			} else if (credit_final_exchange(sock, &chan, &out, &cs,
							 final_completed,
							 final_status,
							 (uint32_t)posted_initial_recvs,
							 (uint32_t)requested_recvs,
							 (uint64_t)o.count,
							 &why)) {
				failed = 1;
				reason = why;
			}
			final_done = 1;
		}
		fprintf(stderr,
			"send summary: posted=%d completed=%d/%d credits=%s granted=%llu reserved=%llu credit_stalls=%d credit_stall_ms=%.3f reason=%s %s\n",
			posted, sx.completed, o.count, o.credits ? "on" : "off",
			(unsigned long long)cs.granted,
			(unsigned long long)cs.reserved, starve_events,
			starve_ns / 1e6, reason, failed ? "FAIL" : "OK");
		fprintf(stderr,
			"send stalls: transport=%s posting_starved_events=%d posting_starved_ms=%.3f sq_idle_events=%d sq_idle_ms=%.3f grants_uc=%llu grants_tcp=%llu grants_dup=%llu grants_stale=%llu resend_requests=%llu\n",
			o.credits ? cr_xport_str(o.credit_xport) : "off",
			stall_post_events, stall_post_ns / 1e6,
			stall_idle_events, stall_idle_ns / 1e6,
			(unsigned long long)sx.grants_uc,
			(unsigned long long)sx.grants_tcp,
			(unsigned long long)sx.grants_dup,
			(unsigned long long)sx.grants_stale,
			(unsigned long long)sx.resend_requests);
		if (use_uc_credits)
			fprintf(stderr,
				"send control-rq: slots=%d cumulative_posts=%llu records=%llu reposts=%llu repost_failures=%llu bad_records=%llu wc_errors=%llu ctlwin_sent=%llu\n",
				ctl_recv_slots,
				(unsigned long long)ctl.recv_posted,
				(unsigned long long)ctl.recv_done,
				(unsigned long long)sx.ctl_reposts,
				(unsigned long long)sx.ctl_repost_fail,
				(unsigned long long)sx.ctl_bad_records,
				(unsigned long long)sx.ctl_wc_errors,
				(unsigned long long)sx.ctlwin_sent);
		wl_print(stderr, "send", &lat_data);
		if (use_uc_credits)
			wl_print(stderr, "send", &lat_ctl);
		wl_print_gaps(stderr, &tr);
		if (posted != sx.completed)
			(void)sender_print_outstanding(stderr, &sx, posted,
						       start);
		fflush(stderr);
		(void)cr_out_drain(&out, 1000);
		if (failed) {
			if (o.credits)
				cr_send_abort(sock, &cs, 1);
			ret = 1;
			goto out_final;
		}
	} else {
		struct ibv_wc wc[32];
		int completed = 0, posted = initial_recvs;
		/*
		 * Drain instead of abort. A receiver that exits on the first bad
		 * message leaves the sender's already-posted frames with nowhere to
		 * go: hop-level backpressure holds the sender's TX descriptors, its
		 * QP destroy times out with refs held ("QP N destroy timed out with
		 * 2 refs"), ib_core warns in uverbs_destroy_ufile_hw, and on the
		 * Apple path -- where every inbound frame demuxes to that one QPN --
		 * the device is poisoned until reboot (measured 2026-09-05, four
		 * times). So keep polling and re-posting until every expected message
		 * has arrived, count the failures, and report them at the end.
		 * A QP in the error state flushes every remaining receive with an
		 * error; bail after a run of those, which is unrecoverable anyway.
		 */
		int mismatches = 0, wc_errors = 0, consecutive_wc_errors = 0;
		int repost_failures = 0;
		/*
		 * --recv-keep-posted: every stall seen so far (1978, 1979, 1981,
		 * 1947 of 2000) sits where the receiver has posted its last
		 * receive and its window decays from 60 toward 0. Keep reposting
		 * past --count so the window stays full until FINAL. Such a
		 * receive backs no message: it mints no grant, is never a
		 * completion, and a failure to post one is counted, not fatal.
		 */
		int extra_posted = 0, extra_post_failures = 0;
		int keep_posting = o.recv_keep_posted;
		int final_failed = 0;
		const char *reason = "complete";
		uint64_t last_progress = now_ns();
		uint64_t drain_ns = (uint64_t)o.drain_timeout_ms * 1000000ull;
		uint64_t hold_deadline = 0;
		int holding = 0, hold_done = 0;
		int *held = NULL;
		int held_n = 0, held_cap = 0;
		struct pub_ctx pub;
		uint64_t resend_ns = (uint64_t)o.grant_resend_ms * 1000000ull;
		uint64_t ctlwin_records = 0;
		uint64_t ctl_send_wc_errors = 0;

		/* READY already carried the initial grants, so the publisher
		 * starts from that total rather than from zero. */
		grant_pub_init(&gp, use_uc_credits ? CR_XPORT_UC : CR_XPORT_TCP,
			       cs.granted, o.grant_deadline_us, o.grant_batch);
		memset(&pub, 0, sizeof(pub));
		pub.gp = &gp;
		pub.cs = &cs;
		pub.out = &out;
		pub.ctl = use_uc_credits ? &ctl : NULL;
		pub.cc = use_uc_credits ? &cc : NULL;
		if (o.credits)
			fprintf(stderr,
				"credits: publishing transport=%s batch=%d deadline_us=%d\n",
				cr_xport_str(use_uc_credits ? CR_XPORT_UC :
						CR_XPORT_TCP),
				o.grant_batch, o.grant_deadline_us);

		if (o.recv_repost_hold_ms > 0) {
			held_cap = initial_recvs > 0 ? initial_recvs : 1;
			held = calloc((size_t)held_cap, sizeof(int));
			if (!held) {
				perror("calloc held slots");
				reason = "out of memory";
				goto recv_finish;
			}
			fprintf(stderr,
				"repost hold armed: withholding reposts for %d ms after the first completion (%d slots)\n",
				o.recv_repost_hold_ms, held_cap);
			fflush(stderr);
		}

		while (completed < o.count) {
			int n = ibv_poll_cq(cq, 32, wc);

			if (n < 0) {
				fprintf(stderr, "ibv_poll_cq failed: %d\n", n);
				reason = "ibv_poll_cq failed";
				goto recv_finish;
			}
			if (!n) {
				{
					uint64_t idle_now = now_ns();

					wl_poll_idle(&lat_data, &tr, idle_now);
					if (use_uc_credits)
						wl_poll_idle(&lat_ctl, &tr,
							     idle_now);
				}
				/* Idle: this is exactly when the sender may be
				 * waiting on credit, so publish unconditionally.
				 * Batching can never deadlock a small window. */
				if (o.credits) {
					uint64_t now = now_ns();
					uint64_t abort_aux = 0;
					enum cr_result r = CR_OK;
					int escape = use_uc_credits &&
						cc.stall_since &&
						now - cc.stall_since >= resend_ns;
					int cr;

					if (pub_publish(&pub, now, 1, escape) < 0) {
						reason = "grant send failed";
						goto recv_finish;
					}
					cr = recv_drain_tcp(&chan, &pub, &cc, 0,
							    &abort_aux,
							    &ctlwin_records, &r);
					if (cr == 1) {
						fprintf(stderr,
							"credits: peer ABORT (aux=%llu)\n",
							(unsigned long long)abort_aux);
						cs.aborted = 1;
						reason = "peer abort";
						goto recv_finish;
					}
					if (cr < 0) {
						fprintf(stderr,
							"credits: control channel failure (%s)\n",
							cr_result_str(r));
						reason = "credit channel failure";
						goto recv_finish;
					}
				}
				if (holding && now_ns() >= hold_deadline)
					goto release_hold;
				/* Bounded drain: a receiver that spins forever
				 * on zero CQ progress never prints its summary
				 * and never lets the QP be torn down cleanly. */
				if (now_ns() - last_progress > drain_ns) {
					fprintf(stderr,
						"recv: no CQ progress for %d ms with %d/%d completed; ending drain\n",
						o.drain_timeout_ms, completed,
						o.count);
					reason = "drain timeout (no CQ progress)";
					goto recv_finish;
				}
				continue;
			}
			last_progress = now_ns();
			for (int i = 0; i < n; i++) {
				int slot;
				int bad = 0;

				/* Route by wr_id FIRST: a control completion is
				 * not an application message and must never be
				 * checked, counted or reposted as one. */
				if (use_uc_credits &&
				    ctl_wr_is_control(wc[i].wr_id)) {
					int cslot = (int)ctl_wr_slot(wc[i].wr_id);

					if (ctl_wr_is_send(wc[i].wr_id) &&
					    cslot < ctl.n_send) {
						wl_on_complete(&lat_ctl, &tr,
							       ctl.send_ts[cslot],
							       now_ns(),
							       wc[i].status);
						ctl.send_busy[cslot] = 0;
						ctl.send_done++;
						if (wc[i].status != IBV_WC_SUCCESS)
							ctl_send_wc_errors++;
					}
					continue;
				}

				slot = slot_from_wr_id(&o, wc[i].wr_id);
				if (slot >= 0 && slot < wr_depth)
					wl_on_complete(&lat_data, &tr,
						       data_ts[slot], now_ns(),
						       wc[i].status);
				if (wc[i].status != IBV_WC_SUCCESS) {
					fprintf(stderr,
						"recv wc error wr_id=%llu status=%u opcode=%u byte_len=%u\n",
						(unsigned long long)wc[i].wr_id,
						wc[i].status, wc[i].opcode,
						wc[i].byte_len);
					wc_errors++;
					bad = 1;
					if (++consecutive_wc_errors >= 64) {
						fprintf(stderr,
							"recv: %d consecutive wc errors (QP in error state), giving up\n",
							consecutive_wc_errors);
						reason = "64 consecutive wc errors";
						goto recv_finish;
					}
				} else {
					consecutive_wc_errors = 0;
				}
				if (!bad && o.check) {
					char *p = recv_buf + (size_t)slot * stride;
					uint64_t seq = o.check_any_order ?
						load_le64(p) : (uint64_t)completed;

					if (o.check_any_order &&
					    check_seen(seen, o.count, seq, slot,
						       completed, wc[i].wr_id))
						bad = 1;
					else if (check_pattern(p, o.size, seq, slot,
							       completed, wc[i].wr_id,
							       wc[i].byte_len))
						bad = 1;
					if (bad)
						mismatches++;
				}
				completed++;

				if (held && !hold_done) {
					/* Controlled starvation: retain the
					 * completed slot, issue no grant, and
					 * keep polling. */
					if (!holding) {
						holding = 1;
						hold_deadline = now_ns() +
							(uint64_t)o.recv_repost_hold_ms *
							1000000ull;
						fprintf(stderr,
							"repost hold START at completed=%d posted=%d granted=%llu (deadline +%d ms)\n",
							completed, posted,
							(unsigned long long)cs.granted,
							o.recv_repost_hold_ms);
						fflush(stderr);
					}
					if (held_n < held_cap)
						held[held_n++] = slot;
					continue;
				}

				if (posted < o.count) {
					errno = 0;
					int post_ret = post_recv_slot(&o, qp, mr,
								      recv_buf,
								      stride,
								      o.size,
								      slot);
					if (post_ret) {
						/* ENOMEM grants nothing. */
						fprintf(stderr,
							"ibv_post_recv[repost slot=%d posted=%d count=%d] ret=%d errno=%d\n",
							slot, posted,
							o.count, post_ret,
							errno);
						repost_failures++;
						reason = "ibv_post_recv failed";
						goto recv_finish;
					}
					posted++;
					rx_posted_total++;
					data_ts[slot] = now_ns();
					wl_on_post(&lat_data, data_ts[slot]);
					/*
					 * PUBLISH FROM HERE, not after the CQ
					 * batch. A grant minted by repost k of a
					 * 32-entry batch used to wait for the
					 * other 31 completions to be checked;
					 * now it reaches the transport inside
					 * this iteration, and the publisher's own
					 * deadline bounds it regardless of how
					 * long the batch takes.
					 */
					if (o.credits &&
					    credit_grant_to_ceiling(&cs,
						guard_ceiling(rx_posted_total,
							(uint64_t)o.recv_guard,
							(uint64_t)o.count,
							o.recv_keep_posted))) {
						uint64_t now = now_ns();

						grant_pub_note(&gp, now);
						if (pub_publish(&pub, now, 0,
								0) < 0) {
							reason = "grant send failed";
							goto recv_finish;
						}
					}
				} else if (keep_posting) {
					/* Past --count: hold the window. Such a
					 * receive is never a completion; it
					 * raises P, so it may release one of the
					 * last k guarded grants, never more than
					 * N. A failure here means the window was
					 * NOT maintained: the row is not
					 * evidence and fails, loudly. */
					errno = 0;
					if (post_recv_slot(&o, qp, mr, recv_buf,
							   stride, o.size,
							   slot)) {
						fprintf(stderr,
							"recv keep-posted: ibv_post_recv[slot=%d] errno=%d after %d extra posts; window not maintained\n",
							slot, errno,
							extra_posted);
						extra_post_failures++;
						reason = "keep_posted_broken";
						goto recv_finish;
					}
					extra_posted++;
					rx_posted_total++;
					data_ts[slot] = now_ns();
					wl_on_post(&lat_data, data_ts[slot]);
					if (o.credits &&
					    credit_grant_to_ceiling(&cs,
						guard_ceiling(rx_posted_total,
							(uint64_t)o.recv_guard,
							(uint64_t)o.count,
							o.recv_keep_posted))) {
						uint64_t now = now_ns();

						grant_pub_note(&gp, now);
						if (pub_publish(&pub, now, 0,
								0) < 0) {
							reason = "grant send failed";
							goto recv_finish;
						}
					}
				}
			}

			/* Bound the publication delay even when nothing was
			 * reposted this batch (pub_publish self-forces once the
			 * deadline is reached). */
			if (o.credits) {
				uint64_t now = now_ns();
				int escape = use_uc_credits && cc.stall_since &&
					     now - cc.stall_since >= resend_ns;

				if (pub_publish(&pub, now, 0, escape) < 0) {
					reason = "grant send failed";
					goto recv_finish;
				}
			}
			print_rate("recv", completed, o.count, o.size, start,
				   &last_t, &last_done);
			continue;

release_hold:
			holding = 0;
			hold_done = 1;
			fprintf(stderr,
				"repost hold END after %d ms; reposting %d retained slots\n",
				o.recv_repost_hold_ms, held_n);
			fflush(stderr);
			for (int h = 0; h < held_n; h++) {
				if (posted >= o.count) {
					if (!keep_posting)
						break;
					errno = 0;
					if (post_recv_slot(&o, qp, mr, recv_buf,
							   stride, o.size,
							   held[h])) {
						fprintf(stderr,
							"recv keep-posted: ibv_post_recv[slot=%d] errno=%d after hold; window not maintained\n",
							held[h], errno);
						extra_post_failures++;
						reason = "keep_posted_broken";
						goto recv_finish;
					}
					extra_posted++;
					rx_posted_total++;
					data_ts[held[h]] = now_ns();
					wl_on_post(&lat_data, data_ts[held[h]]);
					if (o.credits &&
					    credit_grant_to_ceiling(&cs,
						guard_ceiling(rx_posted_total,
							(uint64_t)o.recv_guard,
							(uint64_t)o.count,
							o.recv_keep_posted))) {
						uint64_t now = now_ns();

						grant_pub_note(&gp, now);
						if (pub_publish(&pub, now, 1, 0) < 0) {
							reason = "grant send failed";
							goto recv_finish;
						}
					}
					continue;
				}
				errno = 0;
				int post_ret = post_recv_slot(&o, qp, mr,
							      recv_buf, stride,
							      o.size, held[h]);
				if (post_ret) {
					fprintf(stderr,
						"repost hold: ibv_post_recv[slot=%d] ret=%d errno=%d (grants nothing)\n",
						held[h], post_ret, errno);
					repost_failures++;
					reason = "ibv_post_recv failed after hold";
					goto recv_finish;
				}
				posted++;
				rx_posted_total++;
				data_ts[held[h]] = now_ns();
				wl_on_post(&lat_data, data_ts[held[h]]);
				if (o.credits &&
				    credit_grant_to_ceiling(&cs,
					guard_ceiling(rx_posted_total,
						(uint64_t)o.recv_guard,
						(uint64_t)o.count,
						o.recv_keep_posted))) {
					uint64_t now = now_ns();

					grant_pub_note(&gp, now);
					if (pub_publish(&pub, now, 1, 0) < 0) {
						reason = "grant send failed";
						goto recv_finish;
					}
				}
				fprintf(stderr,
					"repost hold: slot=%d reposted posted=%d granted=%llu\n",
					held[h], posted,
					(unsigned long long)cs.granted);
			}
			held_n = 0;
			fflush(stderr);
			last_progress = now_ns();
		}

recv_finish:
		free(held);
		final_completed = (uint64_t)completed;
		final_status = recv_failed(mismatches, wc_errors, repost_failures,
					   extra_post_failures, completed,
					   o.count, 0) ? 1u : 0u;
		if (o.credits) {
			/* Last publication is best-effort but must still be
			 * attempted: the peer may be waiting on it. */
			(void)pub_publish(&pub, now_ns(), 1,
					  use_uc_credits &&
					  !ctl_credit_avail(&cc));
			(void)cr_out_drain(&out, 1000);
		}
		/*
		 * Strict completion handshake BEFORE the summary, so the
		 * harness's `recv summary:` carries the verdict: a peer that
		 * does not confirm this generation and the full count leaves
		 * the row FAIL, whatever the data said. A run that already
		 * failed sends ABORT instead (below) and the loose exchange in
		 * out_final only logs.
		 */
		if (o.credits && sock >= 0 && cs.ready && !cs.aborted &&
		    !final_status) {
			const char *why = NULL;

			if (credit_final_exchange(sock, &chan, &out, &cs,
						  final_completed, final_status,
						  (uint32_t)posted_initial_recvs,
						  (uint32_t)requested_recvs,
						  (uint64_t)o.count, &why)) {
				final_failed = 1;
				reason = why;
			}
			final_done = 1;
		}
		fprintf(stderr,
			"recv summary: completed=%d/%d mismatches=%d wc_errors=%d repost_failures=%d posted=%d extra_posted=%d extra_post_failures=%d keep_posted=%s guard=%d P=%llu G=%llu grants_issued=%llu grant_frames=%llu data_ok=%s final=%s reason=%s %s\n",
			completed, o.count, mismatches, wc_errors,
			repost_failures, posted, extra_posted,
			extra_post_failures, o.recv_keep_posted ? "on" : "off",
			o.recv_guard, (unsigned long long)rx_posted_total,
			(unsigned long long)cs.granted,
			(unsigned long long)cs.granted,
			(unsigned long long)cs.frames_granted,
			recv_data_ok(mismatches, wc_errors, repost_failures,
				     completed, o.count) ? "yes" : "no",
			!o.credits ? "n/a" : final_done ?
				(final_failed ? "invalid" : "valid") : "skipped",
			reason,
			recv_failed(mismatches, wc_errors, repost_failures,
				    extra_post_failures, completed, o.count,
				    final_failed) ? "FAIL" : "OK");
		if (o.credits)
			fprintf(stderr,
				"recv grants: transport=%s publications=%llu delay_us_mean=%.3f delay_us_min=%.3f delay_us_max=%.3f deadline_forced=%llu blocked=%llu resends=%llu uc_pubs=%llu tcp_pubs=%llu coalesced=%llu partial_writes=%llu\n",
				cr_xport_str(use_uc_credits ? CR_XPORT_UC :
						CR_XPORT_TCP),
				(unsigned long long)gp.pubs,
				gp.pubs ? (double)gp.delay_sum / (double)gp.pubs / 1000.0 : 0.0,
				gp.pubs ? (double)gp.delay_min / 1000.0 : 0.0,
				(double)gp.delay_max / 1000.0,
				(unsigned long long)gp.deadline_pubs,
				(unsigned long long)gp.blocked,
				(unsigned long long)gp.resends,
				(unsigned long long)gp.uc_pubs,
				(unsigned long long)gp.tcp_pubs,
				(unsigned long long)out.coalesced,
				(unsigned long long)out.partial_writes);
		if (use_uc_credits)
			fprintf(stderr,
				"recv control-sq: slots=%d ctl_granted=%llu ctl_used=%llu ctl_window_stalls=%llu ctl_stall_ms=%.3f tcp_escapes=%llu send_posted=%llu send_done=%llu send_fails=%llu send_wc_errors=%llu ctlwin_records=%llu\n",
				ctl_send_slots,
				(unsigned long long)cc.granted,
				(unsigned long long)cc.used,
				(unsigned long long)cc.stalls,
				cc.stall_ns / 1e6,
				(unsigned long long)cc.tcp_escapes,
				(unsigned long long)ctl.send_posted,
				(unsigned long long)ctl.send_done,
				(unsigned long long)ctl.send_fails,
				(unsigned long long)ctl_send_wc_errors,
				(unsigned long long)ctlwin_records);
		wl_print(stderr, "recv", &lat_data);
		if (use_uc_credits)
			wl_print(stderr, "recv", &lat_ctl);
		wl_print_gaps(stderr, &tr);
		fflush(stderr);
		recv_summary_printed = 1;
		if (recv_failed(mismatches, wc_errors, repost_failures,
				extra_post_failures, completed, o.count,
				final_failed)) {
			ret = 1;
			if (o.credits)
				cr_send_abort(sock, &cs, 2);
			goto out_final;
		}
	}

	print_rate(o.role, o.count, o.count, o.size, start, &last_t, &last_done);
	ret = 0;

out_final:
	/* Exchange final counts and status BEFORE either side destroys its QP:
	 * a peer that vanishes mid-stream leaves the other side's posted frames
	 * with nowhere to go, which is how the Apple ring pair gets poisoned. */
	if (o.credits && sock >= 0 && cs.ready && !final_done) {
		/* Only the paths that already failed get here without the
		 * strict exchange above; it still runs so the peer is not left
		 * waiting, and its verdict can only confirm the failure. */
		const char *why = NULL;

		if (credit_final_exchange(sock, &chan, &out, &cs,
					  final_completed, final_status,
					  (uint32_t)posted_initial_recvs,
					  (uint32_t)requested_recvs,
					  (uint64_t)o.count, &why))
			ret = 1;
	}
	if (o.hold_before_destroy_ms > 0) {
		fprintf(stderr, "holding before destroy for %d ms\n",
			o.hold_before_destroy_ms);
		fflush(stderr);
		usleep((useconds_t)o.hold_before_destroy_ms * 1000u);
	}

out_qp:
	/* A receive run that dies before its data phase still owes a summary:
	 * a harness that greps for `recv summary:` must never see silence. */
	if (!is_sender && !is_bidi && !recv_summary_printed) {
		fprintf(stderr,
			"recv summary: completed=0/%d mismatches=0 wc_errors=0 repost_failures=0 posted=%d extra_posted=0 extra_post_failures=0 keep_posted=%s guard=%d P=%llu G=%llu grants_issued=%llu grant_frames=%llu data_ok=no final=skipped reason=%s FAIL\n",
			o.count, posted_initial_recvs,
			o.recv_keep_posted ? "on" : "off", o.recv_guard,
			(unsigned long long)rx_posted_total,
			(unsigned long long)cs.granted,
			(unsigned long long)cs.granted,
			(unsigned long long)cs.frames_granted, early_reason);
		fflush(stderr);
	}
	if (listen_fd >= 0)
		close(listen_fd);
	if (sock >= 0)
		close(sock);
	ibv_destroy_qp(qp);
out_cq:
	ibv_destroy_cq(cq);
out_mr:
	free(seen);
	if (ctl.mr)
		ibv_dereg_mr(ctl.mr);
	free(ctl.send_busy);
	free(ctl.send_ts);
	free(ctl.recv_ts);
	free(data_ts);
	free(data_ids);
	free(ctl_buf);
	if (mr)
		ibv_dereg_mr(mr);
out_buf:
	free(buf);
out_pd:
	ibv_dealloc_pd(pd);
out_ctx:
	ibv_close_device(ctx);
	return ret;
}
