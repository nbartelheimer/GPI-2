/*
Copyright (c) Fraunhofer ITWM, 2013-2025

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

#ifndef GPI2_TOPOLOGY_H
#define GPI2_TOPOLOGY_H

#include <stdint.h>
#include <stddef.h>

typedef struct
{
  char** hosts;
  uint8_t* local_ids;     /* id within node */
  uint32_t* hosts_ids;    /* id of host */
  size_t count;           /* total num of hosts (incl. repetitions) */
  size_t* count_per_host; /* entries for each host (size = unique
                           * hosts) */

  char *buffer;       /* whole buffer to transmit: hostnames + ids */
  size_t buffer_size; /* bytes */
} gpi2_topology_t;

gpi2_topology_t*
gpi2_topology_from_file (const char *filepath, size_t n);

/* extract topology from a buffer */
gpi2_topology_t*
gpi2_topology_from_buffer (char *buffer, size_t buf_size, size_t n);

void
gpi2_topology_free (gpi2_topology_t *t);

#endif //GPI2_TOPOLOGY_H
