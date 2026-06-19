/*
Copyright (c) Fraunhofer ITWM - 2013-2026

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

/*
 * gpi2_trace_dump — offline converter for GPI-2 binary trace files.
 *
 * Usage: gpi2_trace_dump [-f text|csv|json] [-r] <trace_file.bin> [...]
 *
 * Multiple trace files can be provided to merge events from all ranks
 * into a single output. Events are sorted by timestamp.
 *
 * Output formats:
 *   text  - human-readable timeline with indentation and durations (default)
 *   csv   - timestamp_ns,rank,event_id,event_name,category,phase,tid
 *   json  - Chrome Trace Event Format (viewable in chrome://tracing or Perfetto UI)
 *
 * Options:
 *   -r    Relative mode: normalize timestamps so each rank starts at t=0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#define GPI2_TRACE_MAGIC   0x47504954u
#define GPI2_TRACE_VERSION 1

#define MAX_EVENTS 1024

enum output_format
{
  FMT_TEXT,
  FMT_CSV,
  FMT_JSON
};

/* On-disk record layout (matches GPI2_Trace.c) */
struct disk_record
{
  unsigned long long timestamp_ns;
  unsigned int event_id;
  unsigned short phase;
  unsigned short tid;
};

/* In-memory record with rank tag for merging */
struct trace_record
{
  unsigned long long timestamp_ns;
  unsigned int event_id;
  unsigned short phase;
  unsigned short tid;
  unsigned int rank;
};

struct event_info
{
  unsigned int id;
  char name[128];
  char category[64];
};

static struct event_info event_table[MAX_EVENTS];
static int event_table_count = 0;

static const char *
phase_str (unsigned short phase)
{
  switch (phase)
  {
  case 0:
    return "BEGIN";
  case 1:
    return "END";
  case 2:
    return "INSTANT";
  default:
    return "?";
  }
}

static const char *
phase_chr (unsigned short phase)
{
  switch (phase)
  {
  case 0:
    return "B";
  case 1:
    return "E";
  case 2:
    return "i";
  default:
    return "?";
  }
}

static const char *
event_name (unsigned int id)
{
  for (int i = 0; i < event_table_count; i++)
  {
    if (event_table[i].id == id)
    {
      return event_table[i].name;
    }
  }

  return "unknown";
}

static const char *
event_category (unsigned int id)
{
  for (int i = 0; i < event_table_count; i++)
  {
    if (event_table[i].id == id)
    {
      return event_table[i].category;
    }
  }

  return "unknown";
}

static int
read_string_table (FILE * f, unsigned int num_entries)
{
  unsigned int eid;
  unsigned short len;

  for (unsigned int e = 0; e < num_entries && event_table_count < MAX_EVENTS;
       e++)
  {
    if (fread (&eid, sizeof (unsigned int), 1, f) != 1)
    {
      return -1;
    }

    /* Check if this event ID is already in the table (from another file) */
    int exists = 0;

    for (int i = 0; i < event_table_count; i++)
    {
      if (event_table[i].id == eid)
      {
        exists = 1;
        break;
      }
    }

    int slot = exists ? -1 : event_table_count;

    /* Read name */
    if (fread (&len, sizeof (unsigned short), 1, f) != 1)
    {
      return -1;
    }

    if (len >= sizeof (event_table[0].name))
    {
      return -1;
    }

    char name_buf[128];

    if (fread (name_buf, 1, len, f) != len)
    {
      return -1;
    }

    name_buf[len] = '\0';

    /* Read category */
    if (fread (&len, sizeof (unsigned short), 1, f) != 1)
    {
      return -1;
    }

    if (len >= sizeof (event_table[0].category))
    {
      return -1;
    }

    char cat_buf[64];

    if (fread (cat_buf, 1, len, f) != len)
    {
      return -1;
    }

    cat_buf[len] = '\0';

    /* Only add if not already present */
    if (slot >= 0)
    {
      event_table[slot].id = eid;
      memcpy (event_table[slot].name, name_buf, sizeof (name_buf));
      memcpy (event_table[slot].category, cat_buf, sizeof (cat_buf));
      event_table_count++;
    }
  }

  return 0;
}

static int
cmp_records (const void *a, const void *b)
{
  const struct trace_record *ra = (const struct trace_record *) a;
  const struct trace_record *rb = (const struct trace_record *) b;

  if (ra->timestamp_ns < rb->timestamp_ns)
  {
    return -1;
  }

  if (ra->timestamp_ns > rb->timestamp_ns)
  {
    return 1;
  }

  /* Stable tie-break: lower rank first, then lower event index (preserved by
   * the fact that qsort sees them in file order within equal timestamps) */
  if (ra->rank != rb->rank)
  {
    return (ra->rank < rb->rank) ? -1 : 1;
  }

  return 0;
}

