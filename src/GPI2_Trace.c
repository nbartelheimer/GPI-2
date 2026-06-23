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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "GPI2_Trace.h"

#define GPI2_TRACE_MAX_EVENTS 8192
#define GPI2_TRACE_MAGIC      0x47504954u     /* "GPIT" */
#define GPI2_TRACE_VERSION    1

/* Thread-local GASPI thread ID (defined in GPI2_Threads.c as gaspi_int) */
extern __thread int __gaspi_thread_tid;

/* Event info table */

struct gpi2_trace_event_info
{
  const char *name;
  const char *category;
  unsigned cat_bit;
};

/* Single source of truth for event metadata.
 * gpi2_trace_event_cat[] is derived from this at init time. */
static const struct gpi2_trace_event_info
  event_info[GPI2_EV_COUNT] = {
  /* init */
  [GPI2_EV_PROC_INIT] = {"proc_init", "init", GPI2_TRACE_CAT_INIT},
  [GPI2_EV_SN_SETUP] = {"sn_setup", "init", GPI2_TRACE_CAT_INIT},
  [GPI2_EV_PARSE_MFILE] = {"parse_mfile", "init", GPI2_TRACE_CAT_INIT},
  [GPI2_EV_BROADCAST_TOPO] = {"broadcast_topo", "init", GPI2_TRACE_CAT_INIT},
  [GPI2_EV_INIT_CORE] = {"init_core", "init", GPI2_TRACE_CAT_INIT},
  [GPI2_EV_SN_WAIT] = {"sn_wait", "init", GPI2_TRACE_CAT_INIT},
  [GPI2_EV_BUILD_INFRA] = {"build_infra", "init", GPI2_TRACE_CAT_INIT},
  [GPI2_EV_PROC_TERM] = {"proc_term", "init", GPI2_TRACE_CAT_INIT},
  /* segments */
  [GPI2_EV_SEG_CREATE] = {"seg_create", "seg", GPI2_TRACE_CAT_SEG},
  [GPI2_EV_SEG_ALLOC] = {"seg_alloc", "seg", GPI2_TRACE_CAT_SEG},
  [GPI2_EV_SEG_REGISTER] = {"seg_register", "seg", GPI2_TRACE_CAT_SEG},
  [GPI2_EV_SEG_DELETE] = {"seg_delete", "seg", GPI2_TRACE_CAT_SEG},
  /* groups */
  [GPI2_EV_GRP_CREATE] = {"grp_create", "grp", GPI2_TRACE_CAT_GRP},
  [GPI2_EV_GRP_DELETE] = {"grp_delete", "grp", GPI2_TRACE_CAT_GRP},
  [GPI2_EV_GRP_COMMIT] = {"grp_commit", "grp", GPI2_TRACE_CAT_GRP},
  /* collectives */
  [GPI2_EV_BARRIER] = {"barrier", "coll", GPI2_TRACE_CAT_COLL},
  [GPI2_EV_ALLREDUCE] = {"allreduce", "coll", GPI2_TRACE_CAT_COLL},
  /* io */
  [GPI2_EV_WRITE] = {"write", "io", GPI2_TRACE_CAT_IO},
  [GPI2_EV_READ] = {"read", "io", GPI2_TRACE_CAT_IO},
  [GPI2_EV_WRITE_LIST] = {"write_list", "io", GPI2_TRACE_CAT_IO},
  [GPI2_EV_READ_LIST] = {"read_list", "io", GPI2_TRACE_CAT_IO},
  [GPI2_EV_NOTIFY] = {"notify", "io", GPI2_TRACE_CAT_IO},
  [GPI2_EV_NOTIFY_WAITSOME] = {"notify_waitsome", "io", GPI2_TRACE_CAT_IO},
  [GPI2_EV_WAIT] = {"wait", "io", GPI2_TRACE_CAT_IO},
  /* atomic */
  [GPI2_EV_FETCH_ADD] = {"fetch_add", "atomic", GPI2_TRACE_CAT_ATOMIC},
  [GPI2_EV_CAS] = {"cas", "atomic", GPI2_TRACE_CAT_ATOMIC},
  /* passive */
  [GPI2_EV_PASSIVE_SEND] = {"passive_send", "passive", GPI2_TRACE_CAT_PASSIVE},
  [GPI2_EV_PASSIVE_RECV] = {"passive_recv", "passive", GPI2_TRACE_CAT_PASSIVE},
  /* queue */
  [GPI2_EV_QUEUE_CREATE] = {"queue_create", "io", GPI2_TRACE_CAT_IO},
  [GPI2_EV_QUEUE_DELETE] = {"queue_delete", "io", GPI2_TRACE_CAT_IO},
};

