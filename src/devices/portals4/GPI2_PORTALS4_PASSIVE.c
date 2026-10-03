/*
Copyright (c) Goethe University Frankfurt MSQC - Niklas Bartelheimer
<bartelheimer@em.uni-frankfurt.de>, 2023-2026

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
#include "GPI2.h"
#include "GPI2_PORTALS4.h"

int
pgaspi_portals4_post_receive(gaspi_context_t const* gctx, unsigned int slot)
{
  gaspi_portals4_ctx* dev = gctx->device->ctx;
  ptl_le_t le = {
    .start = (char*)dev->passive_comm_msg_buf +
             slot * dev->passive_comm_msg_buf_size,
    .length = dev->passive_comm_msg_buf_size,
    .uid = PTL_UID_ANY,
    .options = PTL_LE_OP_PUT | PTL_LE_USE_ONCE |
               PTL_LE_EVENT_LINK_DISABLE | PTL_LE_EVENT_UNLINK_DISABLE,
    .ct_handle = PTL_CT_NONE,
  };
  int ret = PtlLEAppend(dev->ni_h, dev->passive_comm_pt_idx, &le,
                        PTL_PRIORITY_LIST, (void*)(uintptr_t)slot,
                        &dev->passive_recv_le_h[slot]);
  if(ret != PTL_OK)
  {
    GASPI_DEBUG_PRINT_ERROR("PtlLEAppend failed with %d", ret);
    return GASPI_ERROR;
  }
  return GASPI_SUCCESS;
}

gaspi_return_t
pgaspi_dev_passive_send(gaspi_context_t* const gctx,
                        const gaspi_segment_id_t segment_id_local,
                        const gaspi_offset_t offset_local,
                        const gaspi_rank_t rank, const gaspi_size_t size,
                        const gaspi_timeout_t timeout_ms)
{
  const int byte_id = rank >> 3;
  const unsigned char bit = 1u << (rank & 7);
  gaspi_portals4_ctx* dev = gctx->device->ctx;
  const ptl_size_t local_offset =
      gctx->rrmd[segment_id_local][gctx->rank].data.addr + offset_local;
  ptl_ct_event_t ce;
  const ptl_size_t target = dev->passive_send_ct_cnt[rank] + 1;
  int ret;
  if(!(gctx->ne_count_p[byte_id] & bit))
  {
    ret = PtlPut(dev->passive_send_md_h[rank], local_offset, size,
                 PORTALS4_PASSIVE_ACK_TYPE, dev->remote_info[rank].phys_address,
                 dev->passive_comm_pt_idx, 0, 0, NULL, gctx->rank);
    if(ret != PTL_OK)
    {
      GASPI_DEBUG_PRINT_ERROR("PtlPut failed with %d", ret);
      return GASPI_ERROR;
    }
    gctx->ne_count_p[byte_id] |= bit;
  }
  unsigned int which;
  ret = PtlCTPoll(&dev->passive_send_ct_h[rank], &target, 1,
                  timeout_ms == GASPI_BLOCK ? PTL_TIME_FOREVER : timeout_ms,
                  &ce, &which);
  if(ret != PTL_OK)
    return ret == PTL_CT_NONE_REACHED ? GASPI_TIMEOUT : GASPI_ERROR;
  if(ce.failure)
  {
    gctx->state_vec[GASPI_PASSIVE_QP][rank] = GASPI_STATE_CORRUPT;
    return GASPI_ERROR;
  }
  gctx->ne_count_p[byte_id] &= ~bit;
  dev->passive_send_ct_cnt[rank] = target;
  return GASPI_SUCCESS;
}

gaspi_return_t
pgaspi_dev_passive_receive(gaspi_context_t* const gctx,
                           const gaspi_segment_id_t segment_id_local,
                           const gaspi_offset_t offset_local,
                           gaspi_rank_t* const rank, const gaspi_size_t size,
                           const gaspi_timeout_t timeout_ms)
{
  gaspi_portals4_ctx* dev = gctx->device->ctx;
  ptl_event_t event;
  unsigned int which;
  int ret = PtlEQPoll(&dev->passive_comm_eq_h, 1,
                      timeout_ms == GASPI_BLOCK ? PTL_TIME_FOREVER : timeout_ms,
                      &event, &which);
  if(ret != PTL_OK)
    return ret == PTL_EQ_EMPTY ? GASPI_TIMEOUT : GASPI_ERROR;
  uintptr_t slot = (uintptr_t)event.user_ptr;
  if(event.type != PTL_EVENT_PUT || slot >= gctx->config->passive_queue_size_max)
    return GASPI_ERROR;
  dev->passive_recv_le_h[slot] = PTL_INVALID_HANDLE;
  gaspi_return_t status = GASPI_ERROR;
  if(event.ni_fail_type == PTL_NI_OK && event.mlength == event.rlength &&
     event.mlength <= size && event.hdr_data < gctx->tnc)
  {
    void* dest = (void*)(gctx->rrmd[segment_id_local][gctx->rank].data.addr +
                         offset_local);
    memcpy(dest, event.start, event.mlength);
    *rank = event.hdr_data;
    status = GASPI_SUCCESS;
  }
  if(pgaspi_portals4_post_receive(gctx, slot) != GASPI_SUCCESS)
    return GASPI_ERROR;
  return status;
}
