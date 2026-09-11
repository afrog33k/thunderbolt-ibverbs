// SPDX-License-Identifier: GPL-2.0

#include <linux/errno.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/thunderbolt.h>

#include "../proto/native_data.h"
#include "tbv.h"

#define TBV_NATIVE_RING_SIZE 1024
/* Apple-originated bursts can exhaust a 256-entry RX ring before credits
 * recycle. 1024 entries passed checked Mac-to-Linux UC bursts beyond one full
 * ring while keeping per-direction buffer cost modest.
 */
#define TBV_APPLE_RING_SIZE 1024
#define TBV_DATA_FRAME_SIZE SZ_4K
#define TBV_CONTROL_FRAME_SIZE 256
#define TBV_CONTROL_QUEUE_MULTIPLIER 4
#define TBV_DATA_CREDIT_CONTROL_RESERVE 256
#define TBV_DATA_TX_MAX_INFLIGHT 32
#define TBV_TX_POLL_DELAY_MS 1
/*
 * Safety net for the event-driven data-space wait. This is NOT pacing: the
 * waiter is woken the moment a descriptor is handed to the ring, a
 * reservation is released, or admission closes. A firing of this timeout is
 * counted in path.tx_space_wait_timeouts and means a wakeup was missed.
 */
#define TBV_TX_SPACE_WAIT_MS 100
/* Quiesce drain: bounded wait slice between ring-tail reaps. */
#define TBV_TX_REAP_SLICE_MS 50
/*
 * TX completion watchdog cadence and the no-progress interval that counts as
 * a stall. Measured 2026-09-06 (zeus -> Mac, 64 KiB x 2000, d8): 1979/2000
 * completed, then no completion for 5.3 s with 8 WRs in flight and credit in
 * hand, ended only by the bench's RETRY_EXC_ERR; the module logged nothing.
 */
#define TBV_TX_WATCHDOG_MS 500
#define TBV_TX_STALL_WARN_MS 1000
#define TBV_RX_SUPP_POLL_DELAY_MS 1
#define TBV_RX_SUPP_POLL_WINDOW_MS 16
/*
 * Raw-stream zcopy serializes each DMA path, so several QPs sharing a rail can
 * briefly queue a full TX-depth worth of packetized WRs behind one active
 * stream. Keep enough metadata headroom for qps=8/TX-depth=16/1 MiB WRITE and
 * qps=4/TX-depth=128/64 KiB without reporting a false SQ-full error.
 */
#define TBV_DATA_QUEUE_MULTIPLIER 64
#define TBV_DATA_PACKET_POOL_LIMIT 1024

typedef int (*tbv_ring_throttling_fn)(struct tb_ring *ring,
				      unsigned int interval_nsec);

extern int tb_ring_throttling(struct tb_ring *ring,
			      unsigned int interval_nsec);

static uint nhi_interrupt_throttle_ns;
/* module_param_cb() for this lives below tbv_thr_param_ops: writing the sysfs
 * parameter re-programs every live ring when nhi_throttle_direct is set, so the
 * interval can be swept WITHOUT a module reload (unloading with a live verbs
 * client wedges the module in "Unloading" on kernels without forced unload). */
MODULE_PARM_DESC(nhi_interrupt_throttle_ns,
		 "NHI interrupt throttling interval for TBV data rings in ns; 0 disables ring throttling (runtime-writable with nhi_throttle_direct=1)");

static tbv_ring_throttling_fn tbv_ring_throttling;

/*
 * Stock kernels program every NHI MSI-X vector with a fixed 128 us interrupt
 * throttle (nhi_enable_int_throttling) and do not export tb_ring_throttling(),
 * so completions on our rings are reaped at most ~7,800 times a second. On a
 * Strix Halo <-> M4 Max Apple-compat link that is the whole story behind a
 * 65 us typical one-way latency (half the throttle) and a 7,650 WR/s
 * serialized SEND rate. The register is reachable through public fields
 * (tb_ring->nhi->iobase, tb_ring->vector), so with nhi_throttle_direct=1 the
 * module programs it for its own rings' vectors when the helper is missing.
 * Units are 256 ns; 0 disables throttling for that vector.
 */
static bool nhi_throttle_direct;
module_param(nhi_throttle_direct, bool, 0644);
MODULE_PARM_DESC(nhi_throttle_direct,
		 "Program the NHI per-vector interrupt throttle register for TBV rings directly when the kernel lacks tb_ring_throttling(); uses nhi_interrupt_throttle_ns (experiment, default off)");

#define TBV_NHI_REG_INT_THROTTLING_RATE 0x38c00

/*
 * Apple's NHI puts the per-ring interrupt throttle somewhere else entirely.
 * Asahi apple.c: APPLE_CIO_NHI_IRQ_THROTTLE 0xd004c, 256 ns granularity, index
 * apple_cio_ring_index(ring) = ring->hop for TX and ring->hop + n_rings for RX.
 * anhi->nhi.iobase is anhi->nhi_base, and probe refuses to continue unless
 * nhi.hop_count == n_rings, so ring->nhi->hop_count is that n_rings.
 *
 * Read-only here, and for a reason: apple_nhi_ring_interrupt_active() SKIPS the
 * throttle write when ring->interval_nsec is zero, so a zero module parameter
 * does not prove the register is zero -- it proves nothing was written. Only a
 * readback settles it.
 */
#define TBV_APPLE_NHI_IRQ_THROTTLE 0xd004c
#define TBV_APPLE_NHI_IRQ_THROTTLE_GRANULARITY_NSEC 256
/* Asahi apple_nhi_ring_layout: TX descriptors at 0x10000, stride 0x4000. */
#define TBV_APPLE_NHI_TXRING_DESC_BASE 0x10000
#define TBV_APPLE_NHI_RING_STRIDE 0x4000

static bool tbv_path_is_apple(const struct tbv_path *path);

static int tbv_path_ring_throttling_direct(struct tb_ring *ring,
					   unsigned int interval_nsec)
{
	u32 throttle = DIV_ROUND_UP(interval_nsec, 256);

	if (!ring || !ring->nhi || !ring->nhi->iobase)
		return -ENODEV;
	if (ring->vector < 0 || ring->vector >= 16)
		return -EINVAL;
	iowrite32(throttle, ring->nhi->iobase +
		  TBV_NHI_REG_INT_THROTTLING_RATE + ring->vector * 4);
	pr_info("direct NHI throttle: vector=%d interval=%u ns (reg=%u)\n",
		ring->vector, interval_nsec, throttle);
	return 0;
}

/* Rings whose throttle we own, so a runtime parameter write can re-program them. */
#define TBV_THR_MAX_RINGS 16
static struct tb_ring *tbv_thr_rings[TBV_THR_MAX_RINGS];
static DEFINE_SPINLOCK(tbv_thr_lock);

static void tbv_thr_add(struct tb_ring *ring)
{
	unsigned long flags;
	int i;

	if (!ring)
		return;
	spin_lock_irqsave(&tbv_thr_lock, flags);
	for (i = 0; i < TBV_THR_MAX_RINGS; i++) {
		if (tbv_thr_rings[i] == ring)
			break;
		if (!tbv_thr_rings[i]) {
			tbv_thr_rings[i] = ring;
			break;
		}
	}
	spin_unlock_irqrestore(&tbv_thr_lock, flags);
}

static void tbv_thr_del(struct tb_ring *ring)
{
	unsigned long flags;
	int i;

	if (!ring)
		return;
	spin_lock_irqsave(&tbv_thr_lock, flags);
	for (i = 0; i < TBV_THR_MAX_RINGS; i++)
		if (tbv_thr_rings[i] == ring)
			tbv_thr_rings[i] = NULL;
	spin_unlock_irqrestore(&tbv_thr_lock, flags);
}

/*
 * Optional separate interval for the RX rings. 0 = follow nhi_interrupt_throttle_ns.
 * Lets an experiment run the TX vector fast (pacing/latency toward the peer) while
 * keeping the RX vector at another value, to tell which side a loss belongs to.
 */
static uint nhi_rx_throttle_ns;

static int tbv_thr_program(bool want_tx, unsigned int interval)
{
	unsigned long flags;
	int i, n = 0;

	spin_lock_irqsave(&tbv_thr_lock, flags);
	for (i = 0; i < TBV_THR_MAX_RINGS; i++) {
		struct tb_ring *ring = tbv_thr_rings[i];

		if (ring && ring->is_tx == want_tx && ring->nhi &&
		    ring->nhi->iobase && ring->vector >= 0 && ring->vector < 16) {
			iowrite32(DIV_ROUND_UP(interval, 256), ring->nhi->iobase +
				  TBV_NHI_REG_INT_THROTTLING_RATE + ring->vector * 4);
			n++;
		}
	}
	spin_unlock_irqrestore(&tbv_thr_lock, flags);
	return n;
}

static int tbv_thr_param_set(const char *val, const struct kernel_param *kp)
{
	unsigned int interval;
	int ret, ntx, nrx = 0;

	ret = kstrtouint(val, 0, &interval);
	if (ret)
		return ret;
	*(uint *)kp->arg = interval;
	if (!READ_ONCE(nhi_throttle_direct))
		return 0;
	ntx = tbv_thr_program(true, interval);
	if (!READ_ONCE(nhi_rx_throttle_ns))
		nrx = tbv_thr_program(false, interval);
	pr_info("direct NHI throttle: runtime set interval=%u ns on %d tx + %d rx ring(s)\n",
		interval, ntx, nrx);
	return 0;
}

static int tbv_thr_rx_param_set(const char *val, const struct kernel_param *kp)
{
	unsigned int interval;
	int ret, nrx;

	ret = kstrtouint(val, 0, &interval);
	if (ret)
		return ret;
	*(uint *)kp->arg = interval;
	if (!READ_ONCE(nhi_throttle_direct))
		return 0;
	nrx = tbv_thr_program(false, interval ? interval :
			      READ_ONCE(nhi_interrupt_throttle_ns));
	pr_info("direct NHI throttle: runtime set RX interval=%u ns on %d rx ring(s)\n",
		interval ? interval : READ_ONCE(nhi_interrupt_throttle_ns), nrx);
	return 0;
}

static const struct kernel_param_ops tbv_thr_param_ops = {
	.set = tbv_thr_param_set,
	.get = param_get_uint,
};
module_param_cb(nhi_interrupt_throttle_ns, &tbv_thr_param_ops,
		&nhi_interrupt_throttle_ns, 0644);

static const struct kernel_param_ops tbv_thr_rx_param_ops = {
	.set = tbv_thr_rx_param_set,
	.get = param_get_uint,
};
module_param_cb(nhi_rx_throttle_ns, &tbv_thr_rx_param_ops,
		&nhi_rx_throttle_ns, 0644);
MODULE_PARM_DESC(nhi_rx_throttle_ns,
		 "Separate NHI interrupt throttle for TBV RX rings in ns (runtime, needs nhi_throttle_direct=1); 0 follows nhi_interrupt_throttle_ns");

/*
 * Read one ring's Apple interrupt-throttle register. Read-only, and only on an
 * Apple path: on any other controller this offset means something else.
 */
static void tbv_path_apple_throttle_show(struct seq_file *s, const char *what,
					 struct tb_ring *ring)
{
	unsigned int idx;
	u32 raw;

	if (!ring || !ring->nhi || !ring->nhi->iobase) {
		seq_printf(s, " %s=<no-ring>", what);
		return;
	}

	idx = ring->is_tx ? (unsigned int)ring->hop :
			    (unsigned int)ring->hop + ring->nhi->hop_count;
	raw = ioread32(ring->nhi->iobase + TBV_APPLE_NHI_IRQ_THROTTLE + 4 * idx);
	seq_printf(s, " %s[idx=%u hop=%d]=raw:%u ns:%u req_ns:%u", what, idx,
		   ring->hop, raw,
		   raw * TBV_APPLE_NHI_IRQ_THROTTLE_GRANULARITY_NSEC,
		   ring->interval_nsec);
}

void tbv_path_show_apple_throttle(struct seq_file *s, struct tbv_path *path)
{
	if (!tbv_path_is_apple(path))
		return;

	seq_printf(s, "    apple_irq_throttle base=0x%x gran_ns=%u",
		   TBV_APPLE_NHI_IRQ_THROTTLE,
		   TBV_APPLE_NHI_IRQ_THROTTLE_GRANULARITY_NSEC);
	tbv_path_apple_throttle_show(s, "tx", READ_ONCE(path->tx_ring));
	tbv_path_apple_throttle_show(s, "rx", READ_ONCE(path->rx_ring));
	/*
	 * req_ns is tb_ring.interval_nsec, the value the driver was asked for.
	 * apple_nhi_ring_interrupt_active() skips the register write when it is
	 * zero, so req_ns=0 with raw=0 means "never programmed", NOT "throttle
	 * proven disabled" -- the register simply keeps whatever it held.
	 */
	seq_puts(s,
		 " (req_ns=0 skips the write: raw is then whatever the register already held, not a proof of 0)\n");
}

void tbv_path_init_optional_symbols(void)
{
	tbv_ring_throttling = symbol_get(tb_ring_throttling);
	if (tbv_ring_throttling)
		pr_info("using optional tb_ring_throttling() helper\n");
	else
		pr_info("optional tb_ring_throttling() helper unavailable; using stock NHI interrupt throttling\n");
}

void tbv_path_exit_optional_symbols(void)
{
	if (!tbv_ring_throttling)
		return;

	symbol_put(tb_ring_throttling);
	tbv_ring_throttling = NULL;
}

static bool apple_tx_raw_mode;
module_param(apple_tx_raw_mode, bool, 0644);
MODULE_PARM_DESC(apple_tx_raw_mode,
		 "Use RAW descriptors for Apple-compatible TX rings; default keeps FRAME descriptors");

static bool apple_tx_e2e;
module_param(apple_tx_e2e, bool, 0644);
MODULE_PARM_DESC(apple_tx_e2e,
		 "Enable E2E flow control on Apple-compatible TX rings");

static bool apple_rx_raw_mode;
module_param(apple_rx_raw_mode, bool, 0644);
MODULE_PARM_DESC(apple_rx_raw_mode,
		 "Compatibility no-op: Apple RAW RX is disabled because raw descriptor boundaries are not yet message-safe");

static uint apple_tx_stall_fail_ms = 5000;
module_param(apple_tx_stall_fail_ms, uint, 0644);
MODULE_PARM_DESC(apple_tx_stall_fail_ms,
		 "Apple-compatible TX path: no completion for this many ms with descriptors outstanding fails the connection (error CQEs, ring retirement barrier, rail quarantined); 0 warns only");

/*
 * Supplemental TX completion polling. -1 = auto (default, unchanged: native
 * paths only, never Apple), 0 = off everywhere, 1 = on everywhere. The poll
 * re-arms itself through a 1 ms jiffies delay, so it must never be the timely
 * completion source on a path whose interrupts work; the lever exists so that
 * claim can be settled by experiment without a rebuild.
 */
static int tx_progress_poll = -1;
module_param(tx_progress_poll, int, 0644);
MODULE_PARM_DESC(tx_progress_poll,
		 "Supplemental 1 ms TX completion polling: -1 auto (native only, default), 0 off, 1 on. Takes effect at the next path start; peers reports the resolved value as tx_poll enabled=");

/*
 * Placement of the TX post path.
 *
 * What can be placed and what cannot, from the code:
 *
 *  - The REAP and CQ chain cannot be placed at all. The Apple NHI IRQ handler
 *    (Asahi apple.c apple_cio_ring_irq) does schedule_work(&ring->work), so
 *    ring_work() -- and therefore tbv_path_tx_complete() and the CQ push --
 *    runs on system_percpu_wq on whatever CPU AIC2 delivered the interrupt to.
 *    AIC2 has no per-IRQ steering: /proc/irq/N/smp_affinity_list is not
 *    writable on this machine ("Operation not permitted"). Ring poll mode
 *    (tb_ring's start_poll) would hand us the reap, but tb_ring_poll() never
 *    calls ring_write_descriptors() -- only ring_work() and __tb_ring_enqueue()
 *    do -- so a TX ring that filled would stall until the next enqueue. That is
 *    not an acceptable data path, so poll mode is deliberately NOT used here.
 *    peers reports tx_last_cb_cpu so where the interrupt lands is at least
 *    observable.
 *
 *  - The POST path can be placed: tqp->apple_sq_work does the payload alloc,
 *    the per-frame copy, the per-frame doorbell MMIO and the group waits.
 *    Today it runs on state->workqueue, which is WQ_UNBOUND | WQ_HIGHPRI --
 *    already high priority, so raising priority again would not be a change.
 *    apply_workqueue_attrs() and alloc_workqueue_attrs() are not exported, so
 *    an unbound workqueue cannot be pinned from a module; a BOUND (per-CPU)
 *    WQ_HIGHPRI workqueue driven with queue_work_on() can.
 *
 * zeus topology for tx_worker_cpu: E-cores 0-3 and 12-15 (max 2.42 GHz),
 * P-cores 4-11 and 16-23 (max 3.26 GHz, idle 702 MHz).
 */
static bool tx_worker_dedicated;
module_param(tx_worker_dedicated, bool, 0644);
MODULE_PARM_DESC(tx_worker_dedicated,
		 "Run the Apple SQ post worker on a dedicated per-rail bounded WQ_HIGHPRI workqueue instead of the shared unbound device workqueue; default off (unchanged). Implied by tx_worker_cpu >= 0. Takes effect at the next path start");

static int tx_worker_cpu = -1;
module_param(tx_worker_cpu, int, 0644);
MODULE_PARM_DESC(tx_worker_cpu,
		 "Pin the dedicated Apple SQ post worker to this CPU; -1 = unset (default, unchanged). zeus: E-cores 0-3,12-15 (2.42 GHz max), P-cores 4-11,16-23 (3.26 GHz max, 702 MHz idle). This binds OUR worker only -- the ring interrupt and therefore the reap/CQ chain land wherever AIC2 puts them and cannot be steered");

/*
 * T2 of the stall ladder. Off by default: re-announcing the producer index is
 * an experiment, not a documented recovery. It is admissible only because it
 * is idempotent (see tbv_path_tx_rekick_producer), and it is counted so we can
 * measure whether it ever precedes a real recovery instead of believing it.
 */
