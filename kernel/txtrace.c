// SPDX-License-Identifier: GPL-2.0

#define pr_fmt(fmt) "thunderbolt_ibverbs: " fmt

#include <linux/bitops.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "txtrace.h"

static bool tx_stage_trace = true;
module_param(tx_stage_trace, bool, 0644);
MODULE_PARM_DESC(tx_stage_trace,
		 "Record per-WR TX stage timestamps (post entry, handoff, SQ worker start, first/last descriptor published, first/last completion callback, kernel CQ insertion). One ktime_get_ns() per stage; default on. debugfs: tx_trace, summary in peers");

bool tbv_txtrace_enabled(void)
{
	return READ_ONCE(tx_stage_trace);
}

static const char *const tbv_tx_stage_names[TBV_TX_STAGE_COUNT] = {
	"post_entry", "post_handoff", "worker_start", "first_publish",
	"last_publish", "first_cb", "last_cb", "cq",
};

static const char *const tbv_tx_ival_names[TBV_TX_IVAL_COUNT] = {
	"post", "dispatch", "admit", "publish", "notify", "reap", "cq", "total",
};

static const struct {
	enum tbv_tx_stage from;
	enum tbv_tx_stage to;
} tbv_tx_ival_span[TBV_TX_IVAL_COUNT] = {
	[TBV_TX_IVAL_POST] = { TBV_TX_STAGE_POST_ENTRY, TBV_TX_STAGE_POST_HANDOFF },
	[TBV_TX_IVAL_DISPATCH] = { TBV_TX_STAGE_POST_HANDOFF, TBV_TX_STAGE_WORKER_START },
	[TBV_TX_IVAL_ADMIT] = { TBV_TX_STAGE_WORKER_START, TBV_TX_STAGE_FIRST_PUBLISH },
	[TBV_TX_IVAL_PUBLISH] = { TBV_TX_STAGE_FIRST_PUBLISH, TBV_TX_STAGE_LAST_PUBLISH },
	[TBV_TX_IVAL_NOTIFY] = { TBV_TX_STAGE_LAST_PUBLISH, TBV_TX_STAGE_FIRST_CB },
	[TBV_TX_IVAL_REAP] = { TBV_TX_STAGE_FIRST_CB, TBV_TX_STAGE_LAST_CB },
	[TBV_TX_IVAL_CQ] = { TBV_TX_STAGE_LAST_CB, TBV_TX_STAGE_CQ },
	[TBV_TX_IVAL_TOTAL] = { TBV_TX_STAGE_POST_ENTRY, TBV_TX_STAGE_CQ },
};

/*
 * An interval whose endpoints were not both reached is not a measurement, and
 * a negative span is a real ordering fact (FIRST_CB can precede LAST_PUBLISH
 * on a multi-group message). Report both cases as "no sample" rather than
 * folding them into the zero bucket, so a percentile cannot be flattered by
 * records that never happened.
 */
static bool tbv_txtrace_span(const struct tbv_tx_trace_rec *rec,
			     enum tbv_tx_ival ival, u64 *ns)
{
	u64 a = rec->t[tbv_tx_ival_span[ival].from];
	u64 b = rec->t[tbv_tx_ival_span[ival].to];

	if (!a || !b || b < a)
		return false;
	*ns = b - a;
	return true;
}

static u32 tbv_txtrace_bucket(u64 ns)
{
	u32 exp, sub;

	if (ns < TBV_TX_HIST_LINEAR)
		return (u32)ns;

	exp = fls64(ns) - 1;
	if (exp < TBV_TX_HIST_BASE_EXP)
		exp = TBV_TX_HIST_BASE_EXP;
	if (exp - TBV_TX_HIST_BASE_EXP >= TBV_TX_HIST_OCTAVES)
		return TBV_TX_HIST_BUCKETS - 1;

	sub = (u32)((ns >> (exp - TBV_TX_HIST_SUBBITS)) & (TBV_TX_HIST_SUB - 1));
	return TBV_TX_HIST_LINEAR +
	       (exp - TBV_TX_HIST_BASE_EXP) * TBV_TX_HIST_SUB + sub;
}

static u64 tbv_txtrace_bucket_floor(u32 bucket)
{
	u32 off, exp, sub;

	if (bucket < TBV_TX_HIST_LINEAR)
		return bucket;

	off = bucket - TBV_TX_HIST_LINEAR;
	exp = TBV_TX_HIST_BASE_EXP + off / TBV_TX_HIST_SUB;
	sub = off % TBV_TX_HIST_SUB;
	return (1ULL << exp) + ((u64)sub << (exp - TBV_TX_HIST_SUBBITS));
}

struct tbv_txtrace_stats {
	u64 samples;
	u64 p50;
	u64 p90;
	u64 p99;
	u64 max;
	u64 mean;
};

