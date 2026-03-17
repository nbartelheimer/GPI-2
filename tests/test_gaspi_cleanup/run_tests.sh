#!/bin/bash
#
# Test harness for gaspi_cleanup
#
# Mocks ssh, ping, killall, pkill, and pgrep so tests run locally
# without needing actual remote hosts or running GASPI processes.
#
# Tests are written against desired behavior. Tests that expose current
# bugs will be red until the corresponding fix is applied.
#

set -u

TEST_DIR="$(cd "$(dirname "$0")" && pwd)"
GASPI_CLEANUP="$(cd "$TEST_DIR/../../bin" && pwd)/gaspi_cleanup"
MOCK_DIR="$TEST_DIR/mocks"

# Test counters
TESTS_RUN=0
TESTS_PASS=0
TESTS_FAIL=0
FAILURES=""

ORIG_USER="$(id -un)"

######################################################################
# Setup / teardown
######################################################################

setup() {
    export MOCK_LOG
    MOCK_LOG=$(mktemp /tmp/gaspi_cleanup_test_mock.XXXXXX)
    TEST_STDOUT=$(mktemp /tmp/gaspi_cleanup_test_out.XXXXXX)
    TEST_STDERR=$(mktemp /tmp/gaspi_cleanup_test_err.XXXXXX)
    TEST_EXIT=0

    # Prepend mocks to PATH so ssh/ping/killall/pkill/pgrep resolve to ours
    export PATH="$MOCK_DIR:$PATH"

    # Default mock behavior
    export MOCK_PING_FAIL=""
    export MOCK_SSH_FAIL=""
    export MOCK_PKILL_EXIT=0
    export MOCK_KILLALL_EXIT=0
    export MOCK_PGREP_RUNNING=""

    # Save and overwrite /tmp/.last_gaspi_prg for test isolation
    SAVED_LAST_PRG=""
    if [ -f /tmp/.last_gaspi_prg ]; then
        SAVED_LAST_PRG=$(cat /tmp/.last_gaspi_prg)
    fi
    echo "/usr/local/bin/my_gaspi_app" > /tmp/.last_gaspi_prg

    # Create machine files
    MF_SINGLE=$(mktemp /tmp/gaspi_cleanup_test_mf.XXXXXX)
    printf '%s\n' "node01" > "$MF_SINGLE"

    MF_MULTI=$(mktemp /tmp/gaspi_cleanup_test_mf.XXXXXX)
    printf '%s\n%s\n%s\n' "node01" "node02" "node03" > "$MF_MULTI"

    # Non-adjacent duplicate: uniq won't remove the second node01
    MF_DUPES=$(mktemp /tmp/gaspi_cleanup_test_mf.XXXXXX)
    printf '%s\n%s\n%s\n' "node01" "node02" "node01" > "$MF_DUPES"

    MF_EMPTY=$(mktemp /tmp/gaspi_cleanup_test_mf.XXXXXX)
    # intentionally left empty
}

teardown() {
    rm -f "$MOCK_LOG" "$TEST_STDOUT" "$TEST_STDERR"
    rm -f "$MF_SINGLE" "$MF_MULTI" "$MF_DUPES" "$MF_EMPTY"

    # Restore /tmp/.last_gaspi_prg to its original state
    if [ -n "$SAVED_LAST_PRG" ]; then
        echo "$SAVED_LAST_PRG" > /tmp/.last_gaspi_prg
    else
        rm -f /tmp/.last_gaspi_prg
    fi
}

######################################################################
# Test helpers
######################################################################

run_cleanup() {
    TEST_EXIT=0
    "$GASPI_CLEANUP" "$@" > "$TEST_STDOUT" 2> "$TEST_STDERR" || TEST_EXIT=$?
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
    if ! grep -qF "$pattern" "$TEST_STDOUT"; then
        fail "stdout missing: '$pattern'"
        echo "  stdout was: $(cat "$TEST_STDOUT")"
        return 1
    fi
    return 0
}

assert_output_not_contains() {
    local pattern="$1"
    if grep -qF "$pattern" "$TEST_STDOUT"; then
        fail "stdout unexpectedly contains: '$pattern'"
        echo "  stdout was: $(cat "$TEST_STDOUT")"
        return 1
    fi
    return 0
}