/* ── Category name → bit mapping for env var parsing ──────────── */

struct gpi2_trace_cat_entry
{
  const char *name;
  unsigned bit;
};

static const struct gpi2_trace_cat_entry cat_table[] = {
  {"init", GPI2_TRACE_CAT_INIT},
  {"seg", GPI2_TRACE_CAT_SEG},
  {"io", GPI2_TRACE_CAT_IO},
  {"coll", GPI2_TRACE_CAT_COLL},
  {"grp", GPI2_TRACE_CAT_GRP},
  {"atomic", GPI2_TRACE_CAT_ATOMIC},
  {"passive", GPI2_TRACE_CAT_PASSIVE},
  {"sn", GPI2_TRACE_CAT_SN},
  {NULL, 0}
};

/* Trace buffer */

struct gpi2_trace_record
{
  unsigned long long timestamp_ns;
  unsigned int event_id;
  unsigned short phase;
  unsigned short tid;
};

unsigned gpi2_trace_cat_mask = 0;

/* Derived from event_info[] in gpi2_trace_init */
unsigned gpi2_trace_event_cat[GPI2_EV_COUNT];

static struct gpi2_trace_record trace_buf[GPI2_TRACE_MAX_EVENTS];
static unsigned trace_idx = 0;
static unsigned trace_complete = 0;
static int trace_rank = 0;

/* Category parsing */

static unsigned
parse_cat_mask (const char *spec)
{
  if (strcmp (spec, "1") == 0 || strcmp (spec, "all") == 0)
  {
    return GPI2_TRACE_CAT_ALL;
  }

  if (strcmp (spec, "0") == 0)
  {
    return 0;
  }

  unsigned mask = 0;

  /* Work on a copy since we tokenize with strtok */
  char buf[256];
  strncpy (buf, spec, sizeof (buf) - 1);
  buf[sizeof (buf) - 1] = '\0';

  char *saveptr = NULL;
  char *tok = strtok_r (buf, ",", &saveptr);

  while (tok != NULL)
  {
    /* Strip leading whitespace */
    while (*tok == ' ')
    {
      tok++;
    }

    /* Strip trailing whitespace */
    char *end = tok + strlen (tok) - 1;

    while (end > tok && *end == ' ')
    {
      *end-- = '\0';
    }

    int found = 0;

    for (const struct gpi2_trace_cat_entry * e = cat_table; e->name; e++)
    {
      if (strcmp (tok, e->name) == 0)
      {
        mask |= e->bit;
        found = 1;
        break;
      }
    }

    if (!found)
    {
      fprintf (stderr,
               "GPI2 trace: unknown category '%s' (available: "
               "init,seg,io,coll,grp,atomic,passive,sn)\n", tok);
    }

    tok = strtok_r (NULL, ",", &saveptr);
  }

  return mask;
}

/* Checked write helper */

static int
checked_fwrite (const void *buf, size_t size, size_t n, FILE * f)
{
  return fwrite (buf, size, n, f) == n ? 0 : -1;
}

/* Public API */

void
gpi2_trace_init (int rank)
{
  trace_rank = rank;

  /* Derive fast category lookup from the single event_info table */
  for (unsigned i = 0; i < GPI2_EV_COUNT; i++)
  {
    gpi2_trace_event_cat[i] = event_info[i].cat_bit;
  }

  const char *env = getenv ("GASPI_TRACE");

  if (env == NULL || *env == '\0')
  {
    return;
  }

  unsigned mask = parse_cat_mask (env);

  if (mask == 0)
  {
    return;
  }

  __atomic_store_n (&trace_idx, 0, __ATOMIC_RELAXED);
  __atomic_store_n (&trace_complete, 0, __ATOMIC_RELAXED);
  __atomic_store_n (&gpi2_trace_cat_mask, mask, __ATOMIC_RELEASE);
}