/*
 * One walk of the 384-bucket histogram, in place under the ring lock. The
 * histogram is never copied to the stack: at 8 bytes a bucket that is a 3 KiB
 * frame, which the compiler rightly refuses.
 */
static void tbv_txtrace_stats(struct tbv_tx_trace *tr, unsigned int ival,
			      struct tbv_txtrace_stats *out)
{
	const u64 *hist = tr->hist[ival];
	u64 want50, want90, want99;
	u64 total = 0;
	u64 seen = 0;
	u32 i;

	memset(out, 0, sizeof(*out));
	for (i = 0; i < TBV_TX_HIST_BUCKETS; i++)
		total += hist[i];

	out->samples = total;
	out->max = tr->max_ns[ival];
	if (!total)
		return;

	out->mean = div64_u64(tr->sum_ns[ival], total);
	want50 = max_t(u64, 1, div64_u64(total * 50 + 99, 100));
	want90 = max_t(u64, 1, div64_u64(total * 90 + 99, 100));
	want99 = max_t(u64, 1, div64_u64(total * 99 + 99, 100));

	for (i = 0; i < TBV_TX_HIST_BUCKETS; i++) {
		seen += hist[i];
		if (!out->p50 && seen >= want50)
			out->p50 = tbv_txtrace_bucket_floor(i);
		if (!out->p90 && seen >= want90)
			out->p90 = tbv_txtrace_bucket_floor(i);
		if (seen >= want99) {
			out->p99 = tbv_txtrace_bucket_floor(i);
			return;
		}
	}
	out->p99 = tbv_txtrace_bucket_floor(TBV_TX_HIST_BUCKETS - 1);
}

struct tbv_tx_trace *tbv_txtrace_alloc(void)
{
	struct tbv_tx_trace *tr = kvzalloc(sizeof(*tr), GFP_KERNEL);

	if (!tr)
		return NULL;

	spin_lock_init(&tr->lock);
	tr->recs = kvcalloc(TBV_TX_TRACE_RECS, sizeof(*tr->recs), GFP_KERNEL);
	if (!tr->recs)
		pr_warn("tx stage trace: no memory for the %u-record ring; histograms only\n",
			TBV_TX_TRACE_RECS);
	return tr;
}

void tbv_txtrace_free(struct tbv_tx_trace *tr)
{
	if (!tr)
		return;
	kvfree(tr->recs);
	kvfree(tr);
}

/* Keep the worst TBV_TX_TRACE_WORST records by total, newest wins on a tie. */
static void tbv_txtrace_note_worst(struct tbv_tx_trace *tr,
				   const struct tbv_tx_trace_rec *rec)
{
	u64 total;
	u64 other;
	u32 i, slot;

	if (!tbv_txtrace_span(rec, TBV_TX_IVAL_TOTAL, &total))
		return;

	if (tr->worst_used < TBV_TX_TRACE_WORST) {
		tr->worst[tr->worst_used++] = *rec;
		return;
	}

	slot = 0;
	for (i = 1; i < TBV_TX_TRACE_WORST; i++) {
		u64 cur = 0, min = 0;

		tbv_txtrace_span(&tr->worst[i], TBV_TX_IVAL_TOTAL, &cur);
		tbv_txtrace_span(&tr->worst[slot], TBV_TX_IVAL_TOTAL, &min);
		if (cur < min)
			slot = i;
	}

	other = 0;
	tbv_txtrace_span(&tr->worst[slot], TBV_TX_IVAL_TOTAL, &other);
	if (total >= other)
		tr->worst[slot] = *rec;
}

void tbv_txtrace_record(struct tbv_tx_trace *tr,
			const struct tbv_tx_stamps *st, u64 wr_id, u32 qpn,
			u32 psn, int status)
{
	struct tbv_tx_trace_rec rec = {};
	unsigned long flags;
	unsigned int i;

	if (!tr || !st->active)
		return;

	memcpy(rec.t, st->t, sizeof(rec.t));
	rec.wr_id = wr_id;
	rec.qpn = qpn;
	rec.psn = psn;
	rec.length = st->length;
	rec.frames = st->frames;
	rec.cpu_post = st->cpu_post;
	rec.cpu_worker = st->cpu_worker;
	rec.cpu_reap = st->cpu_reap;
	rec.cpu_cq = st->cpu_cq;
	rec.status = status;

	spin_lock_irqsave(&tr->lock, flags);
	rec.seq = tr->seq++;
	tr->count++;
	for (i = 0; i < TBV_TX_IVAL_COUNT; i++) {
		u64 ns;

		if (!tbv_txtrace_span(&rec, i, &ns))
			continue;
		tr->hist[i][tbv_txtrace_bucket(ns)]++;
		tr->sum_ns[i] += ns;
		if (ns > tr->max_ns[i])
			tr->max_ns[i] = ns;
	}
	tbv_txtrace_note_worst(tr, &rec);
	if (tr->recs) {
		tr->recs[tr->head] = rec;
		tr->head = (tr->head + 1) % TBV_TX_TRACE_RECS;
		if (tr->filled < TBV_TX_TRACE_RECS)
			tr->filled++;
	}
	spin_unlock_irqrestore(&tr->lock, flags);
}