static bool apple_tx_stall_rekick;
module_param(apple_tx_stall_rekick, bool, 0644);
MODULE_PARM_DESC(apple_tx_stall_rekick,
		 "Stall ladder T2 (EXPERIMENT, default off): once per stall episode, re-announce the TX ring producer index. Idempotent -- it re-writes the index the hardware should already hold, so it cannot post, reorder or duplicate a descriptor. peers reports tx_stall_recovered_by rekick=");

static uint apple_tx_stall_rekick_ms = 2000;
module_param(apple_tx_stall_rekick_ms, uint, 0644);
MODULE_PARM_DESC(apple_tx_stall_rekick_ms,
		 "Milliseconds without a TX retirement before the T2 producer re-kick fires; must sit between the 1000 ms warn and apple_tx_stall_fail_ms. 0 disables T2");

static uint native_tx_max_inflight = TBV_DATA_TX_MAX_INFLIGHT;
module_param(native_tx_max_inflight, uint, 0644);
MODULE_PARM_DESC(native_tx_max_inflight,
		 "Maximum native data TX descriptors posted per path before waiting for completions; 0 disables native throttling");

struct tbv_data_frame {
	struct ring_frame frame;
	struct tbv_path *path;
	struct list_head free_node;
	void *buf;
	dma_addr_t dma;
	struct tbv_tx_packet *packet;
	tbv_path_tx_done_fn done;
	void *done_ctx;
	bool tx;
};

struct tbv_tx_packet {
	struct list_head node;
	struct tbv_path *path;
	u8 *buf;
	u32 len;
	struct ring_frame frame;
	dma_addr_t dma;
	tbv_path_tx_done_fn done;
	void *done_ctx;
	void *owner_ctx;
	u8 sof;
	u8 eof;
	u32 start_credit_group_frames;
	unsigned long queued_jiffies;
	bool control;
	bool pooled;
	bool queued;
	bool inflight;
	bool zcopy;
	bool unmap_dma;
	bool raw_stream_start;
	bool raw_stream_end;
	bool raw_stream_counted;
	/*
	 * Logically canceled by its owner while its descriptor was already
	 * submitted to the NHI. Ownership (done/done_ctx and the reference
	 * behind them) stays with the frame until the ring retires it — at
	 * completion or at a tb_ring_stop() barrier — and the retirement then
	 * reports -ECANCELED. See tbv_path_cancel_data_match().
	 */
	bool canceled;
	u8 control_buf[TBV_CONTROL_FRAME_SIZE];
};

static u32 tbv_frame_len(const struct ring_frame *frame)
{
	return frame->size ? frame->size : (u32)TBV_DATA_FRAME_SIZE;
}

static struct tbv_state *tbv_path_state(struct tbv_path *path)
{
	return path->rail && path->rail->peer ? path->rail->peer->state : NULL;
}

static u32 tbv_path_control_packet_count(const struct tbv_path *path)
{
	u32 count = path->cfg.tx_ring_size * TBV_CONTROL_QUEUE_MULTIPLIER;

	return clamp_t(u32, count, 64, 4096);
}

static u32 tbv_path_tx_inflight_limit(const struct tbv_path *path)
{
	u32 limit = TBV_DATA_TX_MAX_INFLIGHT;

	if (path->rail && path->rail->peer &&
	    path->rail->peer->backend == TBV_BACKEND_NATIVE)
		limit = READ_ONCE(native_tx_max_inflight);
	if (!limit)
		return 0;

	return clamp_t(u32, limit, 1, path->cfg.tx_ring_size);
}

static u32 tbv_path_data_packet_count(const struct tbv_path *path)
{
	u32 count = path->cfg.tx_ring_size * TBV_DATA_QUEUE_MULTIPLIER;

	return min_t(u32, count, TBV_DATA_PACKET_POOL_LIMIT);
}

static u32 tbv_path_tx_control_frame_reserve(const struct tbv_path *path)
{
	u32 reserve;

	if (path->tx_frame_count <= 1)
		return 0;

	reserve = path->tx_frame_count / 4;
	return clamp_t(u32, reserve, 1, TBV_DATA_CREDIT_CONTROL_RESERVE);
}

static int tbv_path_configure_ring_throttling(struct tbv_path *path)
{
	u32 interval = READ_ONCE(nhi_interrupt_throttle_ns);
	int ret;

	if (!tbv_ring_throttling) {
		/*
		 * nhi_throttle_direct writes Intel's 0x38c00 + 4*vector. On the
		 * Apple NHI the throttle lives at 0xd004c + 4*ring_index, so
		 * that write would land on an unrelated register. Refuse it
		 * rather than corrupt the controller; peers reports the real
		 * Apple throttle by readback instead.
		 */
		if (READ_ONCE(nhi_throttle_direct) && tbv_path_is_apple(path)) {
			pr_warn_once("nhi_throttle_direct=1 refused on the Apple-compatible path: it programs Intel's 0x%x+4*vector, while Apple's throttle is 0x%x+4*ring_index; see the throttle readback in peers\n",
				     TBV_NHI_REG_INT_THROTTLING_RATE,
				     TBV_APPLE_NHI_IRQ_THROTTLE);
			return 0;
		}
		if (READ_ONCE(nhi_throttle_direct)) {
			tbv_thr_add(path->tx_ring);
			tbv_thr_add(path->rx_ring);
		}
		if (interval && READ_ONCE(nhi_throttle_direct)) {
			ret = tbv_path_ring_throttling_direct(path->tx_ring,
							      interval);
			if (ret)
				return ret;
			return tbv_path_ring_throttling_direct(path->rx_ring,
							       interval);
		}
		if (interval)
			pr_warn_once("nhi_interrupt_throttle_ns requires a kernel exporting tb_ring_throttling(); ignoring interval %u ns\n",
				     interval);
		return 0;
	}

	ret = tbv_ring_throttling(path->tx_ring, interval);
	if (ret) {
		pr_warn("TX ring throttling interval %u ns failed ret=%d\n",
			interval, ret);
		return ret;
	}

	ret = tbv_ring_throttling(path->rx_ring, interval);
	if (ret) {
		pr_warn("RX ring throttling interval %u ns failed ret=%d\n",
			interval, ret);
		return ret;
	}

	return 0;
}

static void tbv_path_tx_packet_release(struct tbv_tx_packet *packet, int status)
{
	struct tbv_path *path = packet->path;
	unsigned long flags;

	if (packet->zcopy && packet->unmap_dma) {
		struct device *dma_dev = tb_ring_dma_device(path->tx_ring);

		if (tbv_dma_device_ready(dma_dev))
			dma_unmap_page(dma_dev, packet->dma, packet->len,
				       DMA_TO_DEVICE);
		else
			pr_warn_ratelimited("TX ring DMA device is not ready for zcopy unmapping\n");
	}

	if (packet->done)
		packet->done(packet->done_ctx, status);

	packet->done = NULL;
	packet->done_ctx = NULL;
	packet->owner_ctx = NULL;
	packet->len = 0;
	packet->start_credit_group_frames = 0;
	packet->queued_jiffies = 0;
	packet->queued = false;
	packet->inflight = false;

	if (packet->zcopy) {
		kfree(packet);
		return;
	}

	if (!packet->control && packet->pooled) {
		spin_lock_irqsave(&path->tx_lock, flags);
		list_add_tail(&packet->node, &path->tx_data_free);
		spin_unlock_irqrestore(&path->tx_lock, flags);
		return;
	}

	if (!packet->control) {
		kfree(packet->buf);
		kfree(packet);
		return;
	}
	if (!packet->pooled) {
		kfree(packet);
		return;
	}

	packet->buf = packet->control_buf;
	spin_lock_irqsave(&path->tx_lock, flags);
	list_add_tail(&packet->node, &path->tx_control_free);
	spin_unlock_irqrestore(&path->tx_lock, flags);
}

static void tbv_path_schedule_tx(struct tbv_path *path);
static bool tbv_path_tx_admits_locked(const struct tbv_path *path);
static void tbv_path_tx_poll_work(struct work_struct *work);
static void tbv_path_rx_supp_poll_work(struct work_struct *work);
static void tbv_path_tx_watchdog_work(struct work_struct *work);

static bool tbv_path_progress_poll_enabled(const struct tbv_path *path)
{
	int mode = READ_ONCE(tx_progress_poll);

	if (!path->rail || !path->rail->peer)
		return false;

	if (mode == 0)
		return false;
	if (mode > 0)
		return true;

	/*
	 * Auto (default, unchanged): Apple FA57 has no transport-level ACK.
	 * Early local TX polling can open the verbs SQ window before macOS has
	 * consumed the previous SEND group, so the supplemental 1 ms
	 * jiffies-granular poll (TBV_TX_POLL_DELAY_MS) is NEVER on the Apple
	 * data path -- normal NHI TX callbacks carry Apple completions. It is
	 * reserved for native, where a missed notification has no other
	 * recovery. peers reports the resolved value as "tx_poll enabled=".
	 */
	return path->rail->peer->backend == TBV_BACKEND_NATIVE;
}

static void tbv_path_queue_delayed_work(struct tbv_path *path,
					struct delayed_work *work,
					unsigned long delay)
{
	struct tbv_state *state = tbv_path_state(path);

	if (state && state->workqueue)
		queue_delayed_work(state->workqueue, work, delay);
	else
		schedule_delayed_work(work, delay);
}

static void tbv_path_queue_tx_poll(struct tbv_path *path, unsigned long delay)
{
	if (!path->tx_poll_enabled || !path->tx_ring)
		return;

	tbv_path_queue_delayed_work(path, &path->tx_poll_work, delay);
}

/* Reaped descriptors run tbv_path_tx_complete() and are counted there. */
static u64 tbv_path_tx_retired_total(const struct tbv_path *path)
{
	return atomic64_read(&path->data_tx_completed) +
	       atomic64_read(&path->control_tx_completed) +
	       atomic64_read(&path->data_tx_canceled);
}

/* One pending-bit test per posted frame; the work re-arms itself. */
static void tbv_path_arm_tx_watchdog(struct tbv_path *path)
{
	if (delayed_work_pending(&path->tx_watchdog_work))
		return;
	path->tx_watchdog_last_retired = tbv_path_tx_retired_total(path);
	path->tx_watchdog_last_progress = jiffies;
	tbv_path_queue_delayed_work(path, &path->tx_watchdog_work,
				    msecs_to_jiffies(TBV_TX_WATCHDOG_MS));
}

static void tbv_path_queue_rx_supp_poll(struct tbv_path *path,
					unsigned long delay)
{
	if (!path->rx_supp_poll_enabled || !path->rx_ring)
		return;

	WRITE_ONCE(path->rx_supp_poll_until,
		   jiffies + msecs_to_jiffies(TBV_RX_SUPP_POLL_WINDOW_MS));
	tbv_path_queue_delayed_work(path, &path->rx_supp_poll_work, delay);
}

static void tbv_path_atomic64_max(atomic64_t *counter, u64 value)
{
	s64 old;

	if (value > S64_MAX)
		value = S64_MAX;

	for (;;) {
		old = atomic64_read(counter);
		if (old >= (s64)value)
			return;
		if (atomic64_cmpxchg(counter, old, (s64)value) == old)
			return;
	}
}

static u32 tbv_path_data_credit_window(u32 rx_ring_size)
{
	u32 credits;

	if (!rx_ring_size)
		return 0;

	if (rx_ring_size <= TBV_DATA_CREDIT_CONTROL_RESERVE)
		credits = rx_ring_size / 2;
	else
		credits = rx_ring_size - TBV_DATA_CREDIT_CONTROL_RESERVE;

	if (credits > TBV_NATIVE_DATA_CREDIT_BATCH)
		credits -= credits % TBV_NATIVE_DATA_CREDIT_BATCH;
	if (!credits)
		credits = 1;

	return credits;
}

void tbv_path_set_remote_rx_capacity(struct tbv_path *path, u32 rx_ring_size)
{
	unsigned long flags;
	u32 credits;

	if (!path)
		return;

	credits = tbv_path_data_credit_window(rx_ring_size);
	spin_lock_irqsave(&path->tx_lock, flags);
	path->tx_remote_data_credit_max = credits;
	path->tx_remote_data_credits = credits;
	path->rx_data_credit_pending = 0;
	spin_unlock_irqrestore(&path->tx_lock, flags);

	tbv_path_schedule_tx(path);
}

void tbv_path_add_remote_rx_credits(struct tbv_path *path, u32 credits)
{
	struct tbv_state *state;
	unsigned long flags;
	u32 accepted = 0;
	u32 old;
	u32 new;

	if (!path || !credits)
		return;

	state = tbv_path_state(path);
	spin_lock_irqsave(&path->tx_lock, flags);
	if (path->tx_remote_data_credit_max) {
		old = path->tx_remote_data_credits;
		if (old >= path->tx_remote_data_credit_max)
			new = path->tx_remote_data_credit_max;
		else if (credits > path->tx_remote_data_credit_max - old)
			new = path->tx_remote_data_credit_max;
		else
			new = old + credits;
		path->tx_remote_data_credits = new;
		accepted = new - old;
	}
	spin_unlock_irqrestore(&path->tx_lock, flags);

	if (!accepted)
		return;

	if (state)
		atomic64_add(accepted, &state->data_tx_credit_received);
	atomic64_add(accepted, &path->data_tx_credit_received);
	tbv_path_schedule_tx(path);
}

static int tbv_path_send_rx_credit(struct tbv_path *path, u32 credits)
{
	struct tbv_native_data_header hdr = {};
	u8 frame[TBV_NATIVE_DATA_HDR_SIZE];
	int len;

	hdr.opcode = TBV_NATIVE_DATA_OP_PATH_CREDIT;
	hdr.imm_data = credits;

	len = tbv_native_data_build_header(frame, sizeof(frame), &hdr);
	if (len < 0)
		return len;

	return tbv_path_send(path, frame, len, TBV_PATH_SEND_CONTROL, NULL, NULL);
}

static void tbv_path_return_rx_data_credit(struct tbv_path *path, u32 credits)
{
	struct tbv_state *state;
	unsigned long flags;
	u32 threshold;
	u32 pending;
	u32 send = 0;
	int ret;

	if (!path || !credits)
		return;

	state = tbv_path_state(path);
	threshold = tbv_native_data_credit_return_threshold(
		tbv_path_data_credit_window(path->cfg.rx_ring_size));
	spin_lock_irqsave(&path->tx_lock, flags);
	pending = path->rx_data_credit_pending;
	if (credits > U32_MAX - pending)
		pending = U32_MAX;
	else
		pending += credits;
	if (pending >= threshold) {
		send = pending;
		pending = 0;
	}
	path->rx_data_credit_pending = pending;
	spin_unlock_irqrestore(&path->tx_lock, flags);

	if (!send)
		return;

	ret = tbv_path_send_rx_credit(path, send);
	if (ret) {
		spin_lock_irqsave(&path->tx_lock, flags);
		pending = path->rx_data_credit_pending;
		if (send > U32_MAX - pending)
			path->rx_data_credit_pending = U32_MAX;
		else
			path->rx_data_credit_pending = pending + send;
		spin_unlock_irqrestore(&path->tx_lock, flags);
		if (state)
			atomic64_inc(&state->data_rx_credit_send_error);
		atomic64_inc(&path->data_rx_credit_send_error);
		return;
	}

	if (state)
		atomic64_add(send, &state->data_rx_credit_sent);
	atomic64_add(send, &path->data_rx_credit_sent);
}

static bool tbv_native_data_consumes_rx_credit(u8 opcode)
{
	switch (opcode) {
	case TBV_NATIVE_DATA_OP_SEND:
	case TBV_NATIVE_DATA_OP_SEND_IMM:
	case TBV_NATIVE_DATA_OP_RDMA_WRITE:
	case TBV_NATIVE_DATA_OP_RDMA_WRITE_IMM:
	case TBV_NATIVE_DATA_OP_RDMA_READ_REQ:
	case TBV_NATIVE_DATA_OP_RDMA_READ_RESP:
	case TBV_NATIVE_DATA_OP_MAD:
		return true;
	default:
		return false;
	}
}

static bool
tbv_native_data_valid_path_credit(const struct tbv_native_data_header *hdr)
{
	return hdr->opcode == TBV_NATIVE_DATA_OP_PATH_CREDIT &&
	       hdr->imm_data &&
	       !hdr->flags &&
	       !hdr->dest_qp &&
	       !hdr->src_qp &&
	       !hdr->psn &&
	       !hdr->length &&
	       !hdr->remote_addr &&
	       !hdr->rkey;
}

static void tbv_path_count_raw_stream_locked(struct tbv_path *path,
					     struct tbv_tx_packet *packet)
{
	if (packet->raw_stream_start) {
		path->tx_raw_stream_active = true;
		path->tx_raw_stream_owner = packet->owner_ctx;
		path->tx_raw_stream_inflight = 0;
		path->tx_raw_stream_end_seen = false;
	}
	if (path->tx_raw_stream_active &&
	    packet->owner_ctx == path->tx_raw_stream_owner) {
		path->tx_raw_stream_inflight++;
		packet->raw_stream_counted = true;
	}
}

static void tbv_path_finish_raw_stream_if_needed(struct tbv_path *path,
						 struct tbv_tx_packet *packet)
{
	unsigned long flags;

	if (!packet || !packet->raw_stream_counted)
		return;

	spin_lock_irqsave(&path->tx_lock, flags);
	if (path->tx_raw_stream_owner == packet->owner_ctx) {
		if (packet->raw_stream_end)
			path->tx_raw_stream_end_seen = true;
		if (path->tx_raw_stream_inflight)
			path->tx_raw_stream_inflight--;
		if (path->tx_raw_stream_end_seen &&
		    !path->tx_raw_stream_inflight) {
			path->tx_raw_stream_active = false;
			path->tx_raw_stream_owner = NULL;
			path->tx_raw_stream_end_seen = false;
		}
	}
	packet->raw_stream_counted = false;
	if (!path->tx_raw_stream_active) {
		path->tx_raw_stream_active = false;
		path->tx_raw_stream_owner = NULL;
		path->tx_raw_stream_end_seen = false;
		path->tx_raw_stream_inflight = 0;
	}
	spin_unlock_irqrestore(&path->tx_lock, flags);
}

