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
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "GPI2_Topology.h"

#define HASH_LOAD_FACTOR 1.33

typedef struct
{
  char *key;
  uint8_t local_id;
  uint8_t host_count;
  uint8_t next_local;
  int used;
} ht_entry;

typedef struct
{
  size_t n;

  /* mmap'd file raw data */
  char*  file_map;
  size_t file_size;

  /* arrays */
  char**   hosts;
  uint8_t* local_ids;
  uint32_t* hosts_ids;

  /* hash table */
  ht_entry *hash;
  size_t hash_size;
} hosttable_t;

/* hash function */
static uint64_t
hfast (const char *s)
{
  uint64_t h = 146527;
  while (*s) h = (h * 131) ^ (unsigned char)*s++;

  return h;
}

static void
ht_init (hosttable_t *t, size_t capacity)
{
  t->hash_size = (size_t) (capacity * HASH_LOAD_FACTOR) + 3;
  t->hash = calloc (t->hash_size, sizeof (ht_entry));
}

/* lookup or insertion slot */
static ht_entry*
ht_get_entry (hosttable_t *t, const char *key)
{
  uint64_t hv = hfast (key);
  size_t i = hv % t->hash_size;

  while (1)
  {
    if (!t->hash[i].used)
    {
      return &t->hash[i];
    }
    if (strcmp(t->hash[i].key, key) == 0)
    {
      return &t->hash[i];
    }

    i = (i + 1) % t->hash_size;
  }
}

static void
hosttable_destroy (hosttable_t *t)
{
  if (!t)
  {
    return;
  }

  if (t->file_map != NULL && t->file_map != MAP_FAILED)
  {
    munmap (t->file_map, t->file_size);
  }
  free (t->hosts);
  free (t->local_ids);
  free (t->hosts_ids);
  free (t->hash);
  free (t);
}


/* mmap + parse */
static hosttable_t*
hosttable_create_mmap (const char *filepath, size_t n)
{
  int fd = open (filepath, O_RDONLY);
  if (fd < 0)
  {
    return NULL;
  }

  struct stat st;
  if (fstat (fd, &st) != 0)
  {
    close (fd);
    return NULL;
  }

  hosttable_t *t = calloc (1, sizeof (*t));
  if (NULL == t)
  {
    close (fd);
    return NULL;
  }

  t->file_size = st.st_size;

  t->file_map = mmap (NULL, st.st_size,
                      PROT_READ | PROT_WRITE,
                      MAP_PRIVATE, fd, 0);
  close (fd);

  if (t->file_map == MAP_FAILED)
  {
    free (t);
    return NULL;
  }

  t->n = n;
  t->hosts = malloc (n * sizeof(char*));
  t->local_ids = malloc (n * sizeof(uint8_t));
  t->hosts_ids = malloc (n * sizeof (uint32_t));

  if (t->hosts == NULL || t->local_ids == NULL || t->hosts_ids == NULL)
  {
    hosttable_destroy (t);
    return NULL;
  }

  ht_init (t, n);
  if (!t->hash)
  {
    hosttable_destroy (t);
    return NULL;
  }

  /* parse lines inside mmap block */
  size_t idx = 0;
  char *p = t->file_map;
  char *end = t->file_map + st.st_size;

  size_t hid = 0;
  while (p < end && idx < n)
  {
    char *start = p;
    char *nl = memchr (p, '\n', end - p);
    if (!nl)
    {
      nl = end;
    }

    *nl = '\0'; /* convert newline to terminator */
    t->hosts[idx] = start;

    ht_entry *e = ht_get_entry (t, start);

    if (!e->used)
    {
      e->used = 1;
      e->key = start;
      e->local_id = 0;
      hid++;
      e->host_count++;
      e->next_local = 1;
      t->local_ids[idx] = 0;
    }
    else
    {
      t->local_ids[idx] = e->next_local;
      e->next_local++;
    }

    t->hosts_ids[idx] = hid-1;

    idx++;
    p = nl + 1;
  }

  if (idx != n)
  {
    hosttable_destroy (t);

    return NULL;
  }

  return t;
}

