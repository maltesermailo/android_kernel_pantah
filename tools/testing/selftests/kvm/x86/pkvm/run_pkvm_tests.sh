#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu

usage()
{
	cat <<-EOF
	Usage: $0 [-t seconds] [test ...]

	Run x86 pKVM KVM selftests from this directory.

	Options:
	  -t seconds  Per-test timeout in seconds (default: 90)
	  -h          Show this help

	If one or more test names are provided, run only those tests.
	Names may be given with or without the x86/pkvm/ prefix.
	EOF
}

timeout_secs=90

while getopts "ht:" opt; do
	case "$opt" in
	h)
		usage
		exit 0
		;;
	t)
		timeout_secs=$OPTARG
		;;
	*)
		usage >&2
		exit 1
		;;
	esac
done
shift $((OPTIND - 1))

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

pass=0
skip=0
fail=0
timeout_count=0

run_one()
{
	test_path=$1
	test_name=$(basename -- "$test_path")
	tmp_log=$(mktemp "/tmp/${test_name}.XXXXXX")

	printf '=== %s ===\n' "${test_path#${script_dir}/}"
	if timeout "${timeout_secs}s" "$test_path" >"$tmp_log" 2>&1; then
		rc=0
	else
		rc=$?
	fi

	# "timeout" preserves the test's exit status unless it expires.
	# Kselftest uses 4 for skip, while GNU timeout returns 124 on timeout.
	case "$rc" in
	0)
		status=PASS
		pass=$((pass + 1))
		;;
	4)
		status=SKIP
		skip=$((skip + 1))
		;;
	124)
		status=TIMEOUT
		timeout_count=$((timeout_count + 1))
		;;
	*)
		status=FAIL
		fail=$((fail + 1))
		;;
	esac

	printf '%s\n' "$status"
	tail -n 20 "$tmp_log"
	rm -f "$tmp_log"
}

if [ "$#" -eq 0 ]; then
	for test_path in "$script_dir"/pkvm_*; do
		[ -x "$test_path" ] || continue
		[ -f "$test_path" ] || continue
		[ "$(basename -- "$test_path")" = "$(basename -- "$0")" ] && continue
		run_one "$test_path"
	done
else
	for test_name in "$@"; do
		case "$test_name" in
		x86/pkvm/*)
			test_path=$(CDPATH= cd -- "$script_dir/../.." && pwd)/$test_name
			;;
		*)
			test_path="$script_dir/$test_name"
			;;
		esac

		if [ ! -x "$test_path" ] || [ ! -f "$test_path" ]; then
			printf 'Missing test: %s\n' "$test_name" >&2
			exit 1
		fi

		run_one "$test_path"
	done
fi

printf '\nSummary: pass=%d skip=%d fail=%d timeout=%d\n' \
	"$pass" "$skip" "$fail" "$timeout_count"

[ "$fail" -eq 0 ] && [ "$timeout_count" -eq 0 ]