bool tbv_path_apple_tx_raw_mode(void)
{
	return READ_ONCE(apple_tx_raw_mode);
}

bool tbv_path_apple_rx_raw_mode(void)
{
	if (READ_ONCE(apple_rx_raw_mode))
		pr_warn_once("apple_rx_raw_mode is ignored: Apple RAW RX descriptor boundaries are not message-safe\n");
	return false;
}

void tbv_path_default_config(enum tbv_backend_type backend,
			     struct tbv_path_config *cfg)
{
	memset(cfg, 0, sizeof(*cfg));
	cfg->tx_hop = -1;
	cfg->rx_hop = -1;
	cfg->transmit_path = -1;
	cfg->receive_path = -1;

	switch (backend) {
	case TBV_BACKEND_APPLE:
		cfg->tx_ring_size = TBV_APPLE_RING_SIZE;
		cfg->rx_ring_size = TBV_APPLE_RING_SIZE;
		cfg->tx_flags = 0;
		if (!tbv_path_apple_tx_raw_mode())
			cfg->tx_flags |= RING_FLAG_FRAME;
		if (READ_ONCE(apple_tx_e2e))
			cfg->tx_flags |= RING_FLAG_E2E;
		cfg->rx_flags = RING_FLAG_E2E;
		if (!tbv_path_apple_rx_raw_mode())
			cfg->rx_flags |= RING_FLAG_FRAME;
		cfg->tx_hop = 2;
		cfg->rx_hop = 2;
		cfg->transmit_path = 9;
		cfg->receive_path = 9;
		if (tbv_path_apple_rx_raw_mode()) {
			cfg->sof_mask = 0xffff;
			cfg->eof_mask = 0xffff;
		} else {
			cfg->sof_mask = BIT(1);
			cfg->eof_mask = BIT(2) | BIT(3);
		}
		cfg->e2e = true;
		break;

	case TBV_BACKEND_NATIVE:
	default:
		cfg->tx_ring_size = TBV_NATIVE_RING_SIZE;
		cfg->rx_ring_size = TBV_NATIVE_RING_SIZE;
		cfg->tx_flags = RING_FLAG_FRAME;
		cfg->rx_flags = RING_FLAG_FRAME;
		cfg->sof_mask = BIT(1);
		cfg->eof_mask = BIT(2) | BIT(3);
		cfg->e2e = false;
		break;
	}
}

static void tbv_path_init_common(struct tbv_path *path)
{
	spin_lock_init(&path->tx_lock);
	INIT_LIST_HEAD(&path->tx_free);
	INIT_LIST_HEAD(&path->tx_control_free);
	INIT_LIST_HEAD(&path->tx_data_free);
	INIT_LIST_HEAD(&path->tx_control_queue);
	INIT_LIST_HEAD(&path->tx_data_queue);
	INIT_LIST_HEAD(&path->tx_zcopy_inflight);
	INIT_DELAYED_WORK(&path->tx_poll_work, tbv_path_tx_poll_work);
	INIT_DELAYED_WORK(&path->rx_supp_poll_work,
			  tbv_path_rx_supp_poll_work);
	INIT_DELAYED_WORK(&path->tx_watchdog_work, tbv_path_tx_watchdog_work);
	init_waitqueue_head(&path->tx_retire_wait);
	init_waitqueue_head(&path->tx_space_wait);
	atomic_set(&path->tx_inflight, 0);
	path->local_transmit_path = -1;
	path->local_tx_hop = -1;
	path->local_rx_hop = -1;
	path->remote_transmit_path = -1;
}

static bool tbv_path_is_apple(const struct tbv_path *path)
{
	return path->rail && path->rail->peer &&
	       path->rail->peer->backend == TBV_BACKEND_APPLE;
}

static void tbv_path_log_tx_stall(struct tbv_path *path, const char *level,
				  int inflight, unsigned int stalled_ms)
{
	struct tbv_path_ring_snapshot snap;

	tbv_path_tx_ring_snapshot(path, &snap);
	pr_warn("TX stall %s on hop %d (rail %u): %d frames unretired, no completion for %u ms; ring running=%u head=%d tail=%d hw_cons=%d size=%d outstanding=%d sw_queued=%d tail_flags=0x%03x tail_completed=%u ring_flags=0x%x e2e=%u e2e_tx_hop=%d hw_options=0x%08x (tail_completed 0 = hardware/peer has not finished the oldest descriptor, 1 = completion notification missed)\n",
		level, path->local_tx_hop,
		path->rail ? path->rail->rail_id : U32_MAX, inflight, stalled_ms,
		snap.running, snap.head, snap.tail, snap.hw_index, snap.size,
		snap.outstanding, snap.sw_queued, snap.tail_flags,
		snap.tail_completed, snap.ring_flags,
		!!(snap.ring_flags & RING_FLAG_E2E), snap.e2e_tx_hop,
		snap.hw_options);
}

/*
 * A warned stall whose completions resumed on their own. The duration is
 * measured from the last retirement before the gap to the first retirement
 * after it (tx_last_retire is written per completion), not from the 500 ms
 * watchdog tick, so the distribution in peers is the peer's credit-return
 * gap itself. Measured 2026-09-06: 0.7 s and ~1-2 s recoveries alongside the
 * >5 s ones that end in a WR timeout.
 */
static const char *const tbv_tx_stall_recovery_names[] = {
	[TBV_TX_STALL_RECOVERY_PEER] = "peer",
	[TBV_TX_STALL_RECOVERY_REAP] = "reap",
	[TBV_TX_STALL_RECOVERY_REKICK] = "rekick",
	[TBV_TX_STALL_RECOVERY_TIMEOUT] = "timeout",
};

static void tbv_path_tx_stall_recovered(struct tbv_path *path,
					enum tbv_tx_stall_recovery by)
{
	unsigned long began = path->tx_watchdog_last_progress;
	unsigned long ended = READ_ONCE(path->tx_last_retire);
	unsigned int ms = time_after(ended, began) ?
				  jiffies_to_msecs(ended - began) :
				  jiffies_to_msecs(jiffies - began);

	path->tx_stalled = false;
	path->tx_stall_rekicked = false;
	path->tx_stall_recovered++;
	path->tx_stall_recovered_by[by]++;
	path->tx_stall_total_ms += ms;
	if (ms > path->tx_stall_longest_ms)
		path->tx_stall_longest_ms = ms;
	pr_warn("TX stall recovered by %s after %u ms (%u frames) on hop %d (rail %u): recovered=%u longest_ms=%u\n",
		tbv_tx_stall_recovery_names[by], ms, path->tx_stall_frames,
		path->local_tx_hop, path->rail ? path->rail->rail_id : U32_MAX,
		path->tx_stall_recovered, path->tx_stall_longest_ms);
}

/*
 * T2, an EXPERIMENT and off by default: re-announce the TX ring's producer
 * index. Asahi's ring_write_descriptors() writes the producer inside its
 * posting loop (nhi.c), so a lost or coalesced doorbell would leave the
 * hardware behind ring->head with descriptors already sitting in the
 * descriptor ring.
 *
 * Safe by construction, which is the only reason it is admissible at all: the
 * value written is exactly ring->head, the index the hardware should already
 * have. It cannot post a descriptor that is not already in the ring, cannot
 * reorder one, and cannot duplicate one -- re-writing an index the hardware
 * already holds is a no-op. The ring's own lock is taken so head is read
 * consistently with nhi.c's writer; nhi->lock is deliberately NOT taken, so no
 * new lock order is introduced.
 *
 * Apple register layout, from Asahi apple_nhi_ring_layout: TX descriptor base
 * 0x10000, stride 0x4000, producer at +8 in the upper 16 bits
 * (ring_iowrite_prod). Apple paths only.
 */
static void tbv_path_tx_rekick_producer(struct tbv_path *path)
{
	struct tb_ring *ring = READ_ONCE(path->tx_ring);
	unsigned long flags;
	u16 head;

	if (!ring || !ring->nhi || !ring->nhi->iobase || !ring->is_tx)
		return;

	spin_lock_irqsave(&ring->lock, flags);
	if (!ring->running) {
		spin_unlock_irqrestore(&ring->lock, flags);
		return;
	}
	head = (u16)ring->head;
	iowrite32((u32)head << 16,
		  ring->nhi->iobase + TBV_APPLE_NHI_TXRING_DESC_BASE +
			  ring->hop * TBV_APPLE_NHI_RING_STRIDE + 8);
	spin_unlock_irqrestore(&ring->lock, flags);

	path->tx_stall_rekicked = true;
	path->tx_rekicks++;
	pr_warn("TX stall: re-announced producer index %u on hop %d (rail %u), rekicks=%u (experiment; correctness is the ledger's, not this rung's)\n",
		head, path->local_tx_hop,
		path->rail ? path->rail->rail_id : U32_MAX, path->tx_rekicks);
}

/*
 * TX completion watchdog, and the bounded recovery ladder hung off it.
 *
 * A hard stall used to have exactly one outcome: QP -> ERR, the barrier, and a
 * dead run. Degrade instead, one logged and counted rung at a time. NONE of
 * these rungs is required for correctness -- the credit ledger, the QP send
 * list and the retirement barrier own that. They are liveness fallbacks, and
 * tx_stall_recovered_by exists so we can tell which of them ever ends a real
 * episode rather than assuming.
 *
 * T1  TBV_TX_STALL_WARN_MS with descriptors outstanding and nothing retired:
 *     warn once with the ring's own indices, and reap anything the NHI
 *     completed but never notified (Apple TX polling is off, so nothing else
 *     will). Attributed as "reap".
 * T2  apple_tx_stall_rekick_ms, gated behind apple_tx_stall_rekick (default
 *     OFF, an experiment): re-announce the producer index once per episode.
 *     If progress follows, the episode is attributed "rekick" -- which is how
 *     we measure whether the rung is worth anything.
 * T3  apple_tx_stall_fail_ms (0 disables): declare the connection failed.
 *     tbv_apple_path_quiesce() closes TX admission, moves the bound QP to ERR
 *     (outstanding WRs complete with error CQEs), cancels unsent work and runs
 *     the retirement barrier before DMA ownership is released; the rail is
 *     quarantined until a new peer session binds it. Attributed "timeout".
 *     The default sits well above the bench's deliberate 1500 ms repost hold
 *     so a legitimate receive pause is a warning, not a failure.
 *
 * An episode that ends with no rung having fired is attributed "peer".
 */
static void tbv_path_tx_watchdog_work(struct work_struct *work)
{
	struct tbv_path *path = container_of(to_delayed_work(work),
					     struct tbv_path, tx_watchdog_work);
	u64 retired = tbv_path_tx_retired_total(path);
	int inflight = atomic_read(&path->tx_inflight);
	unsigned int fail_ms = READ_ONCE(apple_tx_stall_fail_ms);
	unsigned int rekick_ms = READ_ONCE(apple_tx_stall_rekick_ms);
	enum tbv_tx_stall_recovery by;
	unsigned int stalled_ms;

	/*
	 * Credit the re-kick only when one was actually issued in this episode;
	 * otherwise the peer resumed by itself and the rung proved nothing.
	 */
	by = path->tx_stall_rekicked ? TBV_TX_STALL_RECOVERY_REKICK :
				       TBV_TX_STALL_RECOVERY_PEER;

	if (inflight <= 0) {
		/* Idle: disarm. The next posted frame re-arms. */
		if (path->tx_stalled)
			tbv_path_tx_stall_recovered(path, by);
		return;
	}
	if (retired != path->tx_watchdog_last_retired) {
		if (path->tx_stalled)
			tbv_path_tx_stall_recovered(path, by);
		path->tx_watchdog_last_retired = retired;
		path->tx_watchdog_last_progress = jiffies;
		goto rearm;
	}

	stalled_ms = jiffies_to_msecs(jiffies - path->tx_watchdog_last_progress);
	if (stalled_ms < TBV_TX_STALL_WARN_MS)
		goto rearm;

	/* T1: descriptors the hardware finished and never told us about. */
	if (tbv_path_reap_tx(path)) {
		if (path->tx_stalled)
			tbv_path_tx_stall_recovered(path,
						    TBV_TX_STALL_RECOVERY_REAP);
		path->tx_watchdog_last_retired = tbv_path_tx_retired_total(path);
		path->tx_watchdog_last_progress = jiffies;
		path->tx_stalled = false;
		goto rearm;
	}
	if (!path->tx_stalled) {
		path->tx_stalled = true;
		path->tx_stall_rekicked = false;
		path->tx_stall_count++;
		path->tx_stall_frames = inflight;
		tbv_path_log_tx_stall(path, "warning", inflight, stalled_ms);
	}

	/* T2: one producer re-kick per episode, opt-in, before giving up. */
	if (READ_ONCE(apple_tx_stall_rekick) && !path->tx_stall_rekicked &&
	    rekick_ms && stalled_ms >= rekick_ms && tbv_path_is_apple(path) &&
	    !path->tx_closed)
		tbv_path_tx_rekick_producer(path);

	/* T3: the ledger takes over; the transfer does not survive this. */
	if (fail_ms && stalled_ms >= fail_ms && tbv_path_is_apple(path) &&
	    !path->tx_closed) {
		tbv_path_log_tx_stall(path, "FAILURE", inflight, stalled_ms);
		path->tx_stall_recovered_by[TBV_TX_STALL_RECOVERY_TIMEOUT]++;
		path->tx_stalled = false;
		path->tx_stall_rekicked = false;
		tbv_apple_path_quiesce(path->rail, NULL, "tx_stall",
				       tbv_ibdev_quiesce_deadline());
		/* The quiesce retired or canceled everything; nothing to watch. */
		return;
	}
rearm:
	tbv_path_queue_delayed_work(path, &path->tx_watchdog_work,
				    msecs_to_jiffies(TBV_TX_WATCHDOG_MS));
}

void tbv_path_init(struct tbv_path *path,
		   const struct tbv_path_config *cfg, struct tbv_rail *rail)
{
	memset(path, 0, sizeof(*path));
	path->state = TBV_PATH_NEW;
	path->cfg = *cfg;
	path->rail = rail;
	tbv_path_init_common(path);
}

void tbv_path_reset(struct tbv_path *path)
{
	path->tx_ring = NULL;
	path->rx_ring = NULL;
	memset(path, 0, sizeof(*path));
	path->state = TBV_PATH_STOPPED;
	tbv_path_init_common(path);
}

/*
 * TX data-queue occupancy dropped (or admission changed), so a sender parked
 * in tbv_path_wait_data_space() may now be able to reserve. Guarded by
 * waitqueue_active() so the common no-waiter case costs one load.
 */
static void tbv_path_wake_tx_space(struct tbv_path *path)
{
	if (waitqueue_active(&path->tx_space_wait))
		wake_up_all(&path->tx_space_wait);
}

/*
 * One TX descriptor retired (completed or canceled by the ring). Wake a
 * quiescing teardown only on the last one; the waitqueue is empty otherwise.
 */
static void tbv_path_tx_retire_one(struct tbv_path *path)
{
	WRITE_ONCE(path->tx_last_retire, jiffies);
	if (atomic_dec_return(&path->tx_inflight) == 0 &&
	    waitqueue_active(&path->tx_retire_wait))
		wake_up(&path->tx_retire_wait);
	tbv_path_wake_tx_space(path);
}

static void tbv_path_tx_complete(struct tb_ring *ring, struct ring_frame *frame,
				 bool canceled)
{
	struct tbv_data_frame *f = container_of(frame, struct tbv_data_frame,
						frame);
	struct tbv_path *path = f->path;
	struct tbv_tx_packet *packet;
	struct tbv_state *state = tbv_path_state(path);
	unsigned long flags;

	/*
	 * Where the reap actually ran. This is not a knob: ring_work() is
	 * scheduled by the NHI IRQ handler on whatever CPU AIC2 chose, and
	 * AIC2 has no per-IRQ affinity. Recording it is the only way to know.
	 */
	WRITE_ONCE(path->tx_last_cb_cpu, raw_smp_processor_id());

	dma_sync_single_for_cpu(tb_ring_dma_device(ring), f->dma,
				TBV_DATA_FRAME_SIZE, DMA_TO_DEVICE);
	spin_lock_irqsave(&path->tx_lock, flags);
	packet = f->packet;
	f->packet = NULL;
	f->done = NULL;
	f->done_ctx = NULL;
	f->frame.callback = NULL;
	f->frame.size = 0;
	f->frame.flags = 0;
	f->frame.sof = 0;
	f->frame.eof = 0;
	list_add_tail(&f->free_node, &path->tx_free);
	spin_unlock_irqrestore(&path->tx_lock, flags);

		if (state) {
			if (canceled)
				atomic64_inc(&state->data_tx_canceled);
			else
				atomic64_inc(&state->data_tx_completed);
		}
		if (packet && !canceled) {
			if (packet->control) {
				u64 age_ms = packet->queued_jiffies ?
					jiffies_to_msecs(jiffies -
							 packet->queued_jiffies) : 0;

				atomic64_inc(&path->control_tx_completed);
				tbv_path_atomic64_max(
					&path->control_tx_queue_max_ms, age_ms);
			} else {
				atomic64_inc(&path->data_tx_completed);
			}
		} else if (packet && !packet->control) {
			atomic64_inc(&path->data_tx_canceled);
		}
	if (packet) {
		tbv_path_finish_raw_stream_if_needed(path, packet);
		tbv_path_tx_packet_release(packet,
					   canceled || packet->canceled ?
					   -ECANCELED : 0);
	}

	tbv_path_tx_retire_one(path);
	tbv_path_schedule_tx(path);
}

static void tbv_path_tx_poll_work(struct work_struct *work)
{
	struct tbv_path *path = container_of(to_delayed_work(work),
					     struct tbv_path, tx_poll_work);
	struct tb_ring *ring = READ_ONCE(path->tx_ring);
	struct ring_frame *frame;
	u64 completed = 0;

	if (!ring)
		return;

	atomic64_inc(&path->tx_poll_calls);
	while ((frame = tb_ring_poll(ring))) {
		if (frame->callback)
			frame->callback(ring, frame, false);
		completed++;
	}
	if (completed)
		atomic64_add(completed, &path->tx_poll_completed);

	if (atomic_read(&path->tx_inflight) > 0 || completed)
		tbv_path_queue_tx_poll(path,
				       msecs_to_jiffies(TBV_TX_POLL_DELAY_MS));
}