assert_mock_log_contains() {
    local pattern="$1"
    if ! grep -qF "$pattern" "$MOCK_LOG" 2>/dev/null; then
        fail "mock log missing: '$pattern'"
        echo "  mock log was: $(cat "$MOCK_LOG" 2>/dev/null)"
        return 1
    fi
    return 0
}

assert_mock_log_not_contains() {
    local pattern="$1"
    if grep -qF "$pattern" "$MOCK_LOG" 2>/dev/null; then
        fail "mock log unexpectedly contains: '$pattern'"
        echo "  mock log was: $(cat "$MOCK_LOG" 2>/dev/null)"
        return 1
    fi
    return 0
}

assert_mock_log_count() {
    local pattern="$1"
    local expected="$2"
    local count
    count=$(grep -cF "$pattern" "$MOCK_LOG" 2>/dev/null || true)
    if [ "$count" -ne "$expected" ]; then
        fail "mock log: expected $expected occurrence(s) of '$pattern', got $count"
        echo "  mock log was: $(cat "$MOCK_LOG" 2>/dev/null)"
        return 1
    fi
    return 0
}

# Test result tracking
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
# Category 1: Help and argument parsing
######################################################################

test_help_short_alone() {
    # -h alone should show usage and exit 0
    # Currently blocked by the "$# -lt 2" guard that fires first
    run_cleanup -h
    assert_exit_code 0 &&
    assert_output_contains "Usage:"
}

test_help_long_alone() {
    # --help alone should show usage and exit 0
    run_cleanup --help
    assert_exit_code 0 &&
    assert_output_contains "Usage:"
}

test_no_args() {
    run_cleanup
    assert_exit_code 1 &&
    assert_output_contains "machinefile"
}

test_unknown_option() {
    run_cleanup -z -m "$MF_SINGLE"
    assert_exit_code 1 &&
    assert_output_contains "Unknown option"
}

test_missing_machinefile_arg() {
    # -m with no following argument
    run_cleanup -m
    assert_exit_code 1
}

test_nonexistent_machinefile() {
    run_cleanup -m /nonexistent/machinefile
    assert_exit_code 1 &&
    assert_output_contains "Cannot read"
}

test_empty_machinefile() {
    run_cleanup -m "$MF_EMPTY"
    assert_exit_code 1 &&
    assert_output_contains "Empty"
}

######################################################################
# Category 2: PRG detection
######################################################################

test_program_option_overrides_prg_file() {
    # -p overrides whatever is in /tmp/.last_gaspi_prg
    run_cleanup -p other_app -m "$MF_SINGLE"
    assert_exit_code 0 &&
    assert_mock_log_contains "other_app" &&
    assert_mock_log_not_contains "my_gaspi_app"
}

test_program_option_no_prg_file_needed() {
    # -p works even when /tmp/.last_gaspi_prg is absent
    rm -f /tmp/.last_gaspi_prg
    run_cleanup -p my_gaspi_app -m "$MF_SINGLE"
    assert_exit_code 0
}

test_no_prg_file() {
    rm -f /tmp/.last_gaspi_prg
    run_cleanup -m "$MF_SINGLE"
    assert_exit_code 1 &&
    assert_output_contains "not found"
}

test_valid_prg_proceeds() {
    run_cleanup -m "$MF_SINGLE"
    assert_exit_code 0
}

######################################################################
# Category 3: Kill mode
######################################################################

test_kill_contacts_each_host() {
    run_cleanup -m "$MF_MULTI"
    assert_exit_code 0 &&
    assert_mock_log_count "MOCK_SSH host=node01" 1 &&
    assert_mock_log_count "MOCK_SSH host=node02" 1 &&
    assert_mock_log_count "MOCK_SSH host=node03" 1
}

test_kill_deduplicates_hosts() {
    # MF_DUPES has node01, node02, node01 (non-adjacent duplicate).
    # uniq only removes adjacent duplicates, so node01 gets SSH'd twice.
    # After fix 1.3 (sort -u), node01 should only be contacted once.
    run_cleanup -m "$MF_DUPES"
    assert_exit_code 0 &&
    assert_mock_log_count "MOCK_SSH host=node01" 1 &&
    assert_mock_log_count "MOCK_SSH host=node02" 1
}

test_kill_no_full_path() {
    # The kill command should use only the basename, not the full path
    # from /tmp/.last_gaspi_prg. Currently the script passes "$1" (full
    # path) as a killall argument alongside the basename.
    run_cleanup -m "$MF_SINGLE"
    assert_exit_code 0 &&
    assert_mock_log_not_contains "/usr/local/bin"
}

