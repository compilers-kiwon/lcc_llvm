#!/bin/sh
# fetch-suites.sh dir - check out the external test suites that
# src/run-llvm.sh takes, at the commits it was checked against, into
# dir/c-testsuite and dir/llvm-test-suite (only SingleSource).  They are
# fetched, not copied into this repository: torture is GPL.
set -e
D=${1:?usage: fetch-suites.sh dir}
mkdir -p "$D"

# fetch dir url commit [sparse paths...]
fetch() {
	dir=$1 url=$2 rev=$3; shift 3
	if [ -d "$dir/.git" ] && [ "`git -C "$dir" rev-parse HEAD`" = "$rev" ]; then
		return 0
	fi
	rm -rf "$dir"
	git init -q "$dir"
	git -C "$dir" remote add origin "$url"
	if [ $# -gt 0 ]; then
		git -C "$dir" sparse-checkout set "$@"
	fi
	git -C "$dir" fetch -q --depth 1 --filter=blob:none origin "$rev"
	git -C "$dir" checkout -q FETCH_HEAD
}

fetch "$D/c-testsuite" https://github.com/c-testsuite/c-testsuite.git \
	5c7275656d751de0e68b2d340a95b5681858ed07
fetch "$D/llvm-test-suite" https://github.com/llvm/llvm-test-suite.git \
	28644d9795d11dcc30f5bdeb398a76a52ca53869 \
	SingleSource/Benchmarks SingleSource/Regression SingleSource/UnitTests