static void tbv_path_rx_supp_poll_work(struct work_struct *work)
{
	struct tbv_path *path = container_of(to_delayed_work(work),
					     struct tbv_path,
					     rx_supp_poll_work);
	struct tb_ring *ring = READ_ONCE(path->rx_ring);
	struct ring_frame *frame;
	u64 completed = 0;

	if (!ring)
		return;

	atomic64_inc(&path->rx_supp_poll_calls);
	while ((frame = tb_ring_poll(ring))) {
		if (frame->callback)
			frame->callback(ring, frame, false);
		completed++;
	}
	if (completed)
		atomic64_add(completed, &path->rx_supp_poll_completed);

	if (completed || time_before(jiffies,
				     READ_ONCE(path->rx_supp_poll_until)))
		tbv_path_queue_delayed_work(
			path, &path->rx_supp_poll_work,
			msecs_to_jiffies(TBV_RX_SUPP_POLL_DELAY_MS));
}

static int tbv_path_post_rx_frame(struct tbv_data_frame *f);

static void tbv_path_rx_start_raw(struct tbv_path *path,
				  const struct tbv_native_data_header *hdr)
{
	path->rx_raw_opcode = hdr->opcode;
	path->rx_raw_flags = hdr->flags;
	path->rx_raw_dest_qp = hdr->dest_qp;
	path->rx_raw_src_qp = hdr->src_qp;
	path->rx_raw_psn = hdr->psn;
	path->rx_raw_imm_data = hdr->imm_data;
	path->rx_raw_rkey = hdr->rkey;
	path->rx_raw_base = hdr->remote_addr;
	path->rx_raw_done = 0;
	path->rx_raw_remaining = hdr->length;
	path->rx_raw_pending = hdr->length != 0;
}

static void tbv_path_rx_raw_payload(struct tbv_path *path,
				    struct tbv_state *state,
				    const void *payload, u32 len)
{
	struct tbv_native_data_header stream = {};
	struct tbv_native_data_header hdr = {};
	int ret;

	if (!path->rx_raw_pending || !len || len > path->rx_raw_remaining) {
		if (state)
			atomic64_inc(&state->data_rx_bad_frame);
		path->rx_raw_pending = false;
		path->rx_raw_remaining = 0;
		return;
	}

	stream.opcode = path->rx_raw_opcode;
	stream.flags = path->rx_raw_flags;
	stream.dest_qp = path->rx_raw_dest_qp;
	stream.src_qp = path->rx_raw_src_qp;
	stream.psn = path->rx_raw_psn;
	stream.length = path->rx_raw_done + path->rx_raw_remaining;
	stream.imm_data = path->rx_raw_imm_data;
	stream.remote_addr = path->rx_raw_base;
	stream.rkey = path->rx_raw_rkey;

	ret = tbv_native_data_raw_payload_header(&stream, path->rx_raw_done,
						 path->rx_raw_remaining, len,
						 &hdr);
	if (ret) {
		if (state)
			atomic64_inc(&state->data_rx_bad_frame);
		path->rx_raw_pending = false;
		path->rx_raw_remaining = 0;
		return;
	}

	if (len == path->rx_raw_remaining)
		path->rx_raw_pending = false;
	path->rx_raw_done += len;
	path->rx_raw_remaining -= len;
	if (state)
		tbv_ibdev_rx_native_frame(state, path, &hdr, payload);
}

static void tbv_path_zcopy_tx_complete(struct tb_ring *ring,
				       struct ring_frame *frame,
				       bool canceled)
{
	struct tbv_tx_packet *packet = container_of(frame,
						   struct tbv_tx_packet,
						   frame);
	struct tbv_path *path = packet->path;
	struct tbv_state *state = tbv_path_state(path);
	unsigned long flags;

	spin_lock_irqsave(&path->tx_lock, flags);
	if (packet->inflight) {
		list_del_init(&packet->node);
		packet->inflight = false;
	}
	spin_unlock_irqrestore(&path->tx_lock, flags);

	if (state) {
		if (canceled)
			atomic64_inc(&state->data_tx_canceled);
		else
			atomic64_inc(&state->data_tx_completed);
	}
	if (!canceled)
		atomic64_inc(&path->data_tx_completed);
	else
		atomic64_inc(&path->data_tx_canceled);

	tbv_path_finish_raw_stream_if_needed(path, packet);
	tbv_path_tx_packet_release(packet,
				   canceled || packet->canceled ? -ECANCELED : 0);
	tbv_path_tx_retire_one(path);
	tbv_path_schedule_tx(path);
}

static void tbv_path_rx_complete(struct tb_ring *ring, struct ring_frame *frame,
				 bool canceled)
{
	struct tbv_data_frame *f = container_of(frame, struct tbv_data_frame,
						frame);
	struct tbv_path *path = f->path;
	struct tbv_state *state = tbv_path_state(path);
	u32 len = tbv_frame_len(frame);
	u32 return_rx_credits = 0;
	u32 add_remote_credits = 0;
	bool was_raw_payload;

	if (canceled) {
		if (state)
			atomic64_inc(&state->data_rx_canceled);
		atomic64_inc(&path->data_rx_canceled);
		return;
	}
	if (state)
		atomic64_inc(&state->data_rx_completed);
	atomic64_inc(&path->data_rx_completed);

	dma_sync_single_for_cpu(tb_ring_dma_device(ring), f->dma,
				TBV_DATA_FRAME_SIZE, DMA_FROM_DEVICE);
	was_raw_payload = path->rx_raw_pending;
	if (len <= TBV_DATA_FRAME_SIZE && state) {
		struct tbv_native_data_header hdr;
		int ret;

		if (path->rail && path->rail->peer &&
		    path->rail->peer->backend == TBV_BACKEND_APPLE) {
			tbv_ibdev_rx_apple_frame(state, path, f->buf, len,
						 frame->sof, frame->eof);
		} else if (was_raw_payload) {
			return_rx_credits = 1;
			tbv_path_rx_raw_payload(path, state, f->buf, len);
		} else {
				ret = tbv_native_data_parse_header(f->buf, len, &hdr);
				if (!ret && hdr.opcode == TBV_NATIVE_DATA_OP_PATH_CREDIT) {
					if (tbv_native_data_valid_path_credit(&hdr)) {
						add_remote_credits = hdr.imm_data;
					} else {
						atomic64_inc(&state->data_rx_bad_header);
						atomic64_inc(&state->data_rx_bad_header_path_credit);
						pr_warn_ratelimited("native RX bad header reason=path_credit frame_len=%u flags=0x%x len=%u imm=%u psn=%u peer=%u rail=%u path_id=%u route=0x%llx\n",
								    len, hdr.flags,
								    hdr.length, hdr.imm_data,
								    hdr.psn,
								    path->rail && path->rail->peer ?
								    path->rail->peer->peer_id :
								    U32_MAX,
								    path->rail ?
								    path->rail->rail_id :
								    U32_MAX,
								    path->rail ?
								    path->rail->key.path_id :
								    0,
								    path->rail ?
								    (unsigned long long)path->rail->key.route :
								    0);
					}
				} else if (!ret &&
					   (hdr.flags & TBV_NATIVE_DATA_F_RAW_STREAM)) {
				if (len != TBV_NATIVE_DATA_HDR_SIZE ||
				    !hdr.length ||
				    (hdr.flags & ~(TBV_NATIVE_DATA_F_LAST |
						   TBV_NATIVE_DATA_F_SOLICITED |
						   TBV_NATIVE_DATA_F_RAW_STREAM))) {
					atomic64_inc(&state->data_rx_bad_frame);
				} else {
					return_rx_credits = 1;
					tbv_path_rx_start_raw(path, &hdr);
				}
			} else {
				if (!ret &&
				    tbv_native_data_consumes_rx_credit(hdr.opcode))
					return_rx_credits = 1;
				tbv_ibdev_rx_frame(state, path, f->buf, len);
			}
		}
	} else if (state) {
		atomic64_inc(&state->data_rx_bad_frame);
	}

	if (path->state == TBV_PATH_RING_STARTED ||
	    path->state == TBV_PATH_TUNNEL_ENABLED) {
		int ret = tbv_path_post_rx_frame(f);

		if (ret) {
			pr_warn_ratelimited("RX repost failed ret=%d\n", ret);
			if (state)
				atomic64_inc(&state->data_rx_repost_failed);
			atomic64_inc(&path->data_rx_repost_failed);
		} else {
			if (return_rx_credits)
				tbv_path_return_rx_data_credit(path,
							       return_rx_credits);
			if (add_remote_credits)
				tbv_path_add_remote_rx_credits(path,
							       add_remote_credits);
		}
	}
}

static int tbv_path_alloc_frames(struct tbv_path *path, bool tx)
{
	struct tbv_data_frame **frames_out = tx ? &path->tx_frames :
						 &path->rx_frames;
	u32 *count_out = tx ? &path->tx_frame_count : &path->rx_frame_count;
	u32 count = tx ? path->cfg.tx_ring_size : path->cfg.rx_ring_size;
	struct tb_ring *ring = tx ? path->tx_ring : path->rx_ring;
	struct device *dma_dev = tb_ring_dma_device(ring);
	struct tbv_data_frame *frames;
	int i;
	int ret = -ENOMEM;

	if (!tbv_dma_device_ready(dma_dev)) {
		pr_warn_ratelimited("%s ring DMA device is not ready for mapping\n",
				    tx ? "TX" : "RX");
		return -EPROBE_DEFER;
	}

	frames = kcalloc(count, sizeof(*frames), GFP_KERNEL);
	if (!frames)
		return -ENOMEM;

	for (i = 0; i < count; i++) {
		struct tbv_data_frame *f = &frames[i];

		f->path = path;
		f->tx = tx;
		INIT_LIST_HEAD(&f->frame.list);
		INIT_LIST_HEAD(&f->free_node);
		f->buf = kmalloc(TBV_DATA_FRAME_SIZE, GFP_KERNEL);
		if (!f->buf)
			goto err;
		f->dma = dma_map_single(dma_dev, f->buf, TBV_DATA_FRAME_SIZE,
					tx ? DMA_TO_DEVICE : DMA_FROM_DEVICE);
		if (dma_mapping_error(dma_dev, f->dma)) {
			kfree(f->buf);
			f->buf = NULL;
			ret = -EIO;
			goto err;
		}
		f->frame.buffer_phy = f->dma;
		f->frame.size = 0;
		if (tx)
			list_add_tail(&f->free_node, &path->tx_free);
	}

	*frames_out = frames;
	*count_out = count;
	return 0;

err:
	while (--i >= 0) {
		struct tbv_data_frame *f = &frames[i];

		if (f->buf) {
			dma_unmap_single(dma_dev, f->dma, TBV_DATA_FRAME_SIZE,
					 tx ? DMA_TO_DEVICE : DMA_FROM_DEVICE);
			kfree(f->buf);
		}
	}
	kfree(frames);
	return ret;
}

static void tbv_path_free_frames(struct tbv_path *path, bool tx)
{
	struct tbv_data_frame *frames = tx ? path->tx_frames : path->rx_frames;
	u32 count = tx ? path->tx_frame_count : path->rx_frame_count;
	struct tb_ring *ring = tx ? path->tx_ring : path->rx_ring;
	struct device *dma_dev;
	u32 i;

	if (!frames || !ring)
		return;

	dma_dev = tb_ring_dma_device(ring);
	if (!tbv_dma_device_ready(dma_dev)) {
		pr_warn_ratelimited("%s ring DMA device is not ready for unmapping\n",
				    tx ? "TX" : "RX");
		dma_dev = NULL;
	}
	for (i = 0; i < count; i++) {
		struct tbv_data_frame *f = &frames[i];

		if (!f->buf)
			continue;
		if (dma_dev)
			dma_unmap_single(dma_dev, f->dma, TBV_DATA_FRAME_SIZE,
					 tx ? DMA_TO_DEVICE : DMA_FROM_DEVICE);
		kfree(f->buf);
	}

	if (tx) {
		path->tx_frames = NULL;
		path->tx_frame_count = 0;
		INIT_LIST_HEAD(&path->tx_free);
	} else {
		path->rx_frames = NULL;
		path->rx_frame_count = 0;
	}
	kfree(frames);
}

static int tbv_path_alloc_control_packets(struct tbv_path *path)
{
	struct tbv_tx_packet *packets;
	u32 count = tbv_path_control_packet_count(path);
	u32 i;

	packets = kcalloc(count, sizeof(*packets), GFP_KERNEL);
	if (!packets)
		return -ENOMEM;

	for (i = 0; i < count; i++) {
		struct tbv_tx_packet *packet = &packets[i];

		INIT_LIST_HEAD(&packet->node);
		packet->path = path;
		packet->buf = packet->control_buf;
		packet->control = true;
		packet->pooled = true;
		list_add_tail(&packet->node, &path->tx_control_free);
	}

	path->tx_control_packets = packets;
	path->tx_control_packet_count = count;
	path->tx_data_queue_limit = path->cfg.tx_ring_size *
				    TBV_DATA_QUEUE_MULTIPLIER;
	return 0;
}

static void tbv_path_free_control_packets(struct tbv_path *path)
{
	kfree(path->tx_control_packets);
	path->tx_control_packets = NULL;
	path->tx_control_packet_count = 0;
	path->tx_control_queued = 0;
	path->tx_data_queued = 0;
	path->tx_data_reserved = 0;
	path->tx_data_queue_limit = 0;
	INIT_LIST_HEAD(&path->tx_control_free);
}

static int tbv_path_alloc_data_packets(struct tbv_path *path)
{
	struct tbv_tx_packet *packets;
	u32 count = tbv_path_data_packet_count(path);
	u32 i;

	if (!path->rail || !path->rail->peer ||
	    path->rail->peer->backend != TBV_BACKEND_APPLE)
		return 0;

	packets = kcalloc(count, sizeof(*packets), GFP_KERNEL);
	if (!packets)
		return -ENOMEM;

	for (i = 0; i < count; i++) {
		struct tbv_tx_packet *packet = &packets[i];

		INIT_LIST_HEAD(&packet->node);
		packet->path = path;
		packet->buf = kmalloc(TBV_DATA_FRAME_SIZE, GFP_KERNEL);
		if (!packet->buf)
			goto err;
		packet->pooled = true;
		list_add_tail(&packet->node, &path->tx_data_free);
	}

	path->tx_data_packets = packets;
	path->tx_data_packet_count = count;
	return 0;

err:
	while (i-- > 0)
		kfree(packets[i].buf);
	kfree(packets);
	INIT_LIST_HEAD(&path->tx_data_free);
	return -ENOMEM;
}

static void tbv_path_free_data_packets(struct tbv_path *path)
{
	u32 i;

	for (i = 0; i < path->tx_data_packet_count; i++)
		kfree(path->tx_data_packets[i].buf);
	kfree(path->tx_data_packets);
	path->tx_data_packets = NULL;
	path->tx_data_packet_count = 0;
	INIT_LIST_HEAD(&path->tx_data_free);
}

static int tbv_path_post_rx_frame(struct tbv_data_frame *f)
{
	struct tbv_path *path = f->path;

	f->frame.callback = tbv_path_rx_complete;
	f->frame.size = 0;
	f->frame.flags = 0;
	f->frame.sof = 0;
	f->frame.eof = 0;
	dma_sync_single_for_device(tb_ring_dma_device(path->rx_ring), f->dma,
				   TBV_DATA_FRAME_SIZE, DMA_FROM_DEVICE);
	return tb_ring_rx(path->rx_ring, &f->frame);
}

const char *tbv_path_state_name(enum tbv_path_state state)
{
	switch (state) {
	case TBV_PATH_NEW:
		return "new";
	case TBV_PATH_RING_ALLOCATED:
		return "ring_allocated";
	case TBV_PATH_RING_STARTED:
		return "ring_started";
	case TBV_PATH_TUNNEL_ENABLED:
		return "tunnel_enabled";
	case TBV_PATH_STOPPED:
		return "stopped";
	default:
		return "unknown";
	}
}

/*
 * A dedicated per-rail worker for the post half. Bound (no WQ_UNBOUND) so that
 * queue_work_on() actually places it; max_active 1 so a rail's WRs stay
 * serialized exactly as the shared queue serializes them today.
 */
static void tbv_path_create_tx_wq(struct tbv_path *path)
{
	int cpu = READ_ONCE(tx_worker_cpu);
	bool want = READ_ONCE(tx_worker_dedicated) || cpu >= 0;

	if (path->tx_wq || !want)
		return;

	path->tx_wq = alloc_workqueue("tbv_tx/%u", WQ_HIGHPRI | WQ_MEM_RECLAIM,
				      1, path->rail ? path->rail->rail_id : 0);
	if (!path->tx_wq) {
		pr_warn("dedicated TX worker requested but the workqueue could not be allocated; using the shared device workqueue\n");
		return;
	}
	pr_info("dedicated TX post worker for rail %u: cpu=%d (bound WQ_HIGHPRI, max_active 1)\n",
		path->rail ? path->rail->rail_id : 0, cpu);
}

static void tbv_path_destroy_tx_wq(struct tbv_path *path)
{
	if (!path->tx_wq)
		return;

	destroy_workqueue(path->tx_wq);
	path->tx_wq = NULL;
}

struct workqueue_struct *tbv_path_tx_wq(struct tbv_path *path)
{
	return path ? READ_ONCE(path->tx_wq) : NULL;
}

/* -1 when unpinned or when the requested CPU is not online. */
int tbv_path_tx_wq_cpu(void)
{
	int cpu = READ_ONCE(tx_worker_cpu);

	if (cpu < 0 || cpu >= nr_cpu_ids || !cpu_online(cpu))
		return -1;
	return cpu;
}

