#!/bin/bash
#
# Test harness for gaspi_run.slurm (the Slurm launcher variant).
#
# Mocks srun and scontrol so tests run locally without a Slurm allocation. The
# real bin/gaspi_run.slurm is invoked directly (not copied) so it resolves the
# real slurm.env wrapper sitting next to it; the mock srun strips the "-n/-N"
# flags and runs that wrapper + binary locally, which lets us observe how
# arguments and GASPI_TRACE propagate from the launcher down to the process.
#
# This mirrors the coverage of run_tests.sh (the SSH launcher harness), adapted
# to the Slurm launcher's actual feature set:
#   - no -b/-d/-p options and no local/remote split (everything goes via srun);
#   - no hostname/newline validation (srun/scontrol own host resolution);
#   - program-argument boundaries are NOT preserved -- the launcher forwards
#     "$*" as a single string -- so argument tests assert that the args reach
#     srun rather than reconstructing argv on the receiving side;
#   - extra Slurm-specific units: scontrol-based auto-machinefile generation
#     and the "exceeds the Slurm job's allocation" guard.
#

set -u

TEST_DIR="$(cd "$(dirname "$0")" && pwd)"
GASPI_RUN_SLURM="$(cd "$TEST_DIR/../../bin" && pwd)/gaspi_run.slurm"
MOCK_DIR="$TEST_DIR/mocks"
FIXTURE_DIR="$TEST_DIR/fixtures"

# Test counters
TESTS_RUN=0
TESTS_PASS=0
TESTS_FAIL=0
FAILURES=""

######################################################################
# Setup / teardown
######################################################################

setup() {
    export MOCK_LOG=$(mktemp /tmp/gaspi_slurm_mock.XXXXXX)
    TEST_STDOUT=$(mktemp /tmp/gaspi_slurm_out.XXXXXX)
    TEST_STDERR=$(mktemp /tmp/gaspi_slurm_err.XXXXXX)
    TEST_EXIT=0

    # Prepend mocks to PATH so srun/scontrol resolve to ours.
    export PATH="$MOCK_DIR:$PATH"

    # Minimal single-task Slurm allocation for the -m (user machinefile) path.
    # Individual tests override SLURM_NTASKS / SLURM_JOB_ID as needed.
    export SLURM_NTASKS=1
    export SLURM_NPROCS=1
    export SLURM_PROCID=0
    export SLURM_LOCALID=0
    export SLURM_NNODES=1
    unset SLURM_JOB_ID

    # GASPI_TRACE must not leak in from the caller's environment.
    unset GASPI_TRACE

    # SLURM_STEP_ID must not leak in: it would trip the nested-step handling. It
    # is set explicitly only by the nested-step tests.
    unset SLURM_STEP_ID SLURM_STEPID
    # The srun --overlap capability toggle is opt-in per test.
    unset MOCK_SRUN_NO_OVERLAP
    # SLURM_TASKS_PER_NODE is derived per-test; default to unset.
    unset SLURM_TASKS_PER_NODE

    # Single-host machine file (1 task, 1 node).
    MF=$(mktemp /tmp/gaspi_slurm_mf.XXXXXX)
    printf '%s\n' "localhost" > "$MF"

    # Three-host machine file (for -n truncation / allocation tests).
    MF3=$(mktemp /tmp/gaspi_slurm_mf.XXXXXX)
    printf '%s\n%s\n%s\n' "hosta" "hostb" "hostc" > "$MF3"
}

teardown() {
    rm -f "$MOCK_LOG" "$TEST_STDOUT" "$TEST_STDERR" "$MF" "$MF3"
    # The launcher's own temp files live under /tmp/.gpi2.*; the (mock) sbcast
    # broadcast copy is /tmp/gaspi.mfile.*.
    rm -f machines_* /tmp/.gpi2.* /tmp/gaspi.mfile.*
}

######################################################################
# Test helpers
######################################################################

run_slurm() {
    TEST_EXIT=0
    "$GASPI_RUN_SLURM" "$@" > "$TEST_STDOUT" 2> "$TEST_STDERR" || TEST_EXIT=$?
}

# Like run_slurm, but launches from <dir> so relative program paths
# (e.g. ./test_app.sh) resolve against a known working directory.
run_slurm_in_dir() {
    local dir="$1"; shift
    TEST_EXIT=0
    ( cd "$dir" && "$GASPI_RUN_SLURM" "$@" ) > "$TEST_STDOUT" 2> "$TEST_STDERR" || TEST_EXIT=$?
}

