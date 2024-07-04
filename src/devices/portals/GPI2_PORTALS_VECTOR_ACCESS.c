#include "GASPI.h"
#include "GPI2.h"
#include "GPI2_PORTALS.h"

gaspi_return_t pgaspi_dev_vector_write(gaspi_context_t* const gctx,
                                const gaspi_segment_id_t segment_id_local,
                                const gaspi_offset_t offset_local,
				const gaspi_offset_t block_count,
				const gaspi_offset_t block_size,
				const gaspi_offset_t stride,
                                const gaspi_rank_t rank,
                                const gaspi_segment_id_t segment_id_remote,
                                const gaspi_offset_t offset_remote,
                                const gaspi_queue_id_t queue) {
	int ret;
	gaspi_portals4_ctx* const dev = gctx->device->ctx;
	const ptl_size_t local_offset =
	    gctx->rrmd[segment_id_local][gctx->rank].data.addr + offset_local;
	const ptl_size_t remote_offset =
	    gctx->rrmd[segment_id_remote][rank].data.addr + offset_remote;

	if (gctx->ne_count_c[queue] == gctx->config->queue_size_max) {
		GASPI_DEBUG_PRINT_ERROR(
		    "pgaspi_dev_write GASPI_QUEUE_FULL is: %d max: %d",
		    gctx->ne_count_c[queue],
		    gctx->config->queue_size_max);
		return GASPI_QUEUE_FULL;
	}
	
	for(int block = 0; block < block_count; ++block){
		ptl_size_t _stride = stride * block;
		ret = PtlPut(dev->comm_notif_md_h[queue],
		             local_offset + stride,
		             block_size,
		             PORTALS4_ACK_TYPE,
		             dev->remote_info[rank].phys_address,
		             dev->data_pt_idx,
		             0,
		             remote_offset + stride,
		             NULL,
		             0);

		if (ret != PTL_OK) {
			GASPI_DEBUG_PRINT_ERROR("PtlPut failed with %d for size %d", ret, size);
			return GASPI_ERROR;
		}
	}
	gctx->ne_count_c[queue] += block_count;
	return GASPI_SUCCESS;
}

gaspi_return_t pgaspi_dev_vector_write_notify(
    gaspi_context_t* const gctx,
    const gaspi_segment_id_t segment_id_local,
    const gaspi_offset_t offset_local,
    const gaspi_offset_t block_count,
    const gaspi_offset_t block_size,
    const gaspi_offset_t stride,
    const gaspi_rank_t rank,
    const gaspi_segment_id_t segment_id_remote,
    const gaspi_offset_t offset_remote,
    const gaspi_notification_id_t notification_id,
    const gaspi_notification_t notification_value,
    const gaspi_queue_id_t queue) {
	int ret;

	if (gctx->ne_count_c[queue] + 2 > gctx->config->queue_size_max) {
		GASPI_DEBUG_PRINT_ERROR(
		    "pgaspi_dev_write_notify GASPI_QUEUE_FULL is: %d max: %d",
		    gctx->ne_count_c[queue],
		    gctx->config->queue_size_max);
		return GASPI_QUEUE_FULL;
	}

	ret = pgaspi_dev_vector_write(gctx,
	                       segment_id_local,
	                       offset_local,
			       block_count,
			       block_size,
			       stride,
	                       rank,
	                       segment_id_remote,
	                       offset_remote,
	                       queue);
	if (ret != GASPI_SUCCESS) {
		return ret;
	}

	ret = pgaspi_dev_notify(gctx,
	                        segment_id_remote,
	                        rank,
	                        notification_id,
	                        notification_value,
	                        queue);
	if (ret != GASPI_SUCCESS) {
		return ret;
	}

	return GASPI_SUCCESS;
}