int tbv_path_alloc_rings(struct tbv_path *path, struct tb_xdomain *xd,
			 int requested_transmit_path)
{
	int e2e_tx_hop = 0;
	int transmit_path;
	int tx_hop;
	int rx_hop;
	int ret;

	if (path->state != TBV_PATH_NEW && path->state != TBV_PATH_STOPPED)
		return -EBUSY;

	if (!path->tx_trace)
		path->tx_trace = tbv_txtrace_alloc();
	tbv_path_create_tx_wq(path);

	tx_hop = path->cfg.tx_hop;
	rx_hop = path->cfg.rx_hop;
	if (requested_transmit_path < 0)
		requested_transmit_path = path->cfg.transmit_path;

	if (path->cfg.receive_path >= 0) {
		ret = tb_xdomain_alloc_in_hopid(xd, path->cfg.receive_path);
		if (ret != path->cfg.receive_path) {
			if (ret >= 0)
				tb_xdomain_release_in_hopid(xd, ret);
			return ret < 0 ? ret : -EBUSY;
		}
		path->remote_transmit_path = ret;
	}

	path->tx_ring = tb_ring_alloc_tx(xd->tb->nhi, tx_hop,
					 path->cfg.tx_ring_size,
					 path->cfg.tx_flags);
	if (!path->tx_ring) {
		ret = -ENOMEM;
		goto err_in_hop;
	}

	transmit_path = tb_xdomain_alloc_out_hopid(xd,
						   requested_transmit_path);
	if (transmit_path < 0) {
		ret = transmit_path;
		goto err_tx_ring;
	}
	path->local_transmit_path = transmit_path;
	path->local_tx_hop = path->tx_ring->hop;

	if (path->cfg.e2e)
		e2e_tx_hop = path->tx_ring->hop;

	path->tx_poll_enabled = tbv_path_progress_poll_enabled(path);
	/*
	 * RX frames for the Apple-compatible verbs path carry no per-message
	 * sequence number. Processing the same RX ring from the normal
	 * completion path and a supplemental poller can therefore expose later
	 * frames to the verbs receive queue before earlier frames. Keep RX
	 * completion single-sourced; TX polling is still used for timely send
	 * completions.
	 */
	path->rx_supp_poll_enabled = false;
	path->rx_ring = tb_ring_alloc_rx(xd->tb->nhi, rx_hop,
					 path->cfg.rx_ring_size,
					 path->cfg.rx_flags, e2e_tx_hop,
					 path->cfg.sof_mask,
					 path->cfg.eof_mask,
					 NULL, NULL);
	if (!path->rx_ring) {
		ret = -ENOMEM;
		goto err_out_hop;
	}
	path->local_rx_hop = path->rx_ring->hop;

	ret = tbv_path_configure_ring_throttling(path);
	if (ret)
		goto err_rx_ring;
	ret = tbv_path_alloc_frames(path, true);
	if (ret)
		goto err_rx_ring;
	ret = tbv_path_alloc_frames(path, false);
	if (ret)
		goto err_tx_frames;
	ret = tbv_path_alloc_control_packets(path);
	if (ret)
		goto err_rx_frames;
	ret = tbv_path_alloc_data_packets(path);
	if (ret)
		goto err_control_packets;

	path->state = TBV_PATH_RING_ALLOCATED;
	return 0;

err_control_packets:
	tbv_path_free_control_packets(path);
err_rx_frames:
	tbv_path_free_frames(path, false);
err_tx_frames:
	tbv_path_free_frames(path, true);
err_rx_ring:
	tb_ring_free(path->rx_ring);
	path->rx_ring = NULL;
	path->local_rx_hop = -1;
err_out_hop:
	tb_xdomain_release_out_hopid(xd, path->local_transmit_path);
	path->local_transmit_path = -1;
err_tx_ring:
	tb_ring_free(path->tx_ring);
	path->tx_ring = NULL;
	path->local_tx_hop = -1;
err_in_hop:
	if (path->remote_transmit_path >= 0) {
		tb_xdomain_release_in_hopid(xd, path->remote_transmit_path);
		path->remote_transmit_path = -1;
	}
	return ret;
}

/*
 * Apple NHI interrupt throttle (0xd004c, 256 ns units).
 *
 * The stock/Intel register (0x38c00) is programmed via nhi_throttle_direct and
 * MUST NOT be written on an Apple NHI -- the register lives elsewhere there.
 * On Apple hardware the kernel's own activation path does the write:
 * apple_nhi_ring_interrupt_active() programs APPLE_CIO_NHI_IRQ_THROTTLE from
 * ring->interval_nsec and SKIPS the write when the field is zero. So a zero
 * module parameter proves nothing was written, and the register keeps whatever
 * it held -- the 0xFF firmware default is 255 * 256 ns = 65.28 us, measured as
 * a size-independent latency floor (2 B through 4 KB all ~65.3 us; ib_send_lat,
 * zeus<->fedora, 2026-09-09; fixed by the same mechanism on fedora's stock NHI
 * at 4.4x).
 *
 * This module never used to set ring->interval_nsec, so on Apple NHIs the
 * write was skipped forever. Fix: set the field before the rings start, then
 * read the register back as proof. Applies to EVERY ring on an Apple NHI --
 * including native-backend rings to a Linux peer -- because the NHI is Apple's
 * regardless of who the peer is. That is why this is not gated on peer backend
 * (tbv_path_is_apple() is backend-based and would skip exactly the leg that
 * needs this).
 */
/* Apple NHIs exist only on Apple-SoC hosts, so the host DT names the NHI type. */
static bool tbv_host_is_apple(void)
{
	return of_machine_is_compatible("apple,arm-platform");
}

static void tbv_path_apply_ring_interval(struct tbv_path *path)
{
#ifdef TBV_HAVE_RING_INTERVAL_NSEC
	unsigned int interval = READ_ONCE(nhi_interrupt_throttle_ns);
	struct tb_ring *tx = path->tx_ring, *rx = path->rx_ring;
	u32 raw, idx;

	if (!interval || !tx || !rx)
		return;

	if (tbv_host_is_apple() && tx->nhi && tx->nhi->iobase) {
		/* apple_cio_ring_index(): the TX ring index is its hop. */
		idx = tx->hop;
		raw = ioread32(tx->nhi->iobase + TBV_APPLE_NHI_IRQ_THROTTLE +
			       4 * idx);
		pr_info("apple ring throttle BEFORE: tx hop=%d raw=%u (%u ns)\n",
			 tx->hop, raw,
			 raw * TBV_APPLE_NHI_IRQ_THROTTLE_GRANULARITY_NSEC);
	}

	tx->interval_nsec = interval;
	rx->interval_nsec = interval;
#endif
}

static void tbv_path_apple_throttle_readback(const struct tbv_path *path)
{
#ifdef TBV_HAVE_RING_INTERVAL_NSEC
	unsigned int interval = READ_ONCE(nhi_interrupt_throttle_ns);
	struct tb_ring *tx = path->tx_ring, *rx = path->rx_ring;
	u32 raw, idx;

	if (!interval || !tx || !rx || !tbv_host_is_apple())
		return;
	if (!tx->nhi || !tx->nhi->iobase || !rx->nhi || !rx->nhi->iobase)
		return;

	/* TX ring index is its hop; RX ring index is hop + n_rings. */
	idx = tx->hop;
	raw = ioread32(tx->nhi->iobase + TBV_APPLE_NHI_IRQ_THROTTLE + 4 * idx);
	pr_info("apple ring throttle AFTER : tx hop=%d raw=%u (%u ns) req=%u ns\n",
		 tx->hop, raw,
		 raw * TBV_APPLE_NHI_IRQ_THROTTLE_GRANULARITY_NSEC, interval);
	idx = rx->hop + rx->nhi->hop_count;
	raw = ioread32(rx->nhi->iobase + TBV_APPLE_NHI_IRQ_THROTTLE + 4 * idx);
	pr_info("apple ring throttle AFTER : rx hop=%d raw=%u (%u ns) req=%u ns\n",
		 rx->hop, raw,
		 raw * TBV_APPLE_NHI_IRQ_THROTTLE_GRANULARITY_NSEC, interval);
#endif
}

int tbv_path_start_rings(struct tbv_path *path)
{
	u32 i;
	int ret;

	if (path->state != TBV_PATH_RING_ALLOCATED)
		return -EINVAL;

	tbv_path_apply_ring_interval(path);
	tb_ring_start(path->tx_ring);
	tb_ring_start(path->rx_ring);
	tbv_path_apple_throttle_readback(path);
	path->state = TBV_PATH_RING_STARTED;
	for (i = 0; i < path->rx_frame_count; i++) {
		ret = tbv_path_post_rx_frame(&path->rx_frames[i]);
		if (ret) {
			pr_warn("post RX frame %u/%u failed ret=%d\n", i,
				path->rx_frame_count, ret);
			return ret;
		}
	}
	return 0;
}

int tbv_path_enable_tunnel(struct tbv_path *path, struct tb_xdomain *xd,
			   int remote_transmit_path)
{
	bool in_hop_allocated = false;
	int ret;

	if (path->state != TBV_PATH_RING_STARTED)
		return -EINVAL;

	if (path->remote_transmit_path >= 0) {
		if (path->remote_transmit_path != remote_transmit_path)
			return -EBUSY;
	} else {
		ret = tb_xdomain_alloc_in_hopid(xd, remote_transmit_path);
		if (ret != remote_transmit_path) {
			if (ret >= 0)
				tb_xdomain_release_in_hopid(xd, ret);
			return ret < 0 ? ret : -EBUSY;
		}
		path->remote_transmit_path = ret;
		in_hop_allocated = true;
	}

	ret = tb_xdomain_enable_paths(xd, path->local_transmit_path,
				      path->local_tx_hop,
				      remote_transmit_path,
				      path->local_rx_hop);
	if (ret) {
		tb_xdomain_release_in_hopid(xd, path->remote_transmit_path);
		path->remote_transmit_path = -1;
		return ret;
	}

	if (!in_hop_allocated)
		path->remote_transmit_path = remote_transmit_path;
	path->state = TBV_PATH_TUNNEL_ENABLED;
	tbv_path_schedule_tx(path);
	return 0;
}

int tbv_path_disable_tunnel(struct tbv_path *path, struct tb_xdomain *xd)
{
	int ret;

	if (path->state != TBV_PATH_TUNNEL_ENABLED)
		return 0;

	ret = tb_xdomain_disable_paths(xd, path->local_transmit_path,
				       path->local_tx_hop,
				       path->remote_transmit_path,
				       path->local_rx_hop);
	if (ret)
		return ret;

	tb_xdomain_release_in_hopid(xd, path->remote_transmit_path);
	path->remote_transmit_path = -1;
	path->state = TBV_PATH_RING_STARTED;
	return 0;
}

/*
 * TX admission. False while the tunnel is not up or while a quiesce has closed
 * the path (tx_closed): nothing may be enqueued, reserved or scheduled, so the
 * only frames left to retire are the ones already handed to the NHI.
 */
static bool tbv_path_tx_admits_locked(const struct tbv_path *path)
{
	return path->state == TBV_PATH_TUNNEL_ENABLED && !path->tx_closed;
}

void tbv_path_close_tx(struct tbv_path *path)
{
	unsigned long flags;

	spin_lock_irqsave(&path->tx_lock, flags);
	path->tx_closed = true;
	spin_unlock_irqrestore(&path->tx_lock, flags);
	/* Release space waiters so their next reserve returns -ENOTCONN now. */
	tbv_path_wake_tx_space(path);
}

void tbv_path_open_tx(struct tbv_path *path)
{
	unsigned long flags;

	spin_lock_irqsave(&path->tx_lock, flags);
	path->tx_closed = false;
	spin_unlock_irqrestore(&path->tx_lock, flags);
	tbv_path_wake_tx_space(path);
	tbv_path_schedule_tx(path);
}

static struct tbv_tx_packet *
tbv_path_alloc_data_packet_owned(struct tbv_path *path, u8 *buf, u32 len,
				 tbv_path_tx_done_fn done, void *done_ctx)
{
	struct tbv_tx_packet *packet;

	packet = kzalloc(sizeof(*packet), GFP_KERNEL);
	if (!packet)
		return NULL;

	INIT_LIST_HEAD(&packet->node);
	packet->path = path;
	packet->buf = buf;
	packet->len = len;
	packet->done = done;
	packet->done_ctx = done_ctx;
	packet->owner_ctx = done_ctx;
	packet->sof = TBV_DATA_PDF_FRAME_START;
	packet->eof = TBV_DATA_PDF_FRAME_END;
	return packet;
}

static struct tbv_tx_packet *
tbv_path_alloc_data_packet(struct tbv_path *path, const void *data, u32 len,
			   tbv_path_tx_done_fn done, void *done_ctx)
{
	struct tbv_tx_packet *packet;
	u8 *buf;

	buf = kmemdup(data, len, GFP_KERNEL);
	if (!buf)
		return NULL;

	packet = tbv_path_alloc_data_packet_owned(path, buf, len, done,
						  done_ctx);
	if (!packet)
		kfree(buf);
	return packet;
}

static struct tbv_tx_packet *tbv_path_alloc_pooled_data_packet(
	struct tbv_path *path, u32 len, tbv_path_tx_done_fn done, void *done_ctx)
{
	struct tbv_tx_packet *packet;
	unsigned long flags;

	spin_lock_irqsave(&path->tx_lock, flags);
	if (list_empty(&path->tx_data_free)) {
		spin_unlock_irqrestore(&path->tx_lock, flags);
		return NULL;
	}

	packet = list_first_entry(&path->tx_data_free, struct tbv_tx_packet,
				  node);
	list_del_init(&packet->node);
	spin_unlock_irqrestore(&path->tx_lock, flags);

	packet->len = len;
	packet->done = done;
	packet->done_ctx = done_ctx;
	packet->owner_ctx = done_ctx;
	packet->sof = TBV_DATA_PDF_FRAME_START;
	packet->eof = TBV_DATA_PDF_FRAME_END;
	packet->control = false;
	packet->queued = false;
	packet->zcopy = false;
	packet->unmap_dma = false;
	packet->raw_stream_start = false;
	packet->raw_stream_end = false;
	packet->raw_stream_counted = false;
	packet->canceled = false;
	packet->start_credit_group_frames = 0;
	return packet;
}

static struct tbv_tx_packet *
tbv_path_alloc_zcopy_packet(struct tbv_path *path, dma_addr_t dma, u32 len,
			    bool unmap_dma, tbv_path_tx_done_fn done,
			    void *done_ctx)
{
	struct tbv_tx_packet *packet;

	packet = kzalloc(sizeof(*packet), GFP_KERNEL);
	if (!packet)
		return NULL;

	INIT_LIST_HEAD(&packet->node);
	INIT_LIST_HEAD(&packet->frame.list);
	packet->path = path;
	packet->len = len;
	packet->dma = dma;
	packet->done = done;
	packet->done_ctx = done_ctx;
	packet->owner_ctx = done_ctx;
	packet->sof = TBV_DATA_PDF_FRAME_START;
	packet->eof = TBV_DATA_PDF_FRAME_END;
	packet->zcopy = true;
	packet->unmap_dma = unmap_dma;
	packet->raw_stream_counted = false;
	return packet;
}

static int tbv_path_enqueue_control(struct tbv_path *path, const void *data,
				    u32 len, tbv_path_tx_done_fn done,
				    void *done_ctx)
{
	struct tbv_tx_packet *packet;
	unsigned long flags;
	bool pooled = true;

	if (len > TBV_CONTROL_FRAME_SIZE)
		return -EMSGSIZE;

	spin_lock_irqsave(&path->tx_lock, flags);
	if (!tbv_path_tx_admits_locked(path)) {
		spin_unlock_irqrestore(&path->tx_lock, flags);
		return -ENOTCONN;
	}
	if (list_empty(&path->tx_control_free)) {
		spin_unlock_irqrestore(&path->tx_lock, flags);
		packet = kzalloc(sizeof(*packet), GFP_ATOMIC);
		if (!packet)
			return -ENOMEM;
		INIT_LIST_HEAD(&packet->node);
		packet->path = path;
		packet->buf = packet->control_buf;
		packet->control = true;
		pooled = false;
		spin_lock_irqsave(&path->tx_lock, flags);
		if (!tbv_path_tx_admits_locked(path)) {
			spin_unlock_irqrestore(&path->tx_lock, flags);
			kfree(packet);
			return -ENOTCONN;
		}
	} else {
		packet = list_first_entry(&path->tx_control_free,
					  struct tbv_tx_packet, node);
		list_del_init(&packet->node);
	}

	packet->len = len;
	packet->done = done;
	packet->done_ctx = done_ctx;
	packet->owner_ctx = done_ctx;
	packet->sof = TBV_DATA_PDF_FRAME_START;
	packet->eof = TBV_DATA_PDF_FRAME_END;
	packet->pooled = pooled;
	packet->queued_jiffies = jiffies;
	packet->queued = true;
	memcpy(packet->buf, data, len);
	list_add_tail(&packet->node, &path->tx_control_queue);
	path->tx_control_queued++;
	atomic64_inc(&path->control_tx_enqueued);
	spin_unlock_irqrestore(&path->tx_lock, flags);

	tbv_path_schedule_tx(path);
	return 0;
}

static int tbv_path_enqueue_data(struct tbv_path *path,
				 struct tbv_tx_packet *packet,
				 bool defer_schedule)
{
	unsigned long flags;

	spin_lock_irqsave(&path->tx_lock, flags);
	if (!tbv_path_tx_admits_locked(path)) {
		spin_unlock_irqrestore(&path->tx_lock, flags);
		return -ENOTCONN;
	}
	if (path->tx_data_reserved) {
		path->tx_data_reserved--;
	} else if (path->tx_data_queued >= path->tx_data_queue_limit) {
		spin_unlock_irqrestore(&path->tx_lock, flags);
		return -ENOMEM;
	}

	packet->start_credit_group_frames = 1;
	packet->queued = true;
	list_add_tail(&packet->node, &path->tx_data_queue);
	path->tx_data_queued++;
	atomic64_inc(&path->data_tx_enqueued);
	spin_unlock_irqrestore(&path->tx_lock, flags);

	if (!defer_schedule)
		tbv_path_schedule_tx(path);
	return 0;
}