assert_exit_code() {
    local expected=$1
    if [ "$TEST_EXIT" -ne "$expected" ]; then
        fail "expected exit code $expected, got $TEST_EXIT"
        return 1
    fi
    return 0
}

assert_output_contains() {
    local pattern="$1"
    if ! grep -qF -- "$pattern" "$TEST_STDOUT"; then
        fail "stdout missing: '$pattern'"
        echo "  stdout was: $(cat "$TEST_STDOUT")"
        return 1
    fi
    return 0
}

assert_stderr_contains() {
    local pattern="$1"
    if ! grep -qF -- "$pattern" "$TEST_STDERR"; then
        fail "stderr missing: '$pattern'"
        echo "  stderr was: $(cat "$TEST_STDERR")"
        return 1
    fi
    return 0
}

assert_mock_log_contains() {
    local pattern="$1"
    if ! grep -qF -- "$pattern" "$MOCK_LOG" 2>/dev/null; then
        fail "mock log missing: '$pattern'"
        echo "  mock log was: $(cat "$MOCK_LOG" 2>/dev/null)"
        return 1
    fi
    return 0
}

assert_mock_log_not_contains() {
    local pattern="$1"
    if grep -qF -- "$pattern" "$MOCK_LOG" 2>/dev/null; then
        fail "mock log unexpectedly contains: '$pattern'"
        echo "  mock log was: $(cat "$MOCK_LOG" 2>/dev/null)"
        return 1
    fi
    return 0
}

assert_no_temp_files() {
    local leftover
    leftover=$(ls /tmp/.gpi2.* 2>/dev/null)
    if [ -n "$leftover" ]; then
        fail "temp files left behind: $leftover"
        return 1
    fi
    return 0
}

assert_machinefile_unchanged() {
    local file="$1" expected="$2"
    if [ "$(cat "$file")" != "$expected" ]; then
        fail "machine file was modified: $file"
        return 1
    fi
    return 0
}

current_test=""

run_test() {
    local name="$1"
    current_test="$name"
    TESTS_RUN=$((TESTS_RUN + 1))

    setup

    local test_failed=0
    "$name" || test_failed=1

    if [ "$test_failed" -eq 0 ]; then
        TESTS_PASS=$((TESTS_PASS + 1))
        printf '\033[32m  PASS\033[0m %s\n' "$name"
    fi

    teardown
}

fail() {
    if [ -n "$current_test" ]; then
        TESTS_FAIL=$((TESTS_FAIL + 1))
        FAILURES="${FAILURES}\n  ${current_test}: $1"
        printf '\033[31m  FAIL\033[0m %s: %s\n' "$current_test" "$1"
    fi
    return 0
}

######################################################################
# Category 1: Argument forwarding
#
# The Slurm launcher collapses program arguments into a single "$*" string
# before handing them to srun, so we assert that the launcher forwarded the
# arguments to srun (visible in the mock log) rather than reconstructing argv.
######################################################################

test_no_args() {
    run_slurm -m "$MF" "$FIXTURE_DIR/test_app.sh"
    assert_exit_code 0 &&
    assert_output_contains "argc="
}

test_single_arg() {
    run_slurm -m "$MF" "$FIXTURE_DIR/test_app.sh" hello
    assert_exit_code 0 &&
    assert_mock_log_contains "test_app.sh hello"
}

test_multi_args() {
    run_slurm -m "$MF" "$FIXTURE_DIR/test_app.sh" arg1 arg2 arg3
    assert_exit_code 0 &&
    assert_mock_log_contains "test_app.sh arg1 arg2 arg3"
}

test_args_with_spaces() {
    run_slurm -m "$MF" "$FIXTURE_DIR/test_app.sh" "hello world"
    assert_exit_code 0 &&
    assert_mock_log_contains "hello world"
}

######################################################################
# Category 2: Machine source modes
######################################################################

test_machinefile_only() {
    run_slurm -m "$MF" "$FIXTURE_DIR/test_app.sh"
    assert_exit_code 0 &&
    assert_mock_log_contains "MOCK_SRUN args=-n 1"
}

test_auto_machinefile() {
    # No -m: the launcher must derive the machine file from the Slurm
    # allocation via "scontrol show hostnames" and build machines_<jobid>.
    export SLURM_JOB_ID=4242
    export SLURM_NTASKS=2 SLURM_NPROCS=2
    run_slurm "$FIXTURE_DIR/test_app.sh"
    assert_exit_code 0 &&
    assert_mock_log_contains "MOCK_SCONTROL args=show hostnames" &&
    assert_mock_log_contains "MOCK_SRUN args=-n 2"
}

