#ifndef GPI2_VECTOR_ACCESS
#define GPI2_VECTOR_ACCESS

#include "GASPI.h"

  gaspi_return_t gaspi_vector_write (const gaspi_segment_id_t segment_id_local,
                                     const gaspi_offset_t offset_local,
				                             const gaspi_offset_t block_count,
				                             const gaspi_offset_t block_size,
				                             const gaspi_offset_t stride,
                                     const gaspi_rank_t rank,
                                     const gaspi_segment_id_t segment_id_remote,
                                     const gaspi_offset_t offset_remote,
                                     const gaspi_queue_id_t queue,
                                     const gaspi_timeout_t timeout_ms);


  gaspi_return_t gaspi_vector_write_notify (const gaspi_segment_id_t segment_id_local,
                                     const gaspi_offset_t offset_local,
				                             const gaspi_offset_t block_count,
				                             const gaspi_offset_t block_size,
				                             const gaspi_offset_t stride,
                                     const gaspi_rank_t rank,
                                     const gaspi_segment_id_t segment_id_remote,
                                     const gaspi_offset_t offset_remote,
                                     const gaspi_notification_id_t notification_id,
                                     const gaspi_notification_t notification_value,
                                     const gaspi_queue_id_t queue,
                                     const gaspi_timeout_t timeout_ms);
#endif