static int tbv_path_enqueue_data_list(struct tbv_path *path,
				      struct list_head *packets, u32 count,
				      bool defer_schedule)
{
	struct tbv_tx_packet *packet;
	unsigned long flags;
	bool first = true;
	u32 used;

	if (!count)
		return 0;

	spin_lock_irqsave(&path->tx_lock, flags);
	if (!tbv_path_tx_admits_locked(path)) {
		spin_unlock_irqrestore(&path->tx_lock, flags);
		return -ENOTCONN;
	}

	used = path->tx_data_queued + path->tx_data_reserved;
	if (count > path->tx_data_queue_limit ||
	    used > path->tx_data_queue_limit - count) {
		spin_unlock_irqrestore(&path->tx_lock, flags);
		return -ENOMEM;
	}

	list_for_each_entry(packet, packets, node) {
		packet->start_credit_group_frames = first ? count : 0;
		packet->queued = true;
		path->tx_data_queued++;
		atomic64_inc(&path->data_tx_enqueued);
		first = false;
	}
	list_splice_tail_init(packets, &path->tx_data_queue);
	spin_unlock_irqrestore(&path->tx_lock, flags);

	if (!defer_schedule)
		tbv_path_schedule_tx(path);
	return 0;
}

static int tbv_path_enqueue_reserved_data_list(struct tbv_path *path,
					       struct list_head *packets,
					       u32 count, bool defer_schedule)
{
	struct tbv_tx_packet *packet;
	unsigned long flags;
	bool first = true;

	if (!count)
		return 0;

	spin_lock_irqsave(&path->tx_lock, flags);
	if (!tbv_path_tx_admits_locked(path)) {
		spin_unlock_irqrestore(&path->tx_lock, flags);
		return -ENOTCONN;
	}
	if (path->tx_data_reserved < count) {
		spin_unlock_irqrestore(&path->tx_lock, flags);
		return -ENOMEM;
	}

	path->tx_data_reserved -= count;
	list_for_each_entry(packet, packets, node) {
		packet->start_credit_group_frames = first ? count : 0;
		packet->queued = true;
		path->tx_data_queued++;
		atomic64_inc(&path->data_tx_enqueued);
		first = false;
	}
	list_splice_tail_init(packets, &path->tx_data_queue);
	spin_unlock_irqrestore(&path->tx_lock, flags);

	if (!defer_schedule)
		tbv_path_schedule_tx(path);
	return 0;
}

int tbv_path_reserve_data(struct tbv_path *path, u32 frames)
{
	unsigned long flags;
	u32 used;

	if (!frames)
		return 0;

	spin_lock_irqsave(&path->tx_lock, flags);
	if (!tbv_path_tx_admits_locked(path)) {
		spin_unlock_irqrestore(&path->tx_lock, flags);
		return -ENOTCONN;
	}

	used = path->tx_data_queued + path->tx_data_reserved;
	if (frames > path->tx_data_queue_limit ||
	    used > path->tx_data_queue_limit - frames) {
		spin_unlock_irqrestore(&path->tx_lock, flags);
		return -ENOMEM;
	}

	path->tx_data_reserved += frames;
	spin_unlock_irqrestore(&path->tx_lock, flags);
	return 0;
}

void tbv_path_release_data_reservation(struct tbv_path *path, u32 frames)
{
	unsigned long flags;

	if (!frames)
		return;

	spin_lock_irqsave(&path->tx_lock, flags);
	if (path->tx_data_reserved >= frames)
		path->tx_data_reserved -= frames;
	else
		path->tx_data_reserved = 0;
	spin_unlock_irqrestore(&path->tx_lock, flags);
	tbv_path_wake_tx_space(path);
}

/*
 * True when tbv_path_reserve_data(path, frames) would not return -ENOMEM, or
 * when admission is closed. The closed case is deliberately "available": the
 * waiter must be released so its next reserve returns the real -ENOTCONN
 * instead of parking until the safety-net timeout.
 */
static bool tbv_path_data_space_available(struct tbv_path *path, u32 frames)
{
	unsigned long flags;
	bool available;
	u32 used;

	spin_lock_irqsave(&path->tx_lock, flags);
	if (!tbv_path_tx_admits_locked(path)) {
		available = true;
	} else if (frames > path->tx_data_queue_limit) {
		/* Never satisfiable: let the caller take the hard error. */
		available = true;
	} else {
		used = path->tx_data_queued + path->tx_data_reserved;
		available = used <= path->tx_data_queue_limit - frames;
	}
	spin_unlock_irqrestore(&path->tx_lock, flags);
	return available;
}

void tbv_path_wait_data_space(struct tbv_path *path, u32 frames)
{
	ktime_t start = ktime_get();
	s64 waited_ns;
	long left;

	atomic64_inc(&path->tx_space_waits);
	left = wait_event_timeout(path->tx_space_wait,
				  tbv_path_data_space_available(path, frames),
				  msecs_to_jiffies(TBV_TX_SPACE_WAIT_MS));
	if (!left)
		atomic64_inc(&path->tx_space_wait_timeouts);

	waited_ns = ktime_to_ns(ktime_sub(ktime_get(), start));
	if (waited_ns < 0)
		waited_ns = 0;
	atomic64_add(waited_ns, &path->tx_space_wait_ns);
	tbv_path_atomic64_max(&path->tx_space_wait_max_ns, waited_ns);
}

static void tbv_path_schedule_tx(struct tbv_path *path)
{
	struct tbv_state *state = tbv_path_state(path);
	unsigned long flags;

	spin_lock_irqsave(&path->tx_lock, flags);
	if (path->tx_scheduling) {
		spin_unlock_irqrestore(&path->tx_lock, flags);
		return;
	}
	path->tx_scheduling = true;
	spin_unlock_irqrestore(&path->tx_lock, flags);

	for (;;) {
		struct tbv_tx_packet *packet;
		struct tbv_data_frame *f;
		bool needs_staging;
		bool old_raw_stream_active;
		bool old_raw_stream_end_seen;
		void *old_raw_stream_owner;
		u32 old_raw_stream_inflight;
		bool charged_data_credit;
		bool from_control_queue;
		u32 old_start_credit_group_frames;
		int ret;

		spin_lock_irqsave(&path->tx_lock, flags);
		if (!tbv_path_tx_admits_locked(path) ||
		    (list_empty(&path->tx_control_queue) &&
		     list_empty(&path->tx_data_queue))) {
			path->tx_scheduling = false;
			spin_unlock_irqrestore(&path->tx_lock, flags);
			return;
		}

		if (path->tx_raw_stream_active) {
			if (list_empty(&path->tx_data_queue)) {
				path->tx_scheduling = false;
				spin_unlock_irqrestore(&path->tx_lock, flags);
				return;
			}
			packet = list_first_entry(&path->tx_data_queue,
						  struct tbv_tx_packet, node);
			if (!packet->zcopy || packet->raw_stream_start ||
			    packet->owner_ctx != path->tx_raw_stream_owner) {
				path->tx_scheduling = false;
				spin_unlock_irqrestore(&path->tx_lock, flags);
				return;
			}
			from_control_queue = false;
			needs_staging = false;
		} else if (!list_empty(&path->tx_control_queue)) {
			packet = list_first_entry(&path->tx_control_queue,
						  struct tbv_tx_packet, node);
			from_control_queue = true;
			needs_staging = true;
		} else {
			if (list_empty(&path->tx_data_queue)) {
				path->tx_scheduling = false;
				spin_unlock_irqrestore(&path->tx_lock, flags);
				return;
			}
			packet = list_first_entry(&path->tx_data_queue,
						  struct tbv_tx_packet, node);
			from_control_queue = false;
			needs_staging = !packet->zcopy;
		}

		if (!from_control_queue) {
			u32 tx_inflight_limit =
				tbv_path_tx_inflight_limit(path);

			if (tx_inflight_limit &&
			    atomic_read(&path->tx_inflight) >=
				    tx_inflight_limit) {
				path->tx_scheduling = false;
				spin_unlock_irqrestore(&path->tx_lock, flags);
				return;
			}
		}

		if (needs_staging && list_empty(&path->tx_free)) {
			path->tx_scheduling = false;
			spin_unlock_irqrestore(&path->tx_lock, flags);
			return;
		}
		if (needs_staging && !from_control_queue) {
			u32 reserve = tbv_path_tx_control_frame_reserve(path);
			u32 inflight = atomic_read(&path->tx_inflight);
			u32 available = path->tx_frame_count > inflight ?
					path->tx_frame_count - inflight : 0;

			if (available <= reserve) {
				path->tx_scheduling = false;
				spin_unlock_irqrestore(&path->tx_lock, flags);
				return;
			}
		}

		charged_data_credit = false;
		old_start_credit_group_frames = packet->start_credit_group_frames;
		if (!packet->control && path->tx_remote_data_credit_max) {
			u32 start_credit_required =
				tbv_native_data_start_credit_required(
					packet->start_credit_group_frames,
					path->tx_remote_data_credit_max);

			if (path->tx_remote_data_credits <
			    max_t(u32, 1, start_credit_required)) {
				if (state)
					atomic64_inc(&state->data_tx_credit_stalls);
				atomic64_inc(&path->data_tx_credit_stalls);
				path->tx_scheduling = false;
				spin_unlock_irqrestore(&path->tx_lock, flags);
				return;
			}
			path->tx_remote_data_credits--;
			charged_data_credit = true;
		}
		packet->start_credit_group_frames = 0;

		if (from_control_queue)
			path->tx_control_queued--;
		else
			path->tx_data_queued--;
		list_del_init(&packet->node);
		packet->queued = false;
		old_raw_stream_active = path->tx_raw_stream_active;
		old_raw_stream_owner = path->tx_raw_stream_owner;
		old_raw_stream_end_seen = path->tx_raw_stream_end_seen;
		old_raw_stream_inflight = path->tx_raw_stream_inflight;
		tbv_path_count_raw_stream_locked(path, packet);

		if (needs_staging) {
			f = list_first_entry(&path->tx_free,
					     struct tbv_data_frame, free_node);
			list_del_init(&f->free_node);
		} else {
			f = NULL;
		}
		atomic_inc(&path->tx_inflight);
		spin_unlock_irqrestore(&path->tx_lock, flags);

		if (packet->zcopy) {
			packet->frame.buffer_phy = packet->dma;
			packet->frame.callback = tbv_path_zcopy_tx_complete;
			packet->frame.size = packet->len == TBV_DATA_FRAME_SIZE ?
					     0 : packet->len;
			packet->frame.flags = 0;
			packet->frame.sof = packet->sof;
			packet->frame.eof = packet->eof;

			spin_lock_irqsave(&path->tx_lock, flags);
			list_add_tail(&packet->node, &path->tx_zcopy_inflight);
			packet->inflight = true;
			spin_unlock_irqrestore(&path->tx_lock, flags);

			ret = tb_ring_tx(path->tx_ring, &packet->frame);
			if (!ret) {
				if (state)
					atomic64_inc(&state->data_tx_posted);
				atomic64_inc(&path->data_tx_posted);
				tbv_path_wake_tx_space(path);
				tbv_path_arm_tx_watchdog(path);
				tbv_path_queue_tx_poll(path, 0);
				tbv_path_queue_rx_supp_poll(
					path,
					msecs_to_jiffies(
						TBV_RX_SUPP_POLL_DELAY_MS));
				continue;
			}

			if (state)
				atomic64_inc(&state->data_tx_errors);
			spin_lock_irqsave(&path->tx_lock, flags);
			if (packet->inflight) {
				list_del_init(&packet->node);
				packet->inflight = false;
			}
			path->tx_raw_stream_active = old_raw_stream_active;
			path->tx_raw_stream_owner = old_raw_stream_owner;
			path->tx_raw_stream_end_seen = old_raw_stream_end_seen;
			path->tx_raw_stream_inflight = old_raw_stream_inflight;
			packet->raw_stream_counted = false;
			if (charged_data_credit) {
				if (path->tx_remote_data_credits <
				    path->tx_remote_data_credit_max)
					path->tx_remote_data_credits++;
			}
			if (ret == -ENOMEM &&
			    path->state == TBV_PATH_TUNNEL_ENABLED &&
			    (packet->done || packet->owner_ctx)) {
				list_add(&packet->node, &path->tx_data_queue);
				path->tx_data_queued++;
				packet->queued = true;
				packet->start_credit_group_frames =
					old_start_credit_group_frames;
				path->tx_scheduling = false;
				spin_unlock_irqrestore(&path->tx_lock, flags);
				atomic_dec(&path->tx_inflight);
				return;
			}
			spin_unlock_irqrestore(&path->tx_lock, flags);
			atomic_dec(&path->tx_inflight);
			tbv_path_finish_raw_stream_if_needed(path, packet);
			tbv_path_tx_packet_release(packet, ret);
			spin_lock_irqsave(&path->tx_lock, flags);
			path->tx_scheduling = false;
			spin_unlock_irqrestore(&path->tx_lock, flags);
			return;
		}

		memcpy(f->buf, packet->buf, packet->len);
		f->packet = packet;
		f->done = packet->done;
		f->done_ctx = packet->done_ctx;
		f->frame.callback = tbv_path_tx_complete;
		f->frame.size = packet->len == TBV_DATA_FRAME_SIZE ? 0 :
							       packet->len;
		f->frame.flags = 0;
		f->frame.sof = packet->sof;
		f->frame.eof = packet->eof;
		dma_sync_single_for_device(tb_ring_dma_device(path->tx_ring),
					   f->dma, TBV_DATA_FRAME_SIZE,
					   DMA_TO_DEVICE);

		ret = tb_ring_tx(path->tx_ring, &f->frame);
		if (!ret) {
			if (state)
				atomic64_inc(&state->data_tx_posted);
			if (packet->control)
				atomic64_inc(&path->control_tx_posted);
			else
				atomic64_inc(&path->data_tx_posted);
			tbv_path_wake_tx_space(path);
			tbv_path_arm_tx_watchdog(path);
			tbv_path_queue_tx_poll(path, 0);
			tbv_path_queue_rx_supp_poll(
				path,
				msecs_to_jiffies(TBV_RX_SUPP_POLL_DELAY_MS));
			continue;
		}

		if (state)
			atomic64_inc(&state->data_tx_errors);
		f->packet = NULL;
		f->done = NULL;
		f->done_ctx = NULL;
		f->frame.callback = NULL;
		spin_lock_irqsave(&path->tx_lock, flags);
		list_add_tail(&f->free_node, &path->tx_free);
		path->tx_raw_stream_active = old_raw_stream_active;
		path->tx_raw_stream_owner = old_raw_stream_owner;
		path->tx_raw_stream_end_seen = old_raw_stream_end_seen;
		path->tx_raw_stream_inflight = old_raw_stream_inflight;
		packet->raw_stream_counted = false;
		if (charged_data_credit) {
			if (path->tx_remote_data_credits <
			    path->tx_remote_data_credit_max)
				path->tx_remote_data_credits++;
		}
		if (ret == -ENOMEM &&
		    path->state == TBV_PATH_TUNNEL_ENABLED &&
		    (packet->control || packet->done || packet->owner_ctx)) {
			if (packet->control) {
				list_add(&packet->node, &path->tx_control_queue);
				path->tx_control_queued++;
			} else {
				list_add(&packet->node, &path->tx_data_queue);
				path->tx_data_queued++;
				packet->start_credit_group_frames =
					old_start_credit_group_frames;
			}
			packet->queued = true;
			path->tx_scheduling = false;
			spin_unlock_irqrestore(&path->tx_lock, flags);
			atomic_dec(&path->tx_inflight);
			return;
		}
		spin_unlock_irqrestore(&path->tx_lock, flags);
		atomic_dec(&path->tx_inflight);
		tbv_path_tx_packet_release(packet, ret);
		spin_lock_irqsave(&path->tx_lock, flags);
		path->tx_scheduling = false;
		spin_unlock_irqrestore(&path->tx_lock, flags);
		return;
	}
}

void tbv_path_kick_tx(struct tbv_path *path)
{
	if (path)
		tbv_path_schedule_tx(path);
}

int tbv_path_send(struct tbv_path *path, const void *data, u32 len,
		  unsigned int send_flags,
		  tbv_path_tx_done_fn done, void *done_ctx)
{
	struct tbv_tx_packet *packet;
	int ret;

	if (!len || len > TBV_DATA_FRAME_SIZE)
		return -EINVAL;
	if (send_flags & ~(TBV_PATH_SEND_CONTROL | TBV_PATH_SEND_DEFER))
		return -EINVAL;
	if (send_flags & TBV_PATH_SEND_CONTROL) {
		if (send_flags & TBV_PATH_SEND_DEFER)
			return -EINVAL;
		return tbv_path_enqueue_control(path, data, len, done, done_ctx);
	}

	packet = tbv_path_alloc_data_packet(path, data, len, done, done_ctx);
	if (!packet)
		return -ENOMEM;

	ret = tbv_path_enqueue_data(path, packet,
				    send_flags & TBV_PATH_SEND_DEFER);
	if (ret) {
		kfree(packet->buf);
		kfree(packet);
		return ret;
	}
	return 0;
}

int tbv_path_send_owned(struct tbv_path *path, void *data, u32 len,
			unsigned int send_flags,
			tbv_path_tx_done_fn done, void *done_ctx)
{
	return tbv_path_send_marked_owned(path, data, len,
					  TBV_DATA_PDF_FRAME_START,
					  TBV_DATA_PDF_FRAME_END,
					  send_flags, done, done_ctx);
}

int tbv_path_send_marked_owned(struct tbv_path *path, void *data, u32 len,
			       u8 sof, u8 eof, unsigned int send_flags,
			       tbv_path_tx_done_fn done, void *done_ctx)
{
	struct tbv_tx_packet *packet;
	int ret;

	if (!data)
		return -EINVAL;
	if (!len || len > TBV_DATA_FRAME_SIZE ||
	    (send_flags & ~(TBV_PATH_SEND_CONTROL | TBV_PATH_SEND_DEFER)) ||
	    (send_flags & TBV_PATH_SEND_CONTROL)) {
		kfree(data);
		return -EINVAL;
	}

	packet = tbv_path_alloc_data_packet_owned(path, data, len, done,
						  done_ctx);
	if (!packet) {
		kfree(data);
		return -ENOMEM;
	}
	packet->sof = sof;
	packet->eof = eof;

	ret = tbv_path_enqueue_data(path, packet,
				    send_flags & TBV_PATH_SEND_DEFER);
	if (ret) {
		kfree(packet->buf);
		kfree(packet);
		return ret;
	}
	return 0;
}