test_m_and_n_truncate() {
    # machinefile has 3 hosts, -n 2 should launch only 2 tasks.
    export SLURM_NTASKS=3 SLURM_NPROCS=3
    run_slurm -m "$MF3" -n 2 "$FIXTURE_DIR/test_app.sh"
    assert_exit_code 0 &&
    assert_mock_log_contains "MOCK_SRUN args=-n 2"
}

test_bare_salloc_no_ntasks() {
    # Headline fix: a bare 'salloc -N2' sets neither SLURM_NTASKS nor
    # SLURM_TASKS_PER_NODE. The launcher must still derive the machine file
    # (one task per node) and run, instead of failing on a missing variable.
    export SLURM_JOB_ID=7000
    unset SLURM_NTASKS SLURM_NPROCS
    export SLURM_TEST_HOSTS="hosta hostb"
    run_slurm "$FIXTURE_DIR/test_app.sh"
    assert_exit_code 0 &&
    assert_mock_log_contains "MOCK_SCONTROL args=show hostnames" &&
    assert_mock_log_contains "MOCK_SRUN args=-n 2"
}

test_tasks_per_node_decompress() {
    # SLURM_TASKS_PER_NODE drives the per-node layout and is given in compressed
    # form: "2(x2)" => 2 tasks on each of 2 nodes => 4 ranks total.
    export SLURM_JOB_ID=7001
    unset SLURM_NTASKS SLURM_NPROCS
    export SLURM_TEST_HOSTS="hosta hostb"
    export SLURM_TASKS_PER_NODE="2(x2)"
    run_slurm "$FIXTURE_DIR/test_app.sh"
    assert_exit_code 0 &&
    assert_mock_log_contains "MOCK_SRUN args=-n 4"
}

test_m_and_n_exact() {
    # machinefile has 3 hosts, -n 3 should use all of them.
    export SLURM_NTASKS=3 SLURM_NPROCS=3
    run_slurm -m "$MF3" -n 3 "$FIXTURE_DIR/test_app.sh"
    assert_exit_code 0 &&
    assert_mock_log_contains "MOCK_SRUN args=-n 3"
}

test_n_preserves_machinefile() {
    # Regression: -n truncation must not overwrite the user's machine file.
    export SLURM_NTASKS=3 SLURM_NPROCS=3
    local before
    before=$(cat "$MF3")
    run_slurm -m "$MF3" -n 2 "$FIXTURE_DIR/test_app.sh"
    assert_exit_code 0 &&
    assert_mock_log_contains "MOCK_SRUN args=-n 2" &&
    assert_machinefile_unchanged "$MF3" "$before"
}

######################################################################
# Category 3: Option interactions
######################################################################

test_mfile_broadcast() {
    # The machine file must be broadcast to the nodes (sbcast) so every rank can
    # read GASPI_MFILE locally; srun must then receive the node-local path, not
    # the submit-node temp file.
    run_slurm -m "$MF" "$FIXTURE_DIR/test_app.sh"
    assert_exit_code 0 &&
    assert_mock_log_contains "MOCK_SBCAST" &&
    assert_mock_log_contains "/tmp/gaspi.mfile."
}

test_numa_option() {
    run_slurm -N -m "$MF" "$FIXTURE_DIR/test_app.sh"
    assert_exit_code 0
}

######################################################################
# Category 3b: Tracing option (-t / --trace)
######################################################################

test_trace_default() {
    # No category after -t -> defaults to "all"; the process must see it.
    run_slurm -t -m "$MF" "$FIXTURE_DIR/trace_probe.sh"
    assert_exit_code 0 &&
    assert_output_contains "GASPI_TRACE=all"
}

test_trace_explicit_categories() {
    run_slurm -t comm,io -m "$MF" "$FIXTURE_DIR/trace_probe.sh"
    assert_exit_code 0 &&
    assert_output_contains "GASPI_TRACE=comm,io"
}

test_trace_long_option() {
    run_slurm --trace io -m "$MF" "$FIXTURE_DIR/trace_probe.sh"
    assert_exit_code 0 &&
    assert_output_contains "GASPI_TRACE=io"
}

test_trace_absent() {
    run_slurm -m "$MF" "$FIXTURE_DIR/trace_probe.sh"
    assert_exit_code 0 &&
    assert_output_contains "GASPI_TRACE=<unset>"
}

test_trace_forwarded_through_srun() {
    # GASPI_TRACE must be exported before srun so it is forwarded to tasks.
    run_slurm -t all -m "$MF" "$FIXTURE_DIR/trace_probe.sh"
    assert_exit_code 0 &&
    assert_mock_log_contains "GASPI_TRACE=all"
}