test_kill_targets_lt_prefix() {
    # The lt- variant (libtool uninstalled binary) must also be targeted
    run_cleanup -m "$MF_SINGLE"
    assert_exit_code 0 &&
    assert_mock_log_contains "lt-my_gaspi_app"
}

test_ssh_uses_batchmode() {
    # All SSH calls must use BatchMode=yes (fail fast on password prompts)
    # and ConnectTimeout=10 (fail fast on unresponsive hosts)
    run_cleanup -m "$MF_SINGLE"
    assert_exit_code 0 &&
    assert_mock_log_contains "BatchMode=yes" &&
    assert_mock_log_contains "ConnectTimeout=10"
}

test_ssh_uses_batchmode_in_check_mode() {
    run_cleanup -c -m "$MF_SINGLE"
    assert_exit_code 0 &&
    assert_mock_log_contains "BatchMode=yes" &&
    assert_mock_log_contains "ConnectTimeout=10"
}

test_kill_user_scoped() {
    # Kill must be scoped to the invoking user to avoid hitting other
    # users' processes with the same program name. After fix 2.1 this
    # switches to pkill with -u "$(id -un)".
    run_cleanup -m "$MF_SINGLE"
    assert_exit_code 0 &&
    assert_mock_log_contains "MOCK_PKILL" &&
    assert_mock_log_contains "$ORIG_USER"
}

test_kill_reports_ssh_failure() {
    # When SSH cannot reach a host, a warning must be printed.
    # Successful hosts produce no output.
    export MOCK_SSH_FAIL="node02"
    run_cleanup -m "$MF_MULTI"
    assert_exit_code 0 &&
    assert_output_contains "Warning" &&
    assert_output_contains "node02" &&
    assert_output_not_contains "node01" &&
    assert_output_not_contains "node03"
}

######################################################################
# Category 4: Check mode
######################################################################

test_check_reports_running() {
    export MOCK_PGREP_RUNNING="my_gaspi_app"
    run_cleanup -c -m "$MF_SINGLE"
    assert_exit_code 0 &&
    assert_output_contains "my_gaspi_app" &&
    assert_output_contains "node01"
}

test_check_reports_nothing_when_not_running() {
    # MOCK_PGREP_RUNNING="" → pgrep exits 1 → nothing printed
    run_cleanup -c -m "$MF_SINGLE"
    assert_exit_code 0 &&
    assert_output_not_contains "running"
}

test_check_validates_machinefile() {
    # Check mode should also reject an empty machine file.
    # Currently validate_machinefile is skipped in check mode.
    run_cleanup -c -m "$MF_EMPTY"
    assert_exit_code 1 &&
    assert_output_contains "Empty"
}

test_check_user_scoped() {
    # pgrep in check mode must be scoped to the invoking user.
    # After fix 2.2, pgrep will include -u "$(id -un)".
    export MOCK_PGREP_RUNNING="my_gaspi_app"
    run_cleanup -c -m "$MF_SINGLE"
    assert_exit_code 0 &&
    assert_mock_log_contains "MOCK_PGREP" &&
    assert_mock_log_contains "$ORIG_USER"
}

######################################################################
# Main
######################################################################

echo "========================================="
echo " gaspi_cleanup test suite"
echo "========================================="
echo

echo "--- Help and argument parsing ---"
run_test test_help_short_alone
run_test test_help_long_alone
run_test test_no_args
run_test test_unknown_option
run_test test_missing_machinefile_arg
run_test test_nonexistent_machinefile
run_test test_empty_machinefile

echo
echo "--- PRG detection ---"
run_test test_program_option_overrides_prg_file
run_test test_program_option_no_prg_file_needed
run_test test_no_prg_file
run_test test_valid_prg_proceeds

echo
echo "--- Kill mode ---"
run_test test_ssh_uses_batchmode
run_test test_kill_contacts_each_host
run_test test_kill_deduplicates_hosts
run_test test_kill_no_full_path
run_test test_kill_targets_lt_prefix
run_test test_kill_user_scoped
run_test test_kill_reports_ssh_failure

echo
echo "--- Check mode ---"
run_test test_ssh_uses_batchmode_in_check_mode
run_test test_check_reports_running
run_test test_check_reports_nothing_when_not_running
run_test test_check_validates_machinefile
run_test test_check_user_scoped

# Summary
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
