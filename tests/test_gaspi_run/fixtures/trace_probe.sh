#!/bin/bash
# Dummy GASPI application that reports the tracing environment it was
# launched with. Used by the -t/--trace tests to verify GASPI_TRACE
# propagation down to the (master/rank-0) process.
echo "TRACE_PROBE GASPI_TRACE=${GASPI_TRACE:-<unset>}"
echo "argc=$(($# + 1))"
exit 0
