#!/bin/bash

# Copyright (c) Fraunhofer ITWM, 2013-2026

# This file is part of GPI-2.

# GPI-2 is free software; you can redistribute it
# and/or modify it under the terms of the GNU General Public License
# version 3 as published by the Free Software Foundation.

# GPI-2 is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with GPI-2. If not, see <http://www.gnu.org/licenses/>.

TMP_PATTERN="/tmp/.gpi2.XXXXXXXX"
HOSTFILE=$(mktemp "${TMP_PATTERN}")   # canonical file: GASPI machine file + SLURM_HOSTFILE
TMP_MFILE=$(mktemp "${TMP_PATTERN}")  # scratch (-n truncation)

GPI2_BINS_LOCATION="$(cd "$(dirname "$0")" && pwd)"
GPI2_ENV="${GPI2_BINS_LOCATION}/slurm.env"
GPI2_LAUNCHER="srun"
GPI2_MFILE=""
GPI2_NTASKS=0          # from -n (0 == not set)
GPI2_PRG=""
GPI2_PRG_ARGS=()
GPI2_SET_NUMA=0
GPI2_TRACE=""

remove_temp_files()
{
    rm -f "$HOSTFILE" "$TMP_MFILE"
}

clean_exit()
{
    remove_temp_files
    exit "$1"
}

print_error_exit_failure()
{
    echo
    echo "Error: $1"
    echo
    clean_exit 1
}

usage()
{
    echo
    echo "Usage: gaspi_run [OPTIONS] <path GASPI program>"
    echo
    echo "Available [OPTIONS]:"
    echo "  -m, --machinefile <machine file> File with hostnames to use."
    echo "  -N, --NUMA                 Enable NUMA for processes on same node."
    echo "  -n, --nodes  <procs>       Start as many as <procs>."
    echo "  -t, --trace [categories]   Enable tracing (default: all)."
    echo "  -h, --help                 This help."
    echo
    remove_temp_files
}

utility_exists()
{
    command -v "$1" > /dev/null 2>&1
}

#  srun support --overlap (Slurm >= 20.11)? Needed to share resources with an
# outer step (e.g. an interactive 'srun --pty bash').
srun_supports_overlap()
{
    "$GPI2_LAUNCHER" --help 2>&1 | grep -q -- '--overlap'
}

find_set_NUMA_config()
{
    if utility_exists numactl || utility_exists lscpu ; then
        GPI2_SET_NUMA=1
    else
        echo "Warning: unable to detect NUMA configuration. Option not used."
        GPI2_SET_NUMA=0
    fi
}

# Expand a SLURM_TASKS_PER_NODE spec ("4(x2),2") into per-node counts ("4 4 2").
decompress_tasks_per_node()
{
    local spec="$1" item n count k out=""
    local IFS=','
    for item in $spec ; do
        if [[ "$item" =~ ^([0-9]+)\(x([0-9]+)\)$ ]]; then
            n=${BASH_REMATCH[1]}
            count=${BASH_REMATCH[2]}
            for ((k = 0; k < count; k++)); do out+="$n "; done
        else
            out+="$item "
        fi
    done
    echo "$out"
}

# Build HOSTFILE (one host per rank, in rank order) from the Slurm allocation.
build_hostfile_from_slurm()
{
    local nodes nnodes counts total base rem i j

    if [ -n "${SLURM_JOB_NODELIST:-}" ]; then
        mapfile -t nodes < <(scontrol show hostnames "$SLURM_JOB_NODELIST")
    else
        mapfile -t nodes < <(scontrol show hostnames)
    fi
    nnodes=${#nodes[@]}
    if [ "$nnodes" -eq 0 ]; then
        print_error_exit_failure "Failed to get hostnames to run application."
    fi

    if [ -n "${SLURM_TASKS_PER_NODE:-}" ]; then
        # Authoritative per-node layout from Slurm.
        counts=( $(decompress_tasks_per_node "$SLURM_TASKS_PER_NODE") )
    else
        # Total rank count: SLURM_NTASKS if set, else one task per node.
        if [ -n "${SLURM_NTASKS:-}" ]; then
            total=$SLURM_NTASKS
        else
            total=$nnodes
        fi
        # Distribute as evenly as possible across the nodes.
        base=$((total / nnodes))
        rem=$((total % nnodes))
        counts=()
        for ((i = 0; i < nnodes; i++)); do
            if [ "$i" -lt "$rem" ]; then
                counts+=( $((base + 1)) )
            else
                counts+=( "$base" )
            fi
        done
    fi

    : > "$HOSTFILE"
    for ((i = 0; i < nnodes && i < ${#counts[@]}; i++)); do
        for ((j = 0; j < counts[i]; j++)); do
            echo "${nodes[i]}" >> "$HOSTFILE"
        done
    done
}

# Copy a user-provided machine file into HOSTFILE, tolerating a missing final
# newline and skipping blank lines. The user's file is left untouched.
build_hostfile_from_mfile()
{
    local line
    : > "$HOSTFILE"
    while IFS= read -r line || [ -n "$line" ]; do
        [ -z "$line" ] && continue
        echo "$line" >> "$HOSTFILE"
    done < "$GPI2_MFILE"
}

################################ GASPI_RUN ######################################

GPI2_IN_STEP=0
if [ -n "${SLURM_STEP_ID:-}" ]; then
    GPI2_IN_STEP=1
fi

#command line parsing
while [ -n "$1" ]; do
    case $1 in
        -m | --machinefile)
            if [ -r "$2" ]; then
                GPI2_MFILE=$2
            else
                print_error_exit_failure "Cannot read $2 (-m option) (or file does not exist)"
            fi
            shift
            ;;
        -N | --NUMA)
            find_set_NUMA_config
            ;;
        -h | --help)
            usage
            exit 0
            ;;
        -n | --nodes)
            if [ -n "$2" ] && expr "$2" + 0 > /dev/null 2>&1 ; then
                GPI2_NTASKS=$2
            else
                print_error_exit_failure "Option -n requires a numeric argument greater than 0."
            fi
            shift
            ;;
        -t | --trace)
            # Optional bare category list; otherwise default to "all" without
            # swallowing a following binary path or option.
            if [ -n "$2" ] && [[ "$2" =~ ^[a-z0-9,]+$ ]]; then
                GPI2_TRACE="$2"
                shift
            else
                GPI2_TRACE="all"
            fi
            ;;
        -*)
            print_error_exit_failure "Invalid option ($1). Use -h (or --help) for available options."
            ;;
        *)
            # 'which ./bin/foo' returns the relative path unchanged, so always
            # absolutize with readlink -f (srun launches tasks from $HOME).
            FULLPATH=$(which "$1" 2> /dev/null)
            if [ -z "$FULLPATH" ]; then
                FULLPATH="$1"
            fi
            FULLPATH=$(readlink -f "$FULLPATH")
            if [ -f "$FULLPATH" ]; then
                if [ -x "$FULLPATH" ]; then
                    GPI2_PRG="$FULLPATH"
                    shift
                    GPI2_PRG_ARGS=("$@")
                    break
                else
                    print_error_exit_failure "Cannot execute $1 (or file does not exist)"
                fi
            else
                print_error_exit_failure "Cannot execute $1 (not a file)"
            fi
    esac
    shift