static int
gpi2_topology_set_hosts_info (gpi2_topology_t* topo)
{
  if (!topo->buffer)
  {
    return 1;
  }

  /* build hosts pointers */
  size_t n = topo->count;

  topo->hosts = malloc (n * sizeof (char*));
  if (!topo->hosts)
  {
    return 1;
  }

  char* p = topo->buffer;

  size_t hosts_ids_off  = topo->buffer_size - n * sizeof (uint32_t);
  size_t local_ids_off = hosts_ids_off - n * sizeof (uint8_t);

  /* set ids locations, local and hosts */
  topo->local_ids = (uint8_t*) (p + local_ids_off);
  topo->hosts_ids = (uint32_t*) (p + hosts_ids_off);

  /* allocate for number of entries per host */
  uint32_t max_host_id = 0;
  for (size_t i = 0; i < n; i++)
  {
    if (topo->hosts_ids[i] > max_host_id)
    {
      max_host_id = topo->hosts_ids[i];
    }
  }
  const size_t num_hosts = (size_t) max_host_id + 1;

  topo->count_per_host = (size_t*) calloc (num_hosts, sizeof (size_t));
  if (!topo->count_per_host)
  {
    free (topo->hosts);
    return 1;
  }

  /* set hosts pointers */
  for (size_t i = 0; i < topo->count; i++)
  {
    topo->hosts[i] = p;
    p += strlen (p) + 1;

    /* set entries per host.  NOTE: hosts_ids is required (we set ptr
       before). We mix the steps a bit to avoid traversing the array *
       again */
    topo->count_per_host[topo->hosts_ids[i]]++;
  }

  return 0;
}

void
gpi2_topology_free (gpi2_topology_t* topo)
{
  free (topo->count_per_host);
  free (topo->hosts);
  free (topo->buffer);
  free (topo);
}

gpi2_topology_t*
gpi2_topology_from_file (const char *filepath, size_t n)
{
  hosttable_t *t = hosttable_create_mmap (filepath, n);
  if (t == NULL)
  {
    return NULL;
  }

  gpi2_topology_t* topo = calloc (1, sizeof (gpi2_topology_t));
  if (!topo)
  {
    hosttable_destroy (t);
    return NULL;
  }

  /* Allocate buffer containing all info */
  size_t local_ids_size = t->n * sizeof (uint8_t);
  size_t host_ids_size  = t->n * sizeof (uint32_t);

  topo->buffer_size = t->file_size + local_ids_size + host_ids_size;

  topo->buffer = malloc (topo->buffer_size);
  if (!(topo->buffer))
  {
    free (topo);
    hosttable_destroy (t);
    return NULL;
  }

  topo->count = t->n;

  /* copy hostnames */
  memcpy (topo->buffer, t->file_map, t->file_size);

  /* copy local IDs */
  memcpy (topo->buffer + t->file_size, t->local_ids, local_ids_size);

  /* copy hosts IDs */
  memcpy (topo->buffer + t->file_size + local_ids_size,
          t->hosts_ids, host_ids_size);

  /* set information about hosts */
  if (gpi2_topology_set_hosts_info (topo))
  {
    free (topo->buffer);
    free (topo);
    hosttable_destroy (t);
    return NULL;
  }

  /* We destroy the table, at least for now, as we have what we need */
  hosttable_destroy (t);

  return topo;
}

gpi2_topology_t*
gpi2_topology_from_buffer (char* buffer, size_t buf_size, size_t n)
{
  gpi2_topology_t* topo = calloc (1, sizeof (gpi2_topology_t));
  if (!topo)
  {
    return NULL;
  }

  /* Note: we're assuming buffer is in the right format */
  topo->buffer = buffer;
  topo->buffer_size = buf_size;
  topo->count = n;

  if (gpi2_topology_set_hosts_info (topo))
  {
    free (topo);
    return NULL;
  }

  return topo;
}