void
gpi2_trace_record (enum gpi2_trace_event ev, enum gpi2_trace_phase ph)
{
  /* Saturating claim: stop accepting once buffer is full.
   * CAS loop prevents unsigned wrap from re-enabling writes. */
  unsigned idx;
  unsigned cur;

  do
  {
    cur = __atomic_load_n (&trace_idx, __ATOMIC_RELAXED);
    if (cur >= GPI2_TRACE_MAX_EVENTS)
    {
      return;
    }
  }
  while (!__atomic_compare_exchange_n (&trace_idx, &cur, cur + 1,
                                       /*weak=*/ 1,
                                       __ATOMIC_RELAXED, __ATOMIC_RELAXED));
  idx = cur;

  struct timespec ts;

  clock_gettime (CLOCK_MONOTONIC, &ts);

  int tid = __gaspi_thread_tid;

  if (tid < 0)
  {
    tid = 0;
  }

  trace_buf[idx].timestamp_ns =
    (unsigned long long) ts.tv_sec * 1000000000ULL +
    (unsigned long long) ts.tv_nsec;
  trace_buf[idx].event_id = (unsigned int) ev;
  trace_buf[idx].phase = (unsigned short) ph;
  trace_buf[idx].tid = (unsigned short) tid;

  /* Signal that this slot is fully written */
  __atomic_fetch_add (&trace_complete, 1, __ATOMIC_RELEASE);
}

void
gpi2_trace_flush (int rank)
{
  unsigned mask =
    __atomic_load_n (&gpi2_trace_cat_mask, __ATOMIC_ACQUIRE);

  if (mask == 0)
  {
    return;
  }

  unsigned count = __atomic_load_n (&trace_idx, __ATOMIC_RELAXED);

  if (count > GPI2_TRACE_MAX_EVENTS)
  {
    count = GPI2_TRACE_MAX_EVENTS;
  }

  if (count == 0)
  {
    return;
  }

  /* Wait for all in-flight writers to finish their field stores */
  while (__atomic_load_n (&trace_complete, __ATOMIC_ACQUIRE) < count)
    ;

  char fname[64];

  snprintf (fname, sizeof (fname), "gaspi_trace_rank%u.bin", (unsigned) rank);

  FILE *f = fopen (fname, "wb");

  if (f == NULL)
  {
    fprintf (stderr, "GPI2 trace: failed to open %s: %s\n", fname,
             strerror (errno));
    return;
  }

  /* Write header */
  unsigned int header[5];

  header[0] = GPI2_TRACE_MAGIC;
  header[1] = GPI2_TRACE_VERSION;
  header[2] = (unsigned int) rank;
  header[3] = count;
  header[4] = GPI2_EV_COUNT;

  if (checked_fwrite (header, sizeof (unsigned int), 5, f) != 0)
  {
    goto write_err;
  }

  /* Write string table: event_count entries of {id, name, category} */
  for (unsigned i = 0; i < GPI2_EV_COUNT; i++)
  {
    unsigned int eid = i;

    if (checked_fwrite (&eid, sizeof (unsigned int), 1, f) != 0)
    {
      goto write_err;
    }

    /* Write name as length-prefixed string */
    const char *name = event_info[i].name;
    unsigned short len = (unsigned short) strlen (name);

    if (checked_fwrite (&len, sizeof (unsigned short), 1, f) != 0)
    {
      goto write_err;
    }

    if (checked_fwrite (name, 1, len, f) != 0)
    {
      goto write_err;
    }

    /* Write category as length-prefixed string */
    const char *cat = event_info[i].category;

    len = (unsigned short) strlen (cat);

    if (checked_fwrite (&len, sizeof (unsigned short), 1, f) != 0)
    {
      goto write_err;
    }

    if (checked_fwrite (cat, 1, len, f) != 0)
    {
      goto write_err;
    }
  }

  /* Write event records */
  if (checked_fwrite (trace_buf, sizeof (struct gpi2_trace_record), count,
                      f) != 0)
  {
    goto write_err;
  }

  fclose (f);

  /* Disable tracing after flush */
  __atomic_store_n (&gpi2_trace_cat_mask, 0, __ATOMIC_RELEASE);
  return;

write_err:
  fprintf (stderr, "GPI2 trace: write error on %s: %s\n", fname,
           strerror (errno));
  fclose (f);
}