done

if [ -z "$GPI2_PRG" ]; then
    print_error_exit_failure "No binary file provided. See help (-h or --help options)"
fi

# Build the canonical host file: from the user's -m file, or from Slurm.
if [ -n "$GPI2_MFILE" ]; then
    build_hostfile_from_mfile
else
    if [ -z "${SLURM_JOB_ID:-}" ]; then
        print_error_exit_failure "SLURM_JOB_ID not defined. Are you running within a Slurm allocation?"
    fi
    build_hostfile_from_slurm
fi

# Optional -n: cap the number of ranks (leaving the user's file untouched).
if [ "$GPI2_NTASKS" -ne 0 ]; then
    avail=$(wc -l < "$HOSTFILE")
    if [ "$GPI2_NTASKS" -gt "$avail" ]; then
        print_error_exit_failure "Number of requested tasks (-n ${GPI2_NTASKS}) exceeds available/provided resources (${avail})."
    fi
    head -n "$GPI2_NTASKS" "$HOSTFILE" > "$TMP_MFILE"
    cp "$TMP_MFILE" "$HOSTFILE"
fi

NTASKS=$(wc -l < "$HOSTFILE")

# Reject an empty host file up front (avoids a divide-by-zero / empty srun).
if [ "$NTASKS" -eq 0 ]; then
    print_error_exit_failure "Machine file is empty (no hosts to run on)."
fi

if [ "$GPI2_IN_STEP" -eq 0 ] && [ -n "${SLURM_NTASKS:-}" ] && [ "$NTASKS" -gt "$SLURM_NTASKS" ]; then
    print_error_exit_failure "Resources requested (${NTASKS}) exceeds the Slurm job's allocation (${SLURM_NTASKS})."
fi

# Tracing is global: export it so srun forwards it to every task. slurm.env
# re-exports it as a safety net under restrictive --export.
if [ -n "$GPI2_TRACE" ]; then
    export GASPI_TRACE="$GPI2_TRACE"
fi

export SLURM_HOSTFILE="$HOSTFILE"

# Slurm-native mechanism; needs no shared filesystem and scales). Fall back to
# the submit-node file for the single-node / no-sbcast case.
GPI2_NODE_MFILE="$HOSTFILE"
if utility_exists sbcast ; then
    GPI2_NODE_MFILE="/tmp/gaspi.mfile.${SLURM_JOB_ID:-$$}"
    if ! sbcast -f "$HOSTFILE" "$GPI2_NODE_MFILE" 2>/dev/null ; then
        echo "Warning: sbcast failed; falling back to the submit-node machine file."
        GPI2_NODE_MFILE="$HOSTFILE"
    fi
fi

# Keep -n first so it stays the leading flag. Inside an existing step, overlap
# it (Slurm >= 20.11) and clear the stale step id so the inner srun starts a
# fresh step.
GPI2_SRUN_OPTS=( -n "$NTASKS" --distribution=arbitrary )
if [ "$GPI2_IN_STEP" -eq 1 ]; then
    # Clear the stale step id so the inner srun starts a fresh step.
    unset SLURM_STEP_ID
    if srun_supports_overlap ; then
        echo "Info: running inside an existing Slurm step; the GASPI step will overlap it (srun --overlap)." >&2
        GPI2_SRUN_OPTS+=( --overlap )
    else
        echo "Warning: running inside a Slurm step but this srun lacks --overlap (Slurm < 20.11)." >&2
        echo "         Step creation may block; prefer launching gaspi_run from salloc/sbatch." >&2
    fi
fi

if ! "$GPI2_LAUNCHER" "${GPI2_SRUN_OPTS[@]}" \
        "$GPI2_ENV" "$GPI2_NODE_MFILE" "$GPI2_SET_NUMA" "$GPI2_PRG" "${GPI2_PRG_ARGS[@]}" ; then
    clean_exit 1
fi

#clean-up and exit
clean_exit 0
