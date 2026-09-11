/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Per-WR TX stage timestamps for the Apple-compatible SEND path.
 *
 * Why this exists: the 2026-09-06 timing matrix (39 cells, group x size x
 * depth, zeus -> M4 Max) shows a 2.4-4.6 ms maximum in 38 of 39 cells,
 * including 4 KiB at depth 1 where p50 is 25 us and p99 is 80 us. It does not
 * scale with size, depth, group or rate, so it is not queueing. A governor A/B
 * (performance governor + irqbalance off) moved p50 from 192-768 us to
 * 50-80 us at 64 KiB/d8 and left the maximum unchanged in every cell of both
 * arms, so it is not CPU frequency, EAS or IRQ balancing either.
 *
 * Guessing further is how this repository has produced its wrong answers. This
 * instrument attributes the event to exactly one interval instead.
 *
 * Honest naming: FIRST_CB/LAST_CB are the first and last TX completion
 * CALLBACKS the module observes for a WR. They are an observation bound on the
 * hardware interrupt, not the interrupt itself: the Apple NHI IRQ handler
 * (Asahi apple.c apple_cio_ring_irq) does schedule_work(&ring->work), and
 * ring_work() then runs our callbacks. The module cannot see IRQ entry.
 */
#ifndef TBV_TXTRACE_H
#define TBV_TXTRACE_H

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/types.h>

struct seq_file;

/* Stage stamps, in the order a WR passes through them. */
enum tbv_tx_stage {
	TBV_TX_STAGE_POST_ENTRY,	/* ibv_post_send -> tbv_post_apple_send */
	TBV_TX_STAGE_POST_HANDOFF,	/* last point the posting thread owns the WR */
	TBV_TX_STAGE_WORKER_START,	/* tbv_apple_sq_transmit entry */
	TBV_TX_STAGE_FIRST_PUBLISH,	/* first descriptor handed to the path */
	TBV_TX_STAGE_LAST_PUBLISH,	/* last descriptor handed to the path */
	TBV_TX_STAGE_FIRST_CB,		/* first TX completion callback observed */
	TBV_TX_STAGE_LAST_CB,		/* last frame reaped */
	TBV_TX_STAGE_CQ,		/* kernel CQ insertion */
	TBV_TX_STAGE_COUNT,
};

/* Intervals summarised in peers. */
enum tbv_tx_ival {
	TBV_TX_IVAL_POST,	/* post entry     -> handoff  (alloc + copy) */
	TBV_TX_IVAL_DISPATCH,	/* handoff        -> worker start */
	TBV_TX_IVAL_ADMIT,	/* worker start   -> first publish */
	TBV_TX_IVAL_PUBLISH,	/* first publish  -> last publish (group gates) */
	TBV_TX_IVAL_NOTIFY,	/* last publish   -> first callback */
	TBV_TX_IVAL_REAP,	/* first callback -> last callback */
	TBV_TX_IVAL_CQ,		/* last callback  -> kernel CQ insertion */
	TBV_TX_IVAL_TOTAL,	/* post entry     -> kernel CQ insertion */
	TBV_TX_IVAL_COUNT,
};

/*
 * Log-linear histogram: exact nanoseconds below 64 ns, then 8 sub-buckets per
 * octave (12.5% resolution) up to 2^45 ns. Percentiles are reported at the
 * bucket's lower bound; the maximum is kept exactly, separately.
 */
#define TBV_TX_HIST_LINEAR 64
#define TBV_TX_HIST_SUBBITS 3
#define TBV_TX_HIST_SUB (1u << TBV_TX_HIST_SUBBITS)
#define TBV_TX_HIST_BASE_EXP 6
#define TBV_TX_HIST_OCTAVES 40
#define TBV_TX_HIST_BUCKETS \
	(TBV_TX_HIST_LINEAR + TBV_TX_HIST_OCTAVES * TBV_TX_HIST_SUB)

#define TBV_TX_TRACE_RECS 4096
#define TBV_TX_TRACE_WORST 32

/* Per-WR stamps, carried in the send context and copied into the ring at CQ. */
struct tbv_tx_stamps {
	u64 t[TBV_TX_STAGE_COUNT];
	u32 length;
	u32 frames;
	u32 cpu_post;
	u32 cpu_worker;
	u32 cpu_reap;
	u32 cpu_cq;
	bool active;
};

struct tbv_tx_trace_rec {
	u64 t[TBV_TX_STAGE_COUNT];
	u64 wr_id;
	u64 seq;
	u32 qpn;
	u32 psn;
	u32 length;
	u32 frames;
	u32 cpu_post;
	u32 cpu_worker;
	u32 cpu_reap;
	u32 cpu_cq;
	s32 status;
};

struct tbv_tx_trace {
	spinlock_t lock;
	struct tbv_tx_trace_rec *recs;	/* NULL if the ring could not be allocated */
	u32 head;
	u32 filled;
	u64 seq;
	u64 count;
	u64 hist[TBV_TX_IVAL_COUNT][TBV_TX_HIST_BUCKETS];
	u64 max_ns[TBV_TX_IVAL_COUNT];
	u64 sum_ns[TBV_TX_IVAL_COUNT];
	struct tbv_tx_trace_rec worst[TBV_TX_TRACE_WORST];
	u32 worst_used;
};

bool tbv_txtrace_enabled(void);

static inline u64 tbv_txtrace_now(void)
{
	return tbv_txtrace_enabled() ? ktime_get_ns() : 0;
}

static inline void tbv_txtrace_stamp(struct tbv_tx_stamps *st,
				     enum tbv_tx_stage stage)
{
	if (st->active)
		WRITE_ONCE(st->t[stage], ktime_get_ns());
}

static inline void tbv_txtrace_stamp_once(struct tbv_tx_stamps *st,
					  enum tbv_tx_stage stage)
{
	if (st->active && !READ_ONCE(st->t[stage]))
		WRITE_ONCE(st->t[stage], ktime_get_ns());
}

struct tbv_tx_trace *tbv_txtrace_alloc(void);
void tbv_txtrace_free(struct tbv_tx_trace *tr);
void tbv_txtrace_record(struct tbv_tx_trace *tr,
			const struct tbv_tx_stamps *st, u64 wr_id, u32 qpn,
			u32 psn, int status);
void tbv_txtrace_show_summary(struct seq_file *s, struct tbv_tx_trace *tr);
void tbv_txtrace_show_records(struct seq_file *s, struct tbv_tx_trace *tr,
			      const char *label);

#endif /* TBV_TXTRACE_H */