int tbv_path_send_marked_fill(struct tbv_path *path, u32 len,
			      u8 sof, u8 eof, unsigned int send_flags,
			      tbv_path_tx_fill_fn fill, void *fill_ctx,
			      tbv_path_tx_done_fn done, void *done_ctx)
{
	struct tbv_tx_packet *packet;
	u8 *buf;
	int ret;

	if (!fill)
		return -EINVAL;
	if (!len || len > TBV_DATA_FRAME_SIZE ||
	    (send_flags & ~(TBV_PATH_SEND_CONTROL | TBV_PATH_SEND_DEFER)) ||
	    (send_flags & TBV_PATH_SEND_CONTROL))
		return -EINVAL;

	packet = tbv_path_alloc_pooled_data_packet(path, len, done, done_ctx);
	if (packet) {
		ret = fill(fill_ctx, packet->buf, len);
		if (ret) {
			packet->done = NULL;
			packet->done_ctx = NULL;
			tbv_path_tx_packet_release(packet, ret);
			return ret;
		}
	} else {
		buf = kmalloc(len, GFP_KERNEL);
		if (!buf)
			return -ENOMEM;
		ret = fill(fill_ctx, buf, len);
		if (ret) {
			kfree(buf);
			return ret;
		}
		packet = tbv_path_alloc_data_packet_owned(path, buf, len,
							  done, done_ctx);
		if (!packet) {
			kfree(buf);
			return -ENOMEM;
		}
	}

	packet->sof = sof;
	packet->eof = eof;
	ret = tbv_path_enqueue_data(path, packet,
				    send_flags & TBV_PATH_SEND_DEFER);
	if (ret) {
		packet->done = NULL;
		packet->done_ctx = NULL;
		tbv_path_tx_packet_release(packet, ret);
		return ret;
	}
	return 0;
}

static void tbv_path_release_packet_list(struct list_head *packets, int status)
{
	while (!list_empty(packets)) {
		struct tbv_tx_packet *packet =
			list_first_entry(packets, struct tbv_tx_packet, node);

		list_del_init(&packet->node);
		tbv_path_tx_packet_release(packet, status);
	}
}

static void tbv_path_release_packet_list_silent(struct list_head *packets,
						int status)
{
	while (!list_empty(packets)) {
		struct tbv_tx_packet *packet =
			list_first_entry(packets, struct tbv_tx_packet, node);

		list_del_init(&packet->node);
		packet->done = NULL;
		packet->done_ctx = NULL;
		tbv_path_tx_packet_release(packet, status);
	}
}

void tbv_path_release_prepared_list_silent(struct list_head *packets,
					   int status)
{
	tbv_path_release_packet_list_silent(packets, status);
}

static void tbv_path_release_owned_frame_list(struct list_head *frames)
{
	while (!list_empty(frames)) {
		struct tbv_path_owned_frame *frame =
			list_first_entry(frames, struct tbv_path_owned_frame,
					 node);

		list_del_init(&frame->node);
		kfree(frame->data);
		kfree(frame);
	}
}

int tbv_path_prepare_owned_list(struct tbv_path *path,
				struct list_head *frames,
				struct list_head *packets,
				u32 *packet_count_out,
				unsigned int send_flags,
				tbv_path_tx_done_fn done,
				void *done_ctx)
{
	struct tbv_path_owned_frame *owned;
	struct tbv_tx_packet *packet;
	LIST_HEAD(prepared);
	u32 packet_count = 0;
	int ret;

	if (!path || !frames || !packets || !packet_count_out)
		return -EINVAL;
	*packet_count_out = 0;
	if (send_flags & ~(TBV_PATH_SEND_DEFER)) {
		tbv_path_release_owned_frame_list(frames);
		return -EINVAL;
	}

	while (!list_empty(frames)) {
		owned = list_first_entry(frames, struct tbv_path_owned_frame,
					 node);
		list_del_init(&owned->node);

		if (!owned->data || !owned->len ||
		    owned->len > TBV_DATA_FRAME_SIZE) {
			kfree(owned->data);
			kfree(owned);
			ret = -EINVAL;
			goto err_release;
		}

		packet = tbv_path_alloc_data_packet_owned(path, owned->data,
							  owned->len, done,
							  done_ctx);
		if (!packet) {
			kfree(owned->data);
			kfree(owned);
			ret = -ENOMEM;
			goto err_release;
		}
		owned->data = NULL;
		packet->sof = owned->sof;
		packet->eof = owned->eof;
		list_add_tail(&packet->node, &prepared);
		packet_count++;
		kfree(owned);
	}

	list_splice_tail_init(&prepared, packets);
	*packet_count_out = packet_count;
	return 0;

err_release:
	tbv_path_release_packet_list_silent(&prepared, ret);
	tbv_path_release_owned_frame_list(frames);
	return ret;
}

int tbv_path_enqueue_prepared_reserved(struct tbv_path *path,
				       struct list_head *packets,
				       u32 packet_count,
				       unsigned int send_flags)
{
	int ret;

	if (!path || !packets)
		return -EINVAL;
	if (send_flags & ~(TBV_PATH_SEND_DEFER)) {
		tbv_path_release_packet_list_silent(packets, -EINVAL);
		return -EINVAL;
	}

	ret = tbv_path_enqueue_reserved_data_list(
		path, packets, packet_count, send_flags & TBV_PATH_SEND_DEFER);
	if (ret)
		tbv_path_release_packet_list_silent(packets, ret);
	return ret;
}

int tbv_path_send_owned_list_reserved(struct tbv_path *path,
				      struct list_head *frames,
				      unsigned int send_flags,
				      tbv_path_tx_done_fn done,
				      void *done_ctx)
{
	LIST_HEAD(packets);
	u32 packet_count;
	int ret;

	ret = tbv_path_prepare_owned_list(path, frames, &packets,
					  &packet_count, send_flags, done,
					  done_ctx);
	if (ret)
		return ret;

	return tbv_path_enqueue_prepared_reserved(path, &packets,
						  packet_count, send_flags);
}

int tbv_path_send_page_stream(struct tbv_path *path,
			      const struct tbv_native_data_header *hdr,
			      u32 total_length, unsigned int send_flags,
			      tbv_path_tx_done_fn meta_done,
			      void *meta_done_ctx,
			      tbv_path_next_page_fn next, void *next_ctx)
{
	struct device *dma_dev;
	LIST_HEAD(packets);
	u32 prepared = 0;
	u32 packet_count = 0;
	struct tbv_tx_packet *packet;
	u32 max_raw_payload;
	struct tbv_native_data_header stream_hdr;
	u8 *hdr_buf;
	int ret;

	if (!path || !hdr || !next || !total_length) {
		ret = -EINVAL;
		goto err_meta_done;
	}
	if (send_flags & ~(TBV_PATH_SEND_DEFER)) {
		ret = -EINVAL;
		goto err_meta_done;
	}
	if (total_length > TBV_NATIVE_DATA_MAX_MSG_SIZE) {
		ret = -EMSGSIZE;
		goto err_meta_done;
	}
	if (!path->tx_ring) {
		ret = -ENOTCONN;
		goto err_meta_done;
	}
	if (hdr->opcode == TBV_NATIVE_DATA_OP_RDMA_WRITE ||
	    hdr->opcode == TBV_NATIVE_DATA_OP_RDMA_WRITE_IMM)
		max_raw_payload = TBV_DATA_FRAME_SIZE;
	else
		max_raw_payload = TBV_NATIVE_DATA_MAX_PAYLOAD;

	dma_dev = tb_ring_dma_device(path->tx_ring);
	if (!tbv_dma_device_ready(dma_dev)) {
		ret = -EPROBE_DEFER;
		goto err_meta_done;
	}

	hdr_buf = kzalloc(TBV_NATIVE_DATA_HDR_SIZE, GFP_KERNEL);
	if (!hdr_buf) {
		ret = -ENOMEM;
		goto err_release;
	}

	stream_hdr = *hdr;
	stream_hdr.length = total_length;
	stream_hdr.flags |= TBV_NATIVE_DATA_F_LAST |
			    TBV_NATIVE_DATA_F_RAW_STREAM;
	ret = tbv_native_data_build_header(hdr_buf, TBV_NATIVE_DATA_HDR_SIZE,
					   &stream_hdr);
	if (ret < 0) {
		kfree(hdr_buf);
		goto err_release;
	}

	packet = tbv_path_alloc_data_packet_owned(path, hdr_buf,
						  TBV_NATIVE_DATA_HDR_SIZE,
						  meta_done, meta_done_ctx);
	if (!packet) {
		kfree(hdr_buf);
		ret = -ENOMEM;
		goto err_release;
	}
	packet->raw_stream_start = true;
	list_add_tail(&packet->node, &packets);
	packet_count++;

	while (prepared < total_length) {
		tbv_path_tx_done_fn done = NULL;
		struct page *page = NULL;
		void *done_ctx = NULL;
		bool last;
		dma_addr_t dma;
		u32 page_off = 0;
		u32 len = 0;

		ret = next(next_ctx, &page, &page_off, &len, &done, &done_ctx);
		if (ret)
			goto err_release;
		if (!page || !len || len > max_raw_payload ||
		    len > total_length - prepared ||
		    page_off > PAGE_SIZE || len > PAGE_SIZE - page_off) {
			if (done)
				done(done_ctx, -EINVAL);
			ret = -EINVAL;
			goto err_release;
		}

		last = prepared + len == total_length;

		dma = dma_map_page(dma_dev, page, page_off, len,
				   DMA_TO_DEVICE);
		if (dma_mapping_error(dma_dev, dma)) {
			if (done)
				done(done_ctx, -EIO);
			ret = -EIO;
			goto err_release;
		}

		packet = tbv_path_alloc_zcopy_packet(path, dma, len, true,
						     done, done_ctx);
		if (!packet) {
			dma_unmap_page(dma_dev, dma, len, DMA_TO_DEVICE);
			if (done)
				done(done_ctx, -ENOMEM);
			ret = -ENOMEM;
			goto err_release;
		}
		packet->owner_ctx = meta_done_ctx ? meta_done_ctx : done_ctx;
		packet->raw_stream_end = last;
		list_add_tail(&packet->node, &packets);
		packet_count++;
		prepared += len;
	}

	ret = tbv_path_enqueue_data_list(path, &packets, packet_count,
					 send_flags & TBV_PATH_SEND_DEFER);
	if (ret)
		goto err_release;
	return 0;

err_release:
	if (!packet_count && meta_done)
		meta_done(meta_done_ctx, ret);
	tbv_path_release_packet_list(&packets, ret);
	return ret;

err_meta_done:
	if (meta_done)
		meta_done(meta_done_ctx, ret);
	return ret;
}

static bool tbv_path_packet_matches(const struct tbv_tx_packet *packet,
				    tbv_path_tx_done_fn done, void *done_ctx,
				    void *owner_ctx)
{
	if (owner_ctx && packet->owner_ctx == owner_ctx)
		return true;
	return done && done_ctx && packet->done == done &&
	       packet->done_ctx == done_ctx;
}

/*
 * Cancel a WR owner's packets on this path.
 *
 * Packets still in the software queue were never handed to the NHI: they are
 * released here and their done() runs with -ECANCELED. Packets whose
 * descriptor is already submitted are only MARKED canceled. Their done() and
 * the reference behind it stay attached to the frame until the ring retires
 * it, normally at completion or, if the NHI never completes it, at the
 * tb_ring_stop() barrier in tbv_path_ring_barrier(). The previous behaviour
 * (detach and invoke done() early, leave the descriptor in the ring) released
 * the QP reference while DMA on its behalf was still possible, which is how a
 * stuck frame turned into a destroy that "succeeded" with a live descriptor.
 *
 * Allocation-free by construction: this runs on teardown paths that must not
 * fail silently on -ENOMEM.
 */
static void tbv_path_cancel_data_match(struct tbv_path *path,
				       tbv_path_tx_done_fn done, void *done_ctx,
				       void *owner_ctx)
{
	struct tbv_tx_packet *packet;
	struct tbv_tx_packet *tmp;
	LIST_HEAD(cancel);
	unsigned long flags;
	u32 i;
	bool raw_stream_canceled = false;

	if ((!done || !done_ctx) && !owner_ctx)
		return;

	spin_lock_irqsave(&path->tx_lock, flags);
	if (owner_ctx && path->tx_raw_stream_active &&
	    path->tx_raw_stream_owner == owner_ctx) {
		path->tx_raw_stream_active = false;
		path->tx_raw_stream_owner = NULL;
		path->tx_raw_stream_end_seen = false;
		path->tx_raw_stream_inflight = 0;
		raw_stream_canceled = true;
	}
	list_for_each_entry_safe(packet, tmp, &path->tx_data_queue, node) {
		if (!tbv_path_packet_matches(packet, done, done_ctx,
					     owner_ctx))
			continue;
		list_del_init(&packet->node);
		packet->queued = false;
		if (path->tx_data_queued)
			path->tx_data_queued--;
		packet->owner_ctx = NULL;
		list_add_tail(&packet->node, &cancel);
	}

	for (i = 0; i < path->tx_frame_count; i++) {
		packet = path->tx_frames[i].packet;
		if (!packet || packet->control ||
		    !tbv_path_packet_matches(packet, done, done_ctx,
					     owner_ctx))
			continue;
		packet->canceled = true;
	}

	list_for_each_entry(packet, &path->tx_zcopy_inflight, node) {
		if (tbv_path_packet_matches(packet, done, done_ctx, owner_ctx))
			packet->canceled = true;
	}
	spin_unlock_irqrestore(&path->tx_lock, flags);

	while (!list_empty(&cancel)) {
		packet = list_first_entry(&cancel, struct tbv_tx_packet, node);
		list_del_init(&packet->node);
		tbv_path_tx_packet_release(packet, -ECANCELED);
	}

	if (raw_stream_canceled)
		tbv_path_schedule_tx(path);
}

void tbv_path_cancel_data_done_ctx(struct tbv_path *path,
				   tbv_path_tx_done_fn done, void *done_ctx)
{
	tbv_path_cancel_data_match(path, done, done_ctx, NULL);
}

void tbv_path_cancel_data_owner_ctx(struct tbv_path *path, void *owner_ctx)
{
	tbv_path_cancel_data_match(path, NULL, NULL, owner_ctx);
}

/*
 * Take every not-yet-submitted packet off the path and retire it with
 * @status. Submitted descriptors are untouched: they retire through the ring.
 * Does not clear tx_scheduling — a scheduler that is mid-loop must see its own
 * exit; it will stop on tx_closed or on empty queues.
 */
static void tbv_path_cancel_queued_tx_status(struct tbv_path *path, int status)
{
	LIST_HEAD(control);
	LIST_HEAD(data);
	unsigned long flags;

	spin_lock_irqsave(&path->tx_lock, flags);
	list_splice_init(&path->tx_control_queue, &control);
	list_splice_init(&path->tx_data_queue, &data);
	path->tx_control_queued = 0;
	path->tx_data_queued = 0;
	path->tx_data_reserved = 0;
	path->tx_raw_stream_active = false;
	path->tx_raw_stream_owner = NULL;
	path->tx_raw_stream_end_seen = false;
	path->tx_raw_stream_inflight = 0;
	spin_unlock_irqrestore(&path->tx_lock, flags);

	tbv_path_release_packet_list(&control, status);
	tbv_path_release_packet_list(&data, status);
}

void tbv_path_cancel_queued_tx(struct tbv_path *path)
{
	tbv_path_cancel_queued_tx_status(path, -ECANCELED);
}

static void tbv_path_flush_tx_queue(struct tbv_path *path, int status)
{
	unsigned long flags;

	tbv_path_cancel_queued_tx_status(path, status);
	spin_lock_irqsave(&path->tx_lock, flags);
	path->tx_scheduling = false;
	spin_unlock_irqrestore(&path->tx_lock, flags);
}

static bool tbv_path_tx_retired(struct tbv_path *path)
{
	unsigned long flags;
	bool retired;

	spin_lock_irqsave(&path->tx_lock, flags);
	retired = !atomic_read(&path->tx_inflight) &&
		  list_empty(&path->tx_control_queue) &&
		  list_empty(&path->tx_data_queue);
	spin_unlock_irqrestore(&path->tx_lock, flags);
	return retired;
}

int tbv_path_unretired_tx(struct tbv_path *path)
{
	return atomic_read(&path->tx_inflight);
}

/*
 * Reap TX descriptors the NHI has already marked COMPLETED but whose
 * interrupt or ring work never delivered them. tb_ring_poll() takes the
 * oldest in-flight frame off the ring under ring->lock only when its
 * descriptor carries RING_DESC_COMPLETED, exactly what ring_work() does, so
 * it is safe alongside the interrupt path and never reaps an incomplete
 * descriptor. Apple TX progress polling is deliberately off
 * (tbv_path_progress_poll_enabled), so without this a missed completion
 * notification would look identical to a frame the hardware never finished
 * and would only be ended by the ring-stop barrier — as canceled, not
 * completed.
 */
int tbv_path_reap_tx(struct tbv_path *path)
{
	struct tb_ring *ring = READ_ONCE(path->tx_ring);
	struct ring_frame *frame;
	int reaped = 0;

	if (!ring)
		return 0;
	while ((frame = tb_ring_poll(ring))) {
		if (frame->callback)
			frame->callback(ring, frame, false);
		reaped++;
	}
	if (reaped)
		atomic64_add(reaped, &path->tx_reaped);
	return reaped;
}

/*
 * Hardware-defined TX/RX descriptor layout (16 bytes, NHI spec). The kernel
 * keeps struct ring_desc private to drivers/thunderbolt/nhi_regs.h while
 * exporting enum ring_desc_flags and the ring indices; this mirror exists
 * only to read the COMPLETED bit of the oldest submitted descriptor for
 * diagnostics.
 */
