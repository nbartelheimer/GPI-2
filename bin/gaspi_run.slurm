#!/bin/sh

# Copyright (c) Fraunhofer ITWM, 2013-2025

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
TMP_MFILE=$(mktemp ${TMP_PATTERN})
UNIQ_MFILE=$(mktemp ${TMP_PATTERN})

GPI2_LAUNCHER="srun"
GPI2_MAX_NUMA_NODES=256
GPI2_MFILE=""
GPI2_NTASKS=0
GPI2_PRG=""
GPI2_PRG_ARGS=""
GPI2_SET_NUMA=0
GPI2_TRACE=""

remove_temp_files()
{
    rm -f "$TMP_MFILE"
    rm -f "$UNIQ_MFILE"
}

#helper functions
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

clean_exit()
{
    local status=$1
    remove_temp_files

    exit $status
}

print_error_exit_failure()
{
    echo
    echo "Error: $1"
    echo
    clean_exit 1
}

utility_exists()
{
    if command -v "$1" > /dev/null 2>&1 ; then
	return 0;
    else
	return 1;
    fi
}

find_set_NUMA_config()
{
    if utility_exists numactl ; then
	GPI2_MAX_NUMA_NODES=$(numactl --hardware|grep available|cut -d' ' -f2)
	GPI2_SET_NUMA=1
    else
	if utility_exists lscpu ; then
	    GPI2_MAX_NUMA_NODES=$(lscpu|grep "NUMA node(s)"|tr -d [:space:]|cut -d':' -f2)
	else
	    echo "Warning: unable to detect NUMA configuration. Option not used."
	    GPI2_SET_NUMA=0
	fi
    fi
}

################################ GASPI_RUN ######################################

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
	    # Optional category list; falls back to "all" when the next
	    # token is missing or not a bare [a-z0-9,] category list
	    # (e.g. the binary path or another option).
	    case "$2" in
		*[!a-z0-9,]* | "")
		    GPI2_TRACE="all"
		    ;;
		*)
		    GPI2_TRACE="$2"
		    shift
		    ;;
	    esac
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
		    GPI2_PRG_ARGS="$*"
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

#create  machine file if not user-provided
if [ -z "$GPI2_MFILE" ]; then
    if [ -z "$SLURM_JOB_ID" ]; then
	print_error_exit_failure "SLURM_JOB_ID not defined. Are you running within a Slurm allocation?"
    fi


    if ! scontrol show hostnames | uniq > "$UNIQ_MFILE" 2>/dev/null ; then
	print_error_exit_failure "Failed to get hostnames to run application."
    fi

    if [ -z "$SLURM_NTASKS" ]; then
	print_error_exit_failure "SLURM_NTASKS not defined. Are you running within a Slurm allocation?"
    fi
    if [ -z "$SLURM_NNODES" ]; then
	print_error_exit_failure "SLURM_NNODES not defined. Are you running within a Slurm allocation?"
    fi

    tasks_per_node=$((SLURM_NTASKS / SLURM_NNODES))

    GPI2_MFILE="machines_"$SLURM_JOB_ID
    if [ -r "$GPI2_MFILE" ]; then
	rm -f "$GPI2_MFILE"
    fi

    while IFS= read -r host ;
    do
	for i in $(seq $tasks_per_node)
	do
	    echo "$host" >> "$GPI2_MFILE"
	done
    done <  "$UNIQ_MFILE"

else
    #user provided machine file

    uniq "$GPI2_MFILE" > "$UNIQ_MFILE" 2>/dev/null
fi

#user-provided number of tasks
if [ "$GPI2_NTASKS" -ne 0 ]; then
    tasks_in_mfile=$(wc -l "$GPI2_MFILE"  | cut -d' ' -f1)
    if [ $GPI2_NTASKS -gt $tasks_in_mfile ]; then
	print_error_exit_failure "Number of requested tasks (-n) exceeds available/provided resources."
    fi
    head -n "$GPI2_NTASKS" "$GPI2_MFILE" > "$TMP_MFILE"
    # Use the truncated copy rather than overwriting the user's machine file.
    GPI2_MFILE="$TMP_MFILE"
    uniq "$GPI2_MFILE" > "$UNIQ_MFILE"
fi

GPI2_NTASKS=$(wc -l "$GPI2_MFILE" | cut -d' ' -f1)

# Reject an empty machine file up front
if [ "$GPI2_NTASKS" -eq 0 ]; then
    print_error_exit_failure "Machine file is empty (no hosts to run on)."
fi

if [ "$GPI2_NTASKS" -gt "$SLURM_NTASKS" ]; then
    print_error_exit_failure "Resources requested (${GPI2_NTASKS}) exceeds the Slurm job's allocation (${SLURM_NTASKS})."
fi


if [ -z "$SLURM_PROCID" ]; then
    print_error_exit_failure "Environment variable SLURM_PROCID not defined."
else
    if [ 0 -eq "$SLURM_PROCID" ]; then
	NNODES=$(wc -l < "$UNIQ_MFILE")
    fi
fi

if [ $((GPI2_NTASKS / NNODES)) -gt "$GPI2_MAX_NUMA_NODES" ]; then
    print_error_exit_failure "NUMA option (-N): max procs per node: $GPI2_MAX_NUMA_NODES (used: $((GPI2_NTASKS / NNODES)))."
fi

# Tracing is global (not per-rank): export it so srun forwards it to every
# task. slurm.env re-exports it as a safety net under restrictive --export.
if [ -n "$GPI2_TRACE" ]; then
    export GASPI_TRACE="$GPI2_TRACE"
fi

if ! $GPI2_LAUNCHER -n "$GPI2_NTASKS" -N "$NNODES" "$(readlink -f "$(dirname "$0")")/slurm.env" "$GPI2_MFILE" $GPI2_SET_NUMA "$GPI2_PRG" "$GPI2_PRG_ARGS" ; then
    clean_exit 1
fi

wait

#clean-up and exit
clean_exit 0