void tbv_txtrace_show_summary(struct seq_file *s, struct tbv_tx_trace *tr)
{
	struct tbv_txtrace_stats st;
	unsigned long flags;
	unsigned int i;
	u64 count;
	u32 filled;

	if (!tr)
		return;

	spin_lock_irqsave(&tr->lock, flags);
	count = tr->count;
	filled = tr->filled;
	spin_unlock_irqrestore(&tr->lock, flags);

	seq_printf(s, "    tx_stage records=%llu ring=%u/%u (ns)\n", count,
		   tr->recs ? filled : 0, tr->recs ? TBV_TX_TRACE_RECS : 0);

	for (i = 0; i < TBV_TX_IVAL_COUNT; i++) {
		spin_lock_irqsave(&tr->lock, flags);
		tbv_txtrace_stats(tr, i, &st);
		spin_unlock_irqrestore(&tr->lock, flags);

		seq_printf(s,
			   "      %-12s n=%llu p50=%llu p90=%llu p99=%llu max=%llu mean=%llu\n",
			   tbv_tx_ival_names[i], st.samples, st.p50, st.p90,
			   st.p99, st.max, st.mean);
	}
}

static void tbv_txtrace_show_rec(struct seq_file *s,
				 const struct tbv_tx_trace_rec *rec)
{
	unsigned int i;
	u64 base = rec->t[TBV_TX_STAGE_POST_ENTRY];

	seq_printf(s,
		   "seq=%llu qpn=%u psn=%u len=%u frames=%u status=%d cpu post=%u worker=%u reap=%u cq=%u wr_id=0x%llx base_ns=%llu",
		   rec->seq, rec->qpn, rec->psn, rec->length, rec->frames,
		   rec->status, rec->cpu_post, rec->cpu_worker, rec->cpu_reap,
		   rec->cpu_cq, rec->wr_id, base);

	for (i = 0; i < TBV_TX_STAGE_COUNT; i++) {
		if (!rec->t[i])
			seq_printf(s, " %s=-", tbv_tx_stage_names[i]);
		else if (rec->t[i] >= base)
			seq_printf(s, " %s=+%llu", tbv_tx_stage_names[i],
				   rec->t[i] - base);
		else
			seq_printf(s, " %s=-%llu", tbv_tx_stage_names[i],
				   base - rec->t[i]);
	}

	for (i = 0; i < TBV_TX_IVAL_COUNT; i++) {
		u64 ns;

		if (tbv_txtrace_span(rec, i, &ns))
			seq_printf(s, " d_%s=%llu", tbv_tx_ival_names[i], ns);
		else
			seq_printf(s, " d_%s=-", tbv_tx_ival_names[i]);
	}
	seq_puts(s, "\n");
}

/*
 * Records are copied one at a time under the ring lock, so a very long read
 * concurrent with traffic can see the ring wrap. Each line carries its own
 * seq number, which makes that visible instead of silent.
 */
void tbv_txtrace_show_records(struct seq_file *s, struct tbv_tx_trace *tr,
			      const char *label)
{
	struct tbv_tx_trace_rec rec;
	unsigned long flags;
	u32 used, worst_used;
	u32 i;

	if (!tr) {
		seq_printf(s, "# %s: no trace ring\n", label);
		return;
	}

	spin_lock_irqsave(&tr->lock, flags);
	worst_used = tr->worst_used;
	spin_unlock_irqrestore(&tr->lock, flags);

	seq_printf(s, "# %s worst %u by total\n", label, worst_used);
	for (i = 0; i < worst_used; i++) {
		spin_lock_irqsave(&tr->lock, flags);
		rec = tr->worst[i];
		spin_unlock_irqrestore(&tr->lock, flags);
		tbv_txtrace_show_rec(s, &rec);
	}

	spin_lock_irqsave(&tr->lock, flags);
	used = tr->filled;
	spin_unlock_irqrestore(&tr->lock, flags);

	seq_printf(s, "# %s ring %u records, oldest first\n", label, used);
	if (!tr->recs)
		return;

	for (i = 0; i < used; i++) {
		spin_lock_irqsave(&tr->lock, flags);
		rec = tr->recs[(tr->head + TBV_TX_TRACE_RECS - used + i) %
			       TBV_TX_TRACE_RECS];
		spin_unlock_irqrestore(&tr->lock, flags);
		tbv_txtrace_show_rec(s, &rec);
	}
}