struct tbv_nhi_ring_desc {
	u64 phys;
	/* length:12 | eof:4 | sof:4 | flags:12, LSB first */
	u32 ctrl;
	u32 time;
} __packed;

#define TBV_NHI_DESC_FLAGS(ctrl) ((ctrl) >> 20)

/*
 * NHI ring register access from the module.
 *
 * tb_nhi.ring_layout is public but its struct lives in drivers/thunderbolt/
 * nhi.h; this is a field-for-field mirror of that six-u32 struct (Asahi 7.2 /
 * upstream). A NULL layout means the upstream Intel layout. The descriptor
 * block is the same on every NHI (00 base, 08 hw/sw indices, 12 count) and
 * the options word carries the valid bit and E2E flow-control bit
 * (drivers/thunderbolt/nhi_regs.h enum ring_flags). tb_ring_start/stop drive
 * these same registers through the same layout; the module touches them only
 * to (a) read the hardware index for diagnostics and (b) clear the valid bit
 * and let the hardware settle before tb_ring_stop() clears the descriptor
 * base — see tbv_path_ring_hw_disable().
 */
struct tbv_nhi_ring_layout_view {
	u32 tx_desc_base;
	u32 rx_desc_base;
	u32 desc_stride;
	u32 tx_options_base;
	u32 rx_options_base;
	u32 options_stride;
};

static const struct tbv_nhi_ring_layout_view tbv_nhi_default_layout = {
	.tx_desc_base = 0x00000,
	.rx_desc_base = 0x08000,
	.desc_stride = 16,
	.tx_options_base = 0x19800,
	.rx_options_base = 0x29800,
	.options_stride = 32,
};

#define TBV_NHI_RING_FLAG_E2E_FLOW_CONTROL BIT(28)
#define TBV_NHI_RING_FLAG_VALID BIT(31)
#define TBV_NHI_RING_HW_INDEX_OFF 8

static const struct tbv_nhi_ring_layout_view *
tbv_nhi_ring_layout(const struct tb_ring *ring)
{
	if (ring->nhi->ring_layout)
		return (const struct tbv_nhi_ring_layout_view *)
			ring->nhi->ring_layout;
	return &tbv_nhi_default_layout;
}

static void __iomem *tbv_nhi_ring_desc_regs(struct tb_ring *ring)
{
	const struct tbv_nhi_ring_layout_view *l = tbv_nhi_ring_layout(ring);

	if (!ring->nhi->iobase)
		return NULL;
	return ring->nhi->iobase +
	       (ring->is_tx ? l->tx_desc_base : l->rx_desc_base) +
	       ring->hop * l->desc_stride;
}

static void __iomem *tbv_nhi_ring_options_regs(struct tb_ring *ring)
{
	const struct tbv_nhi_ring_layout_view *l = tbv_nhi_ring_layout(ring);

	if (!ring->nhi->iobase)
		return NULL;
	return ring->nhi->iobase +
	       (ring->is_tx ? l->tx_options_base : l->rx_options_base) +
	       ring->hop * l->options_stride;
}

/* TX: hardware consumer in the low 16 bits; RX: hardware producer in the high 16. */
static int tbv_nhi_ring_hw_index(struct tb_ring *ring)
{
	void __iomem *regs = tbv_nhi_ring_desc_regs(ring);
	u32 v;

	if (!regs)
		return -1;
	v = ioread32(regs + TBV_NHI_RING_HW_INDEX_OFF);
	return ring->is_tx ? (int)(v & 0xffff) : (int)(v >> 16);
}

static u32 tbv_nhi_ring_hw_options(struct tb_ring *ring)
{
	void __iomem *regs = tbv_nhi_ring_options_regs(ring);

	return regs ? ioread32(regs) : 0;
}

void tbv_path_tx_ring_snapshot(struct tbv_path *path,
			       struct tbv_path_ring_snapshot *snap)
{
	struct tb_ring *ring = READ_ONCE(path->tx_ring);
	const struct tbv_nhi_ring_desc *descs;
	struct list_head *pos;
	unsigned long flags;

	BUILD_BUG_ON(sizeof(struct tbv_nhi_ring_desc) != 16);
	BUILD_BUG_ON(sizeof(struct tbv_nhi_ring_layout_view) != 6 * sizeof(u32));
	memset(snap, 0, sizeof(*snap));
	snap->hw_index = -1;
	if (!ring)
		return;

	spin_lock_irqsave(&ring->lock, flags);
	snap->valid = true;
	snap->running = ring->running;
	snap->head = ring->head;
	snap->tail = ring->tail;
	snap->size = ring->size;
	snap->ring_flags = ring->flags;
	snap->e2e_tx_hop = ring->e2e_tx_hop;
	snap->hw_index = tbv_nhi_ring_hw_index(ring);
	snap->hw_options = tbv_nhi_ring_hw_options(ring);
	if (ring->size > 0)
		snap->outstanding = (ring->head - ring->tail + ring->size) %
				    ring->size;
	list_for_each(pos, &ring->queue)
		snap->sw_queued++;
	list_for_each(pos, &ring->in_flight)
		snap->in_flight++;
	descs = (const struct tbv_nhi_ring_desc *)ring->descriptors;
	if (descs && snap->outstanding) {
		snap->tail_flags =
			TBV_NHI_DESC_FLAGS(READ_ONCE(descs[ring->tail].ctrl));
		snap->tail_completed = !!(snap->tail_flags & RING_DESC_COMPLETED);
	}
	spin_unlock_irqrestore(&ring->lock, flags);
}

/*
 * Wait, bounded by @deadline (jiffies), for every submitted TX descriptor to
 * retire through its completion callback, reaping completed-but-unnotified
 * descriptors from the ring tail between bounded waits. Call with TX
 * admission closed and the queues canceled, otherwise the predicate can be
 * satisfied late or never.
 */
int tbv_path_drain_tx(struct tbv_path *path, unsigned long deadline)
{
	for (;;) {
		long left;

		tbv_path_reap_tx(path);
		if (tbv_path_tx_retired(path))
			return 0;
		left = (long)deadline - (long)jiffies;
		if (left <= 0)
			return -ETIMEDOUT;
		wait_event_timeout(path->tx_retire_wait,
				   tbv_path_tx_retired(path),
				   min_t(long, left,
					 msecs_to_jiffies(TBV_TX_REAP_SLICE_MS)));
	}
}

static bool tbv_path_rings_started(const struct tbv_path *path)
{
	return path->state == TBV_PATH_RING_STARTED ||
	       path->state == TBV_PATH_TUNNEL_ENABLED;
}

static int tbv_path_restart_rings(struct tbv_path *path)
{
	u32 i;
	int ret;

	pr_info("ring barrier: phase restart begin tx hop %d rx hop %d\n",
		path->tx_ring->hop, path->rx_ring->hop);
	tbv_path_apply_ring_interval(path);
	tb_ring_start(path->tx_ring);
	tb_ring_start(path->rx_ring);
	tbv_path_apple_throttle_readback(path);
	pr_info("ring barrier: phase restart end (tx base=%pad rx base=%pad)\n",
		&path->tx_ring->descriptors_dma, &path->rx_ring->descriptors_dma);
	path->rings_stopped = false;
	path->rx_raw_pending = false;
	path->rx_raw_remaining = 0;
	for (i = 0; i < path->rx_frame_count; i++) {
		ret = tbv_path_post_rx_frame(&path->rx_frames[i]);
		if (ret) {
			pr_err("ring barrier: RX repost %u/%u failed after restart ret=%d; path stays quarantined\n",
			       i, path->rx_frame_count, ret);
			return ret;
		}
	}
	return 0;
}

/*
 * Retirement barrier for descriptors the NHI never completed.
 *
 * tb_ring_stop() (verified against the loaded Asahi 7.2.2 nhi.c) disables the
 * ring, marks it not running, then schedules and FLUSHES ring->work, which
 * moves every in_flight and queued frame to the done list and invokes its
 * callback with canceled=true. So when it returns, every submitted frame of
 * ours has run tbv_path_tx_complete()/tbv_path_zcopy_tx_complete() and the
 * owner references behind them are released; no callback of this ring
 * incarnation can run afterwards. Must be called from sleepable context with
 * no callback-side locks held, and never from a ring callback.
 *
 * With @restart the pair is started again and the RX frames reposted so the
 * still-enabled tunnel keeps a live receive side; the caller decides whether
 * the rail may be re-bound (quarantine). Without it the rings stay stopped for
 * tbv_path_destroy(), which then skips its own tb_ring_stop().
 *
 * Returns 0 when nothing is left unretired, -EIO when frames are still
 * outstanding after the stop (the NHI refused: nhi->going_away), or the
 * restart error.
 */
/*
 * Hardware-side pre-stop. Measured 2026-09-06 on zeus (apple-dart
 * "translation fault ... NO PMD FOR IOVA at 0x10000000000" right after a
 * barrier with 6 E2E-blocked TX descriptors): tb_ring_stop() clears the ring
 * options word and the descriptor base register back to back inside one
 * locked section, and the Apple NHI firmware still had a descriptor fetch in
 * flight when the base went to zero. Nothing of ours was unmapped — the
 * descriptor ring and the frame buffers stay mapped across a barrier restart
 * and are released only in tbv_path_destroy() — so the fetch was against the
 * cleared base.
 *
 * So, before tb_ring_stop(): clear the valid and E2E flow-control bits in the
 * options word ourselves (the same register tb_ring_stop() zeroes first),
 * read it back so the posted write has landed, then require the hardware
 * index register to hold still for TBV_RING_SETTLE_SAMPLES consecutive
 * samples spanning at least TBV_RING_SETTLE_MIN_MS, bounded by
 * TBV_RING_SETTLE_TIMEOUT_MS. Only then may the base be cleared. The source
 * offers no "ring idle" status bit on either NHI; a steady hardware index
 * after the valid bit is gone is the strongest local observation available,
 * and whether the firmware can still hold a prefetched frame is what the
 * post-barrier stale probe in tools/ci/teardown-smoke.sh checks.
 */
#define TBV_RING_SETTLE_SAMPLES 8
#define TBV_RING_SETTLE_MIN_MS 5
#define TBV_RING_SETTLE_TIMEOUT_MS 200

/*
 * Pre-stop record for fault attribution. tb_ring_stop() clears the 64-bit
 * descriptor base as two 32-bit writes, low word first (nhi.c
 * ring_iowrite64desc), so for one write's duration the hardware sees
 * base = (high32 << 32). On zeus the ring lives in the DART aperture starting
 * at 0x100_00000000, which makes that torn intermediate exactly the aperture
 * start — the address the 2026-09-06 translation fault named. Log it so a
 * fault address can be matched against it directly.
 */
static void tbv_path_ring_log_before_stop(struct tb_ring *ring, const char *what)
{
	u64 base = (u64)ring->descriptors_dma;
	unsigned long flags;
	u32 options;
	int hw_index;

	spin_lock_irqsave(&ring->lock, flags);
	options = tbv_nhi_ring_hw_options(ring);
	hw_index = tbv_nhi_ring_hw_index(ring);
	spin_unlock_irqrestore(&ring->lock, flags);

	pr_info("ring barrier: %s hop %d before stop: descriptors_dma=0x%016llx (hi=0x%08x lo=0x%08x torn_base=0x%016llx) size=%d sw head=%d tail=%d hw_index=%d hw_options=0x%08x\n",
		what, ring->hop, base, upper_32_bits(base), lower_32_bits(base),
		base & ~0xffffffffULL, ring->size, ring->head, ring->tail,
		hw_index, options);
}

/*
 * NOTE on the settle predicate: an E2E-blocked consumer is already
 * stationary, so a steady hardware index cannot distinguish "idle" from
 * "blocked DMA machinery". It is the right register to watch and the only
 * one the source offers, but it is not proof that the firmware will not
 * fetch; the phase timestamps and torn_base above are what attribute a
 * fault. base/size/descriptors are left intact here on purpose — only
 * tb_ring_stop() touches them.
 */
static void tbv_path_ring_hw_disable(struct tb_ring *ring, const char *what)
{
	void __iomem *opts = tbv_nhi_ring_options_regs(ring);
	unsigned long flags;
	unsigned long start = jiffies;
	unsigned long deadline = start + msecs_to_jiffies(TBV_RING_SETTLE_TIMEOUT_MS);
	u32 before = 0;
	u32 after = 0;
	int idx = -1;
	int last = -2;
	int steady = 0;
	bool settled = false;

	if (!opts) {
		pr_warn("ring barrier: %s hop %d has no MMIO base; cannot pre-disable, relying on tb_ring_stop()\n",
			what, ring->hop);
		return;
	}

	spin_lock_irqsave(&ring->lock, flags);
	before = ioread32(opts);
	iowrite32(before & ~(TBV_NHI_RING_FLAG_VALID |
			     TBV_NHI_RING_FLAG_E2E_FLOW_CONTROL), opts);
	after = ioread32(opts);
	spin_unlock_irqrestore(&ring->lock, flags);

	for (;;) {
		idx = tbv_nhi_ring_hw_index(ring);
		steady = idx == last ? steady + 1 : 1;
		last = idx;
		if (steady >= TBV_RING_SETTLE_SAMPLES &&
		    time_after_eq(jiffies, start +
				  msecs_to_jiffies(TBV_RING_SETTLE_MIN_MS))) {
			settled = true;
			break;
		}
		if (time_after(jiffies, deadline))
			break;
		usleep_range(1000, 1500);
	}

	pr_info("ring barrier: %s hop %d valid bit cleared (options 0x%08x -> 0x%08x), hw index %s at %d after %u ms (%d samples; stationary means blocked-or-idle, not proven idle), sw head=%d tail=%d\n",
		what, ring->hop, before, after,
		settled ? "stationary" : "STILL MOVING", idx,
		jiffies_to_msecs(jiffies - start), steady, ring->head,
		ring->tail);
	if (!settled)
		pr_err("ring barrier: %s hop %d hardware index did not settle within %u ms after the valid bit was cleared; clearing the base now may fault (apple-dart translation fault)\n",
		       what, ring->hop, TBV_RING_SETTLE_TIMEOUT_MS);
}

int tbv_path_ring_barrier(struct tbv_path *path, bool restart)
{
	int unretired;

	if (!tbv_path_rings_started(path) || !path->tx_ring || !path->rx_ring)
		return atomic_read(&path->tx_inflight) ? -EIO : 0;

	if (!path->rings_stopped) {
		/* Last chance to retire as completed rather than canceled. */
		tbv_path_reap_tx(path);
		tbv_path_ring_log_before_stop(path->tx_ring, "tx");
		tbv_path_ring_log_before_stop(path->rx_ring, "rx");
		tbv_path_ring_hw_disable(path->tx_ring, "tx");
		tbv_path_ring_hw_disable(path->rx_ring, "rx");
		/*
		 * Separate timestamped phases so an apple-dart translation fault
		 * in dmesg can be attributed to the TX stop, the RX stop or the
		 * restart, and its address compared with the torn_base values
		 * logged above.
		 */
		pr_info("ring barrier: phase tx-stop begin hop %d\n",
			path->tx_ring->hop);
		tb_ring_stop(path->tx_ring);
		pr_info("ring barrier: phase tx-stop end hop %d\n",
			path->tx_ring->hop);
		pr_info("ring barrier: phase rx-stop begin hop %d\n",
			path->rx_ring->hop);
		tb_ring_stop(path->rx_ring);
		pr_info("ring barrier: phase rx-stop end hop %d\n",
			path->rx_ring->hop);
		path->rings_stopped = true;
	}

	unretired = atomic_read(&path->tx_inflight);
	if (unretired) {
		pr_err("ring barrier failed: %d TX frames still unretired after tb_ring_stop() on hop %d (NHI going away?)\n",
		       unretired, path->local_tx_hop);
		return -EIO;
	}
	if (!restart)
		return 0;
	return tbv_path_restart_rings(path);
}

void tbv_path_destroy(struct tbv_path *path, struct tb_xdomain *xd)
{
	bool tunnel_enabled = path->state == TBV_PATH_TUNNEL_ENABLED;
	bool rings_started = tbv_path_rings_started(path);

	cancel_delayed_work_sync(&path->tx_poll_work);
	cancel_delayed_work_sync(&path->rx_supp_poll_work);
	cancel_delayed_work_sync(&path->tx_watchdog_work);

	if (tunnel_enabled) {
		int ret = tbv_path_disable_tunnel(path, xd);

		if (ret)
			pr_warn("disable tunnel route path tx=%d rx=%d failed: %d\n",
				path->local_transmit_path,
				path->remote_transmit_path, ret);
		if (ret && path->remote_transmit_path >= 0) {
			tb_xdomain_release_in_hopid(xd, path->remote_transmit_path);
			path->remote_transmit_path = -1;
		}
	}
	if (rings_started) {
		if (!path->rings_stopped) {
			if (path->rx_ring)
				tb_ring_stop(path->rx_ring);
			if (path->tx_ring)
				tb_ring_stop(path->tx_ring);
			path->rings_stopped = true;
		}
		path->state = TBV_PATH_RING_ALLOCATED;
	}
	if (!tunnel_enabled && path->remote_transmit_path >= 0) {
		tb_xdomain_release_in_hopid(xd, path->remote_transmit_path);
		path->remote_transmit_path = -1;
	}

	tbv_path_flush_tx_queue(path, -ECANCELED);

	if (path->rx_ring) {
		tbv_thr_del(path->rx_ring);
		tbv_path_free_frames(path, false);
		tb_ring_free(path->rx_ring);
		path->rx_ring = NULL;
		path->local_rx_hop = -1;
	}

	if (path->local_transmit_path >= 0) {
		tb_xdomain_release_out_hopid(xd, path->local_transmit_path);
		path->local_transmit_path = -1;
	}

	if (path->tx_ring) {
		tbv_thr_del(path->tx_ring);
		tbv_path_free_frames(path, true);
		tb_ring_free(path->tx_ring);
		path->tx_ring = NULL;
		path->local_tx_hop = -1;
	}
	tbv_path_free_data_packets(path);
	tbv_path_free_control_packets(path);
	tbv_txtrace_free(path->tx_trace);
	path->tx_trace = NULL;
	tbv_path_destroy_tx_wq(path);

	path->state = TBV_PATH_STOPPED;
}
