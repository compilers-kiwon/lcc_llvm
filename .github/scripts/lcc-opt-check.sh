#!/bin/sh
# lcc-opt-check.sh deps|lit|sanitizers|lcc|kernels|suite|all target ... -
# build lcc-opt, run its tests, and lcc's through it, as CI's lcc-opt job
# does, on Ubuntu 24.04, from the top directory of lcc
#
#   deps target       what ci-target.sh deps target mlir installs, LLVM 23
#                     from apt.llvm.org among it, and what lcc-opt needs
#                     besides: libmlir-23-dev and llvm-23-dev (libraries,
#                     CMake files), llvm-23-tools (lit, FileCheck),
#                     libclang-rt-23-dev (sanitizers), cmake and ninja
#   lit target work   build lcc-opt in work/lcc-opt and run its lit tests
#   sanitizers target work
#                     the same in work/lcc-opt-san, built by clang 23 with
#                     -fsanitize=address,undefined; a report fails the test
#   lcc target work   build lcc with the mlir back end for target in
#                     work/lcc, and in work/lcc-raise one whose driver takes
#                     its tools from work/bin (-DLLVMBIN), where mlir-opt is
#                     a script that runs lcc-opt --lcc-to-linalg --lcc-lower
#                     instead; check with lcc -v that it does
#   kernels target work
#                     lcc-opt/kernels/check.sh with work/lcc and lcc-opt,
#                     its files in work/kernels
#   suite target work args...
#                     src/run-llvm.sh -q -t target -b work/lcc-raise args
#                     with MLIRBIN=work/bin, so that its own check of the
#                     IR also goes through lcc-opt
#   all target work suites
#                     everything but deps in order, the suites lcc, unit,
#                     unit with DRVOPT=-O2, c-testsuite, torture and
#                     llvm-bench, from suites as fetch-suites.sh leaves them
#
# All but suite print the versions of the LLVM 23 packages first.  target
# is x86_64-linux or aarch64-linux, work a directory for the builds; $JOBS
# is the number of compilers to run at once (default: the processors).
# The exit status is 0 only if everything passes.
set -e
cmd=$1 T=$2
case "$T" in
x86_64-linux|aarch64-linux) ;;
*)	echo "usage: $0 deps|lit|sanitizers|lcc|kernels|suite|all x86_64-linux|aarch64-linux [work [args]]" 1>&2
	exit 2 ;;
esac
LLVMBIN=/usr/lib/llvm-23/bin
case $cmd in
deps)	;;
*)	[ -n "$3" ] || { echo "usage: $0 $cmd $T work ..." 1>&2; exit 2; }
	mkdir -p "$3"
	W=`cd "$3" && pwd`
	shift 3 ;;
esac
case $cmd in
all)	[ -z "$1" ] || S=`cd "$1" && pwd` ;;
esac
top=`dirname "$0"`/../..
top=`cd "$top" && pwd`
self=$top/.github/scripts/`basename "$0"`
cd "$top"
jobs=${JOBS:-`getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2`}

# the version of each LLVM 23 package and of its LLVM
versions() {
	dpkg-query -W -f='${Package} ${Version}\n' clang-23 lld-23 llvm-23 \
		llvm-23-dev llvm-23-tools libmlir-23-dev mlir-23-tools \
		libclang-rt-23-dev 2>&1 || true
	echo "LLVM `$LLVMBIN/llvm-config --version`"
}

# buildlcc dir [make variables]: lcc with the mlir back end for $T in dir
buildlcc() {
	d=$1; shift
	mkdir -p "$d"
	make -r -j$jobs BUILDDIR="$d" HOSTFILE=etc/mlir-$T.c "$@" rcc lcc cpp headers
	case $T in
	x86_64-linux)	make -r BUILDDIR="$d" liblcc ;;
	aarch64-linux)	make -r BUILDDIR="$d" CC=aarch64-linux-gnu-gcc liblcc ;;
	esac
}

case $cmd in
deps)
	.github/scripts/ci-target.sh deps "$T" mlir
	sudo apt-get install -y -q --no-install-recommends \
		libmlir-23-dev llvm-23-dev llvm-23-tools libclang-rt-23-dev \
		cmake ninja-build
	versions ;;
lit)
	versions
	cmake -G Ninja -S lcc-opt -B "$W/lcc-opt"
	ninja -C "$W/lcc-opt" -j$jobs
	ninja -C "$W/lcc-opt" check-lcc-opt ;;
sanitizers)
	versions
	san="-fsanitize=address,undefined -fno-sanitize-recover=all"
	cmake -G Ninja -S lcc-opt -B "$W/lcc-opt-san" \
		-DCMAKE_C_COMPILER=$LLVMBIN/clang \
		-DCMAKE_CXX_COMPILER=$LLVMBIN/clang++ \
		"-DCMAKE_CXX_FLAGS=-g -fno-omit-frame-pointer $san" \
		"-DCMAKE_EXE_LINKER_FLAGS=$san"
	ninja -C "$W/lcc-opt-san" -j$jobs
	# abort, so that the tests that expect lcc-opt to fail (with not)
	# fail on a report too
	ASAN_OPTIONS=abort_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:abort_on_error=1:print_stacktrace=1 \
		ninja -C "$W/lcc-opt-san" check-lcc-opt ;;