test_trace_no_category_before_binary() {
    # -t immediately before the binary path: not a valid category list, so
    # it must fall back to "all" without swallowing the binary argument.
    run_slurm -m "$MF" -t "$FIXTURE_DIR/trace_probe.sh"
    assert_exit_code 0 &&
    assert_output_contains "GASPI_TRACE=all"
}

test_trace_in_help() {
    run_slurm -h
    assert_exit_code 0 &&
    assert_output_contains "trace [categories]"
}

######################################################################
# Category 4: Error cases
######################################################################

test_error_no_binary() {
    run_slurm -m "$MF"
    assert_exit_code 1 &&
    assert_output_contains "No binary file provided"
}

test_error_nonexistent_binary() {
    run_slurm -m "$MF" /nonexistent/program
    assert_exit_code 1
}

test_error_nonexecutable_binary() {
    local tmpfile=$(mktemp /tmp/gaspi_slurm_noexec.XXXXXX)
    chmod -x "$tmpfile"
    run_slurm -m "$MF" "$tmpfile"
    assert_exit_code 1 &&
    assert_output_contains "Cannot execute"
    rm -f "$tmpfile"
}

test_error_nonexistent_machinefile() {
    run_slurm -m /nonexistent/machines "$FIXTURE_DIR/test_app.sh"
    assert_exit_code 1 &&
    assert_output_contains "Cannot read"
}

test_error_empty_machinefile() {
    # An empty machine file must fail cleanly (no division-by-zero crash) and
    # leave no temp files behind.
    local emptyfile
    emptyfile=$(mktemp /tmp/gaspi_slurm_empty.XXXXXX)
    run_slurm -m "$emptyfile" "$FIXTURE_DIR/test_app.sh"
    assert_exit_code 1 &&
    assert_output_contains "Machine file is empty" &&
    assert_no_temp_files
    rm -f "$emptyfile"
}

test_error_invalid_option() {
    run_slurm -z "$FIXTURE_DIR/test_app.sh"
    assert_exit_code 1 &&
    assert_output_contains "Invalid option"
}

test_error_n_nonnumeric() {
    run_slurm -n abc -m "$MF" "$FIXTURE_DIR/test_app.sh"
    assert_exit_code 1 &&
    assert_output_contains "numeric"
}

test_error_n_exceeds_resources() {
    # -n exceeds the number of hosts in the machine file.
    export SLURM_NTASKS=5 SLURM_NPROCS=5
    run_slurm -m "$MF" -n 3 "$FIXTURE_DIR/test_app.sh"
    assert_exit_code 1 &&
    assert_output_contains "exceeds available/provided resources"
}

test_error_mfile_exceeds_allocation() {
    # machine file requests more tasks than the Slurm allocation provides.
    export SLURM_NTASKS=2 SLURM_NPROCS=2
    run_slurm -m "$MF3" "$FIXTURE_DIR/test_app.sh"
    assert_exit_code 1 &&
    assert_output_contains "exceeds the Slurm job's allocation"
}

test_error_no_mfile_no_jobid() {
    # No -m and no SLURM_JOB_ID: cannot derive a machine file.
    unset SLURM_JOB_ID
    run_slurm "$FIXTURE_DIR/test_app.sh"
    assert_exit_code 1 &&
    assert_output_contains "SLURM_JOB_ID not defined"
}

test_nested_step_uses_overlap() {
    # Launched inside an srun step (e.g. an interactive 'srun --pty bash'): a
    # plain nested step would be blocked, so the launcher must overlap the outer
    # step (srun --overlap) instead of failing.
    export SLURM_STEP_ID=0
    run_slurm -m "$MF" "$FIXTURE_DIR/test_app.sh"
    assert_exit_code 0 &&
    assert_mock_log_contains "--overlap" &&
    assert_no_temp_files
}

test_nested_step_skips_allocation_ceiling() {
    # Inside a step SLURM_NTASKS reflects the outer step (here 1), but --overlap
    # can use the whole allocation, so a 3-host machine file must NOT trip the
    # "exceeds the Slurm job's allocation" guard.
    export SLURM_STEP_ID=0
    export SLURM_NTASKS=1 SLURM_NPROCS=1
    run_slurm -m "$MF3" "$FIXTURE_DIR/test_app.sh"
    assert_exit_code 0 &&
    assert_mock_log_contains "MOCK_SRUN args=-n 3" &&
    assert_mock_log_contains "--overlap"
}

