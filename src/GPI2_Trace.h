/*
Copyright (c) Fraunhofer ITWM, 2013-2026

This file is part of GPI-2.

GPI-2 is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License
version 3 as published by the Free Software Foundation.

GPI-2 is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GPI-2. If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef GPI2_TRACE_H
#define GPI2_TRACE_H

/* Category bitmask */
#define GPI2_TRACE_CAT_INIT    (1u << 0)
#define GPI2_TRACE_CAT_SEG     (1u << 1)
#define GPI2_TRACE_CAT_IO      (1u << 2)
#define GPI2_TRACE_CAT_COLL    (1u << 3)
#define GPI2_TRACE_CAT_GRP     (1u << 4)
#define GPI2_TRACE_CAT_ATOMIC  (1u << 5)
#define GPI2_TRACE_CAT_PASSIVE (1u << 6)
#define GPI2_TRACE_CAT_SN      (1u << 7)
#define GPI2_TRACE_CAT_ALL     0xFFFFFFFFu

/* Event IDs — indexes into static event info table */
enum gpi2_trace_event
{
  /* init */
  GPI2_EV_PROC_INIT,
  GPI2_EV_SN_SETUP,
  GPI2_EV_PARSE_MFILE,
  GPI2_EV_BROADCAST_TOPO,
  GPI2_EV_INIT_CORE,
  GPI2_EV_SN_WAIT,
  GPI2_EV_BUILD_INFRA,
  GPI2_EV_PROC_TERM,
  /* segments */
  GPI2_EV_SEG_CREATE,
  GPI2_EV_SEG_ALLOC,
  GPI2_EV_SEG_REGISTER,
  GPI2_EV_SEG_DELETE,
  /* groups */
  GPI2_EV_GRP_CREATE,
  GPI2_EV_GRP_DELETE,
  GPI2_EV_GRP_COMMIT,
  /* collectives */
  GPI2_EV_BARRIER,
  GPI2_EV_ALLREDUCE,
  /* io */
  GPI2_EV_WRITE,
  GPI2_EV_READ,
  GPI2_EV_WRITE_LIST,
  GPI2_EV_READ_LIST,
  GPI2_EV_NOTIFY,
  GPI2_EV_NOTIFY_WAITSOME,
  GPI2_EV_WAIT,
  /* atomic */
  GPI2_EV_FETCH_ADD,
  GPI2_EV_CAS,
  /* passive */
  GPI2_EV_PASSIVE_SEND,
  GPI2_EV_PASSIVE_RECV,
  /* queue */
  GPI2_EV_QUEUE_CREATE,
  GPI2_EV_QUEUE_DELETE,

  GPI2_EV_COUNT
};

/* Phase types */
enum gpi2_trace_phase
{
  GPI2_TRACE_PH_BEGIN = 0,
  GPI2_TRACE_PH_END = 1,
  GPI2_TRACE_PH_INSTANT = 2
};

/* Global category mask — 0 means tracing disabled */
extern unsigned gpi2_trace_cat_mask;

/* Per-event category lookup (derived from event_info in gpi2_trace_init) */
extern unsigned gpi2_trace_event_cat[];

void gpi2_trace_init (int rank);
void gpi2_trace_flush (int rank);
void gpi2_trace_record (enum gpi2_trace_event ev, enum gpi2_trace_phase ph);

/*
 * Tier 1 macros — lifecycle operations.
 * Always compiled in, runtime-gated by category mask.
 * One predicted-not-taken branch; negligible for functions
 * that already do substantial work (syscalls, RDMA registration, etc.)
 */
#define GPI2_TRACE_BEGIN(ev)                                        \
  do {                                                              \
    if (__builtin_expect(                                           \
          __atomic_load_n(&gpi2_trace_cat_mask, __ATOMIC_ACQUIRE)   \
          & gpi2_trace_event_cat[ev], 0))                           \
      gpi2_trace_record((ev), GPI2_TRACE_PH_BEGIN);                 \
  } while (0)

#define GPI2_TRACE_END(ev)                                          \
  do {                                                              \
    if (__builtin_expect(                                           \
          __atomic_load_n(&gpi2_trace_cat_mask, __ATOMIC_ACQUIRE)   \
          & gpi2_trace_event_cat[ev], 0))                           \
      gpi2_trace_record((ev), GPI2_TRACE_PH_END);                  \
  } while (0)

#define GPI2_TRACE_EVENT(ev)                                        \
  do {                                                              \
    if (__builtin_expect(                                           \
          __atomic_load_n(&gpi2_trace_cat_mask, __ATOMIC_ACQUIRE)   \
          & gpi2_trace_event_cat[ev], 0))                           \
      gpi2_trace_record((ev), GPI2_TRACE_PH_INSTANT);              \
  } while (0)

/*
 * Tier 2 macros — data-path operations.
 * Compiled out entirely unless GPI2_TRACE is defined.
 * Zero instructions in production builds.
 */
#ifdef GPI2_TRACE
#define GPI2_TRACE_BEGIN_HOT(ev)  GPI2_TRACE_BEGIN(ev)
#define GPI2_TRACE_END_HOT(ev)    GPI2_TRACE_END(ev)
#else
#define GPI2_TRACE_BEGIN_HOT(ev)  ((void)0)
#define GPI2_TRACE_END_HOT(ev)    ((void)0)
#endif

#endif /* GPI2_TRACE_H */