static int
load_trace_file (const char *filename, struct trace_record **out_records,
                 unsigned int *out_count, unsigned int *out_rank)
{
  FILE *f = fopen (filename, "rb");

  if (f == NULL)
  {
    fprintf (stderr, "Cannot open %s\n", filename);
    return -1;
  }

  /* Read header */
  unsigned int header[5];

  if (fread (header, sizeof (unsigned int), 5, f) != 5)
  {
    fprintf (stderr, "%s: failed to read header\n", filename);
    fclose (f);
    return -1;
  }

  if (header[0] != GPI2_TRACE_MAGIC)
  {
    fprintf (stderr, "%s: not a GPI-2 trace file (bad magic: 0x%08x)\n",
             filename, header[0]);
    fclose (f);
    return -1;
  }

  if (header[1] != GPI2_TRACE_VERSION)
  {
    fprintf (stderr, "%s: unsupported trace version: %u\n", filename,
             header[1]);
    fclose (f);
    return -1;
  }

  unsigned int rank = header[2];
  unsigned int event_count = header[3];
  unsigned int string_table_count = header[4];

  *out_rank = rank;

  /* Read string table (deduplicates across files) */
  if (read_string_table (f, string_table_count) != 0)
  {
    fprintf (stderr, "%s: failed to read string table\n", filename);
    fclose (f);
    return -1;
  }

  /* Read event records from disk */
  struct disk_record *disk =
    malloc (event_count * sizeof (struct disk_record));
  if (disk == NULL)
  {
    fprintf (stderr, "Out of memory\n");
    fclose (f);
    return -1;
  }

  size_t nread = fread (disk, sizeof (struct disk_record), event_count, f);

  fclose (f);

  if (nread != event_count)
  {
    fprintf (stderr, "%s: expected %u events, read %zu\n",
             filename, event_count, nread);
    event_count = (unsigned int) nread;
  }

  /* Convert to in-memory records with rank tag */
  struct trace_record *records =
    malloc (event_count * sizeof (struct trace_record));
  if (records == NULL)
  {
    fprintf (stderr, "Out of memory\n");
    free (disk);
    return -1;
  }

  for (unsigned int i = 0; i < event_count; i++)
  {
    records[i].timestamp_ns = disk[i].timestamp_ns;
    records[i].event_id = disk[i].event_id;
    records[i].phase = disk[i].phase;
    records[i].tid = disk[i].tid;
    records[i].rank = rank;
  }

  free (disk);

  *out_records = records;
  *out_count = event_count;
  return 0;
}

static void
dump_text (struct trace_record * records, unsigned int count, int multi_rank)
{
  if (count == 0)
  {
    return;
  }

  /* Find max rank to size per-rank state arrays */
  unsigned int max_rank = 0;

  for (unsigned int i = 0; i < count; i++)
  {
    if (records[i].rank > max_rank)
    {
      max_rank = records[i].rank;
    }
  }

  unsigned int nranks = max_rank + 1;

  int *depth = calloc (nranks, sizeof (int));
  int *stack_top = calloc (nranks, sizeof (int));
  unsigned long long (*begin_stack)[64] = calloc (nranks,
                                                  sizeof (*begin_stack));

  if (!depth || !stack_top || !begin_stack)
  {
    fprintf (stderr, "Out of memory\n");
    free (depth);
    free (stack_top);
    free (begin_stack);
    return;
  }

  unsigned long long base_ts = records[0].timestamp_ns;

  if (!multi_rank)
  {
    printf ("=== Trace for rank %u (%u events) ===\n\n", records[0].rank,
            count);
  }
  else
  {
    printf ("=== Merged trace (%u events) ===\n\n", count);
  }

  for (unsigned int i = 0; i < count; i++)
  {
    struct trace_record *r = &records[i];
    unsigned int rk = r->rank;
    double rel_ms = (double) (r->timestamp_ns - base_ts) / 1e6;

    if (r->phase == 1 && depth[rk] > 0)       /* END */
    {
      depth[rk]--;
    }

    /* Rank prefix for multi-rank mode */
    if (multi_rank)
    {
      printf ("[rank %3u] ", r->rank);
    }

    /* Indentation */
    for (int d = 0; d < depth[rk]; d++)
    {
      printf ("  ");
    }

    if (r->phase == 0)            /* BEGIN */
    {
      printf ("[%12.3f ms] >> %s", rel_ms, event_name (r->event_id));
      if (stack_top[rk] < 64)
      {
        begin_stack[rk][stack_top[rk]++] = r->timestamp_ns;
      }

      depth[rk]++;
    }
    else if (r->phase == 1)       /* END */
    {
      double dur_ms = 0.0;

      if (stack_top[rk] > 0)
      {
        dur_ms =
          (double) (r->timestamp_ns -
                    begin_stack[rk][--stack_top[rk]]) / 1e6;
      }

      printf ("[%12.3f ms] << %s  (%.3f ms)", rel_ms,
              event_name (r->event_id), dur_ms);
    }
    else                          /* INSTANT */
    {
      printf ("[%12.3f ms]  * %s", rel_ms, event_name (r->event_id));
    }

    if (r->tid > 0)
    {
      printf ("  [tid=%u]", r->tid);
    }

    printf ("\n");
  }

  free (depth);
  free (stack_top);
  free (begin_stack);
}