test_nested_step_without_overlap_support() {
    # Old Slurm (< 20.11): the capability probe finds no --overlap, so the
    # launcher must NOT pass it, and must warn the user instead of failing.
    export SLURM_STEP_ID=0
    export MOCK_SRUN_NO_OVERLAP=1
    run_slurm -m "$MF" "$FIXTURE_DIR/test_app.sh"
    assert_exit_code 0 &&
    assert_mock_log_not_contains "--overlap" &&
    assert_stderr_contains "lacks --overlap"
}

######################################################################
# Category 5: Cleanup and exit codes
######################################################################

test_exit_success() {
    run_slurm -m "$MF" "$FIXTURE_DIR/test_app.sh"
    assert_exit_code 0 &&
    assert_no_temp_files
}

test_exit_failure_on_app_error() {
    run_slurm -m "$MF" "$FIXTURE_DIR/test_app_fail.sh"
    assert_exit_code 1 &&
    assert_no_temp_files
}

test_temp_files_cleaned_on_error() {
    run_slurm -m /nonexistent/file "$FIXTURE_DIR/test_app.sh"
    assert_exit_code 1 &&
    assert_no_temp_files
}

######################################################################
# Category 6: Program path resolution
######################################################################

test_relative_path_absolutized() {
    # Regression: 'which ./foo' echoes the relative path back unchanged, so a
    # relative program path must be absolutized before srun forwards it to the
    # allocated nodes (whose task CWD is $HOME, not the launch dir).
    run_slurm_in_dir "$FIXTURE_DIR" -m "$MF" ./test_app.sh
    assert_exit_code 0 &&
    assert_mock_log_contains "$FIXTURE_DIR/test_app.sh" &&
    assert_mock_log_not_contains "./test_app.sh"
}

test_path_command_name() {
    # A bare command found on PATH must still be resolved to an absolute path.
    TEST_EXIT=0
    ( export PATH="$FIXTURE_DIR:$PATH"; "$GASPI_RUN_SLURM" -m "$MF" test_app.sh ) \
        > "$TEST_STDOUT" 2> "$TEST_STDERR" || TEST_EXIT=$?
    assert_exit_code 0 &&
    assert_mock_log_contains "$FIXTURE_DIR/test_app.sh"
}

######################################################################
# Main
######################################################################

echo "========================================="
echo " gaspi_run.slurm test suite"
echo "========================================="
echo

echo "--- Argument forwarding ---"
run_test test_no_args
run_test test_single_arg
run_test test_multi_args
run_test test_args_with_spaces

echo
echo "--- Machine source modes ---"
run_test test_machinefile_only
run_test test_auto_machinefile
run_test test_bare_salloc_no_ntasks
run_test test_tasks_per_node_decompress
run_test test_m_and_n_truncate
run_test test_m_and_n_exact
run_test test_n_preserves_machinefile

echo
echo "--- Option interactions ---"
run_test test_mfile_broadcast
run_test test_numa_option

echo
echo "--- Tracing option (-t / --trace) ---"
run_test test_trace_default
run_test test_trace_explicit_categories
run_test test_trace_long_option
run_test test_trace_absent
run_test test_trace_forwarded_through_srun
run_test test_trace_no_category_before_binary
run_test test_trace_in_help

echo
echo "--- Error cases ---"
run_test test_error_no_binary
run_test test_error_nonexistent_binary
run_test test_error_nonexecutable_binary
run_test test_error_nonexistent_machinefile
run_test test_error_empty_machinefile
run_test test_error_invalid_option
run_test test_error_n_nonnumeric
run_test test_error_n_exceeds_resources
run_test test_error_mfile_exceeds_allocation
run_test test_error_no_mfile_no_jobid

echo
echo "--- Nested step (interactive srun --pty) ---"
run_test test_nested_step_uses_overlap
run_test test_nested_step_skips_allocation_ceiling
run_test test_nested_step_without_overlap_support

echo
echo "--- Cleanup and exit codes ---"
run_test test_exit_success
run_test test_exit_failure_on_app_error
run_test test_temp_files_cleaned_on_error

echo
echo "--- Program path resolution ---"
run_test test_relative_path_absolutized
run_test test_path_command_name

echo
echo "========================================="
echo " Results: $TESTS_PASS/$TESTS_RUN passed, $TESTS_FAIL failed"
echo "========================================="

if [ "$TESTS_FAIL" -gt 0 ]; then
    echo
    printf "Failures:$FAILURES\n"
    exit 1
fi

exit 0
