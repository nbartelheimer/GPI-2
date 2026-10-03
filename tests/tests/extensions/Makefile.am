include ../Make.inc

## EXTENSIONS
extensions_tests = ../../bin/ping_procs.bin \
		   ../../bin/q_create_use_queue.bin \
		   ../../bin/q_create_delete_wait.bin \
		   ../../bin/seg_avail_local.bin \
		   ../../bin/seg_avail_local_usermsegs.bin

______bin_ping_procs_bin_SOURCES = ping_procs.c $(auxSRCS)
______bin_q_create_use_queue_bin_SOURCES = q_create_use_queue.c $(auxSRCS)
______bin_q_create_delete_wait_bin_SOURCES = q_create_delete_wait.c $(auxSRCS)
______bin_seg_avail_local_bin_SOURCES = seg_avail_local.c $(auxSRCS)
______bin_seg_avail_local_usermsegs_bin_SOURCES = seg_avail_local_usermsegs.c $(auxSRCS)

test_pkglibexec_PROGRAMS = $(extensions_tests)

if WITH_PORTALS4
test_pkglibexec_PROGRAMS += ../../bin/q_recreate_used.bin
______bin_q_recreate_used_bin_SOURCES = q_recreate_used.c $(auxSRCS)
endif

if WITH_PORTALS4
test_pkglibexec_PROGRAMS += ../../bin/portals4_errors.bin
______bin_portals4_errors_bin_SOURCES = portals4_errors.c $(auxSRCS)
______bin_portals4_errors_bin_CPPFLAGS = $(AM_CPPFLAGS) @PORTALS4_CFLAGS@ -I$(abs_top_srcdir)/src -I$(abs_top_srcdir)/src/devices/include -I$(abs_top_srcdir)/src/devices/portals4
______bin_portals4_errors_bin_LDFLAGS = $(LDFLAGS) -Wl,--export-dynamic
______bin_portals4_errors_bin_LDADD = $(LDADD) @PORTALS4_LIBS@ -ldl
endif