lcc)
	versions
	[ -x "$W/lcc-opt/bin/lcc-opt" ] ||
		{ echo "$0: no $W/lcc-opt/bin/lcc-opt; run $0 lit first" 1>&2; exit 1; }
	buildlcc "$W/lcc"
	# lcc's driver and run-llvm.sh lower lcc's .mlir with mlir-opt; the
	# lcc of lcc-raise and run-llvm.sh with MLIRBIN get this one instead
	mkdir -p "$W/bin"
	for t in mlir-translate opt clang; do
		ln -sf $LLVMBIN/$t "$W/bin/$t"
	done
	cat >"$W/bin/mlir-opt" <<'EOF'
#!/bin/sh
# Made by .github/scripts/lcc-opt-check.sh.  lcc's mlir drivers and
# src/run-llvm.sh lower lcc's .mlir with mlir-opt --convert-arith-to-llvm
# --convert-cf-to-llvm --convert-func-to-llvm --reconcile-unrealized-casts
# --emit-bytecode in -o out; this raises it to linalg with lcc-opt first
# and lowers it with lcc-opt's lcc-lower, which does the same.  With
# $LCC_OPT_TRACE set it prints the lcc-opt command.
in= out=
while [ $# -gt 0 ]; do
	case $1 in
	--convert-arith-to-llvm|--convert-cf-to-llvm|--convert-func-to-llvm|--reconcile-unrealized-casts|--emit-bytecode)
		shift ;;
	-o)	out=$2; shift 2 ;;
	-*)	echo "$0: unexpected option $1" 1>&2; exit 2 ;;
	*)	in=$1; shift ;;
	esac
done
lccopt=`dirname "$0"`/../lcc-opt/bin/lcc-opt
[ -z "$LCC_OPT_TRACE" ] ||
	echo "$lccopt --lcc-to-linalg --lcc-lower --emit-bytecode $in -o $out" 1>&2
exec "$lccopt" --lcc-to-linalg --lcc-lower --emit-bytecode "$in" -o "$out"
EOF
	chmod +x "$W/bin/mlir-opt"
	buildlcc "$W/lcc-raise" "CFLAGS=-g -DLLVMBIN=\\\"$W/bin\\\""
	# the driver runs work/bin/mlir-opt, which runs lcc-opt
	rm -rf "$W/lcc-raise/check"
	mkdir -p "$W/lcc-raise/check"
	LCC_OPT_TRACE=1 "$W/lcc-raise/lcc" -Wo-lccdir="$W/lcc-raise" -v -c \
		-o "$W/lcc-raise/check/convfix.o" lcc-opt/kernels/convfix.c \
		>"$W/lcc-raise/check/v" 2>&1 || { cat "$W/lcc-raise/check/v"; exit 1; }
	cat "$W/lcc-raise/check/v"
	if grep -q "lcc-as $W/bin " "$W/lcc-raise/check/v" &&
	   grep -q "/lcc-opt/bin/lcc-opt --lcc-to-linalg --lcc-lower " "$W/lcc-raise/check/v"; then
		echo "$W/lcc-raise/lcc lowers through lcc-opt"
	else
		echo "$0: $W/lcc-raise/lcc does not run lcc-opt" 1>&2
		exit 1
	fi ;;
kernels)
	versions
	lcc-opt/kernels/check.sh -o "$W/kernels" "$W/lcc" "$W/lcc-opt/bin/lcc-opt" "$T" ;;
suite)
	MLIRBIN=$W/bin src/run-llvm.sh -q -t "$T" -b "$W/lcc-raise" "$@" ;;
all)
	[ -n "$S" ] || { echo "usage: $0 all $T work suites" 1>&2; exit 2; }
	st=0
	"$self" lit "$T" "$W" || st=1
	"$self" sanitizers "$T" "$W" || st=1
	"$self" lcc "$T" "$W" || exit 1
	"$self" kernels "$T" "$W" || st=1
	"$self" suite "$T" "$W" lcc || st=1
	"$self" suite "$T" "$W" unit || st=1
	DRVOPT=-O2 "$self" suite "$T" "$W" -o "$W/lcc-raise/unit-drvopt" unit || st=1
	"$self" suite "$T" "$W" c-testsuite "$S/c-testsuite" || st=1
	"$self" suite "$T" "$W" torture "$S/llvm-test-suite/SingleSource/Regression/C/gcc-c-torture/execute" || st=1
	"$self" suite "$T" "$W" llvm-bench "$S/llvm-test-suite" || st=1
	exit $st ;;
*)
	echo "usage: $0 deps|lit|sanitizers|lcc|kernels|suite|all x86_64-linux|aarch64-linux [work [args]]" 1>&2
	exit 2 ;;
esac