static void
dump_csv (struct trace_record * records, unsigned int count)
{
  printf ("timestamp_ns,rank,event_id,event_name,category,phase,tid\n");

  for (unsigned int i = 0; i < count; i++)
  {
    struct trace_record *r = &records[i];

    printf ("%" PRIu64 ",%u,%u,%s,%s,%s,%u\n",
            (uint64_t) r->timestamp_ns,
            r->rank,
            r->event_id,
            event_name (r->event_id),
            event_category (r->event_id),
            phase_str (r->phase), r->tid);
  }
}

static void
dump_json (struct trace_record * records, unsigned int count)
{
  unsigned long long base_ts = count > 0 ? records[0].timestamp_ns : 0;

  printf ("{\"traceEvents\":[\n");

  for (unsigned int i = 0; i < count; i++)
  {
    struct trace_record *r = &records[i];

    /* Chrome Trace Format uses microseconds */
    double ts_us = (double) (r->timestamp_ns - base_ts) / 1000.0;

    printf ("  {\"ph\":\"%s\",\"name\":\"%s\",\"cat\":\"%s\","
            "\"ts\":%.3f,\"pid\":%u,\"tid\":%u}",
            phase_chr (r->phase),
            event_name (r->event_id),
            event_category (r->event_id), ts_us, r->rank, r->tid);

    if (i < count - 1)
    {
      printf (",");
    }

    printf ("\n");
  }

  printf ("]}\n");
}

int
main (int argc, char *argv[])
{
  enum output_format fmt = FMT_TEXT;
  int relative_mode = 0;

  /* Collect filenames */
  const char *filenames[1024];
  int nfiles = 0;

  for (int i = 1; i < argc; i++)
  {
    if (strcmp (argv[i], "-f") == 0 && i + 1 < argc)
    {
      i++;
      if (strcmp (argv[i], "text") == 0)
      {
        fmt = FMT_TEXT;
      }
      else if (strcmp (argv[i], "csv") == 0)
      {
        fmt = FMT_CSV;
      }
      else if (strcmp (argv[i], "json") == 0)
      {
        fmt = FMT_JSON;
      }
      else
      {
        fprintf (stderr, "Unknown format: %s (use text, csv, or json)\n",
                 argv[i]);
        return 1;
      }
    }
    else if (strcmp (argv[i], "-r") == 0)
    {
      relative_mode = 1;
    }
    else if (strcmp (argv[i], "-h") == 0 || strcmp (argv[i], "--help") == 0)
    {
      printf
        ("Usage: %s [-f text|csv|json] [-r] <trace_file.bin> [...]\n\n"
         "Options:\n"
         "  -f FORMAT  Output format: text (default), csv, or json\n"
         "  -r         Relative mode: normalize so each rank starts at t=0\n"
         "  -h         Show this help\n\n"
         "Multiple trace files can be merged into a single output.\n"
         "JSON output uses Chrome Trace Format (viewable in Perfetto UI).\n",
         argv[0]);
      return 0;
    }
    else
    {
      if (nfiles < 1024)
      {
        filenames[nfiles++] = argv[i];
      }
    }
  }

  if (nfiles == 0)
  {
    fprintf (stderr,
             "Usage: %s [-f text|csv|json] [-r] <trace_file.bin> [...]\n",
             argv[0]);
    return 1;
  }

  /* Load all trace files */
  struct trace_record *all_records = NULL;
  unsigned int total_count = 0;

  for (int i = 0; i < nfiles; i++)
  {
    struct trace_record *file_records = NULL;
    unsigned int file_count = 0;
    unsigned int file_rank = 0;

    if (load_trace_file (filenames[i], &file_records, &file_count,
                         &file_rank) != 0)
    {
      free (all_records);
      return 1;
    }

    if (file_count == 0)
    {
      free (file_records);
      continue;
    }

    /* Apply relative normalization: subtract first timestamp of this rank */
    if (relative_mode && file_count > 0)
    {
      unsigned long long base = file_records[0].timestamp_ns;

      for (unsigned int j = 0; j < file_count; j++)
      {
        file_records[j].timestamp_ns -= base;
      }
    }

    /* Append to merged array */
    struct trace_record *merged =
      realloc (all_records,
               (total_count + file_count) * sizeof (struct trace_record));
    if (merged == NULL)
    {
      fprintf (stderr, "Out of memory\n");
      free (file_records);
      free (all_records);
      return 1;
    }

    all_records = merged;
    memcpy (all_records + total_count, file_records,
            file_count * sizeof (struct trace_record));
    total_count += file_count;
    free (file_records);
  }

  /* Sort by timestamp when merging multiple files */
  if (nfiles > 1 && total_count > 1)
  {
    qsort (all_records, total_count, sizeof (struct trace_record),
           cmp_records);
  }

  int multi_rank = nfiles > 1;

  switch (fmt)
  {
  case FMT_TEXT:
    dump_text (all_records, total_count, multi_rank);
    break;
  case FMT_CSV:
    dump_csv (all_records, total_count);
    break;
  case FMT_JSON:
    dump_json (all_records, total_count);
    break;
  }

  free (all_records);
  return 0;
}
