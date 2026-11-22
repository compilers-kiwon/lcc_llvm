#!/bin/sh
# run-llvm.sh - compile, run and check test programs with -target=llvm/x86_64-linux
#
# usage: src/run-llvm.sh [-b builddir] [-o outdir] [-t target] [-q] suite [dir] [name ...]
#
# target is that of the lcc in builddir: x86_64-linux (the default),
# aarch64-linux, riscv64-linux, armhf-linux, riscv32-linux or cortexm3-eabi, whose
# programs run with qemu (cortexm3-eabi on qemu-system-arm with
# semihosting; its test drivers are built by src/cm3cc.sh); $TARGET
# too.  The lcc may be a program of that target itself (src/selfhost.sh -x).
#
# suites:
#   lcc               tst/*.c; input tst/NAME.0, stdout compared with NAME.1bk
#                     and compiler messages with NAME.2bk, taken from
#                     llvm/TARGET/tst if there, else llvm/x86_64-linux/tst,
#                     else x86/linux/tst;
#                     without NAME.1bk only the messages are compared
#   unit              tst/llvm/NAME.c compiled by lcc and linked with
#                     tst/llvm/NAME-drv.c compiled by clang ($DRVCC); the driver
#                     checks the results and must exit with 0, and each data
#                     object of NAME.o must have the size of the object
#                     r_NAME that the driver defines, if there is one; lcc's
#                     messages are compared with tst/llvm/NAME.2bk if there
#   c-testsuite DIR   DIR/tests/single-exec/*.c tagged c89; stdout+stderr
#                     compared with NAME.c.expected, exit status must be 0
#   torture DIR       DIR/*.c and DIR/ieee/*.c of gcc.c-torture/execute;
#                     exit status must be 0
#   llvm-bench DIR    DIR/SingleSource/Benchmarks/{Stanford,McGill,Shootout,
#                     Dhrystone}/*.c of the LLVM test-suite; stdout+stderr and
#                     "exit N" compared with NAME.reference_output
#   pragma            tst/pragma with lcc -Wf-pragma=knpu -Wf-pragma=npu2:
#                     messages of errNAME.c and errNAME.i compared with
#                     errNAME.2bk, of ref.c with -A -A with ref.2bk (named
#                     objects are used); attr.c compiled and run, stdout compared
#                     with attr.1bk, its IR the same as without the options;
#                     with MLIR, the attributes of attr.c, also with
#                     -Wf-llvmfunc, and of a generated #pragma longer than
#                     rcc's buffer compared with attr.abk and what they must
#                     be, attr.c read by mlir-opt, also as bytecode, and
#                     lcc-opt if $LCCOPT names it, its names of objects
#                     defined, also after --symbol-dce, and checked by
#                     src/mlir-lint.sh; bad -pragma= options must fail
#
# Programs of the external suites are used only if they are C89: no GNU
# keywords, GCC predefined macros or #pragma, also in the files they
# include from their directory, accepted by clang and gcc in
# strict C89 mode, no implicitly declared function that may not return int,
# and not on the exclusion list below.  The test files are not copied into
# this repository; pass the directory of a checkout instead.
#
# $LCC replaces the compiler, e.g. LCC=gcc LCCLIBS=-lm to check the suites
# themselves; compiler messages are then not compared, and tests that check
# only messages are skipped.  $LCCLIBS goes after the source file.
# $LCCFLAGS are more options for lcc, e.g. LCCFLAGS=-Wa-O2 to have clang
# optimize what lcc emits.  $DRVCC compiles the unit test drivers instead
# of clang, e.g. DRVCC=gcc to link lcc's code with gcc's; for another
# target, clang and gcc stand for its cross compilers.  $DRVOPT is their
# optimization (default -O0); optimized drivers rely on what the ABI
# promises of lcc's code, as extended arguments and results.
#
# $OESYSROOT is the target sysroot of a Yocto build, for the lcc of its
# toolchain (meta-lcc's lcc-cross), which $LCC then gives: its messages
# and IR are checked as those of lcc itself, the programs run with
# qemu on that sysroot, and $DRVCC must be the toolchain's gcc command.
# meta-lcc/scripts/check-tests.sh sets all three.
#
# With lcc itself, each program is also compiled with -S and the LLVM IR
# checked with opt -passes=verify ($OPT), since clang does not verify
# the .ll it is given.  An lcc built with etc/mlir-x86_64-linux.c emits
# MLIR instead (-target=mlir/x86_64-linux): the .mlir is lowered to LLVM
# IR with mlir-opt and mlir-translate as its driver does, and checked
# with opt, all three from $MLIRBIN (default /usr/lib/llvm-23/bin).
#
# Results are PASS, FAIL (wrong output or exit status), CFAIL (does not
# compile, or the IR does not verify), TIMEOUT and SKIP.  The exit status is 0 only if every test that
# is not skipped passes.

cd `dirname $0`/.. || exit 1

BUILDDIR=${BUILDDIR-.}
TARGET=${TARGET-x86_64-linux}
quiet=0
outdir=
while [ $# -gt 0 ]; do
	case "$1" in
	-b)	BUILDDIR=$2; shift 2 ;;
	-o)	outdir=$2; shift 2 ;;
	-t)	TARGET=$2; shift 2 ;;
	-q)	quiet=1; shift ;;
	-*)	echo "$0: unknown option $1" 1>&2; exit 2 ;;
	*)	break ;;
	esac
done
suite=$1
[ $# -gt 0 ] && shift
case "$suite" in
lcc|unit|pragma) dir= ;;
c-testsuite|torture|llvm-bench)
	dir=$1
	if [ -z "$dir" -o ! -d "$dir" ]; then
		echo "$0: $suite needs the directory of the suite" 1>&2; exit 2
	fi
	shift ;;
*)	sed -n '2,/^$/s/^# \{0,1\}//p' $0 1>&2; exit 2 ;;
esac

if [ -n "$OESYSROOT" ]; then
	ownlcc=1
elif [ -n "$LCC" ]; then
	ownlcc=0
else
	ownlcc=1
	LCC="$BUILDDIR/lcc -Wo-lccdir=$BUILDDIR -Iinclude/llvm/$TARGET"
fi
LCC="$LCC $LCCFLAGS"
DRVCC=${DRVCC-clang}
# RUN runs a program of the target, NM lists the symbols of its objects.
# On a target without an OS, REFCC (gcc) checks first that the external
# tests can be compiled and the benchmarks run there at all, and these
# are built with SMALL, LLVM's flag for small targets.
NOOS= REFCC= SMALL=
if [ -n "$OESYSROOT" ]; then
	case $TARGET in
	armhf-linux)	Q=arm ;;
	x86_64-linux|aarch64-linux|riscv64-linux|riscv32-linux) Q=${TARGET%-linux} ;;
	*)	echo "$0: $TARGET is not a Yocto target" 1>&2; exit 2 ;;
	esac
	# the sysroot has no ld.so.cache: LD_LIBRARY_PATH keeps the target's
	# loader from finding the host's libraries
	RUN="qemu-$Q -L $OESYSROOT -E LD_LIBRARY_PATH=/usr/lib:/lib" NM=llvm-nm-18
	QEMU_LD_PREFIX=$OESYSROOT
	export QEMU_LD_PREFIX
else
	case $TARGET in
	x86_64-linux)
		RUN= NM=nm ;;
	aarch64-linux)
		RUN="qemu-aarch64 -L /usr/aarch64-linux-gnu" NM=llvm-nm-18
		# an lcc built by itself for AArch64 runs with binfmt_misc
		QEMU_LD_PREFIX=/usr/aarch64-linux-gnu
		export QEMU_LD_PREFIX
		case "$DRVCC" in
		clang)	DRVCC="clang --target=aarch64-linux-gnu -fuse-ld=lld" ;;
		gcc)	DRVCC=aarch64-linux-gnu-gcc ;;
		esac ;;
	riscv64-linux)
		RUN="qemu-riscv64 -L /usr/riscv64-linux-gnu" NM=llvm-nm-18
		QEMU_LD_PREFIX=/usr/riscv64-linux-gnu
		export QEMU_LD_PREFIX
		case "$DRVCC" in
		clang)	DRVCC="clang --target=riscv64-linux-gnu -fuse-ld=lld" ;;
		gcc)	DRVCC=riscv64-linux-gnu-gcc ;;
		esac ;;
	armhf-linux)
		RUN="qemu-arm -L /usr/arm-linux-gnueabihf" NM=llvm-nm-18
		QEMU_LD_PREFIX=/usr/arm-linux-gnueabihf
		export QEMU_LD_PREFIX
		# lcc's off_t has 64 bits and time_t 32 (see README); clang's off_t
		# has 32 by default, Ubuntu's gcc's time_t 64 (-U_TIME_BITS also
		# removes _FILE_OFFSET_BITS)
		case "$DRVCC" in
		clang)	DRVCC="clang --target=arm-linux-gnueabihf -D_FILE_OFFSET_BITS=64 -fuse-ld=lld" ;;
		gcc)	DRVCC="arm-linux-gnueabihf-gcc -U_TIME_BITS -D_FILE_OFFSET_BITS=64" ;;
		esac ;;
	riscv32-linux)	# BUILDDIR/riscv32 is the toolchain, see etc/llvm-riscv32-linux.c
		R32=`cd $BUILDDIR/riscv32 && pwd`
		RUN="qemu-riscv32 -L $R32/sysroot" NM=llvm-nm-18
		QEMU_LD_PREFIX=$R32/sysroot
		export QEMU_LD_PREFIX
		case "$DRVCC" in
		clang)	DRVCC="clang --target=riscv32-unknown-linux-gnu -march=rv32gc -mabi=ilp32d --gcc-toolchain=$R32 --sysroot=$R32/sysroot -fuse-ld=lld" ;;
		gcc)	DRVCC="$R32/bin/riscv32-unknown-linux-gnu-gcc -fPIE -pie -std=gnu89" ;;
		esac ;;
	cortexm3-eabi)	# no OS: newlib and semihosting on qemu's mps2-an385 board
		RUN="qemu-system-arm -M mps2-an385 -nographic -monitor none -serial none -semihosting-config enable=on,target=native -kernel"
		NM=llvm-nm-18
		case "$DRVCC" in
		clang|gcc) DRVCC="src/cm3cc.sh $BUILDDIR $DRVCC" ;;
		esac
		NOOS=1 REFCC="src/cm3cc.sh $BUILDDIR gcc" SMALL=-DSMALL_PROBLEM_SIZE ;;
	*)	echo "$0: unknown target $TARGET" 1>&2; exit 2 ;;
	esac
fi
command -v $NM >/dev/null || NM=/usr/lib/llvm-18/bin/llvm-nm
OPT=${OPT-opt}
MLIRBIN=${MLIRBIN-/usr/lib/llvm-23/bin}
T=${outdir:-$BUILDDIR/llvm/$TARGET/tst/$suite}
mkdir -p $T || exit 1

# strict C89 check for the external suites
GNUKW='__attribute__|__builtin|__extension__|__typeof|typeof|__asm|asm *\(|__label__|__inline|__alignof|__complex__|_Complex|__int128|__restrict|__volatile__|__const__'
CLANG89="-std=c89 -pedantic-errors -Wno-strict-prototypes -Wno-deprecated-non-prototype -Wno-implicit-function-declaration -Wno-implicit-int -fsyntax-only"
GCC89="-std=c89 -pedantic-errors -fsyntax-only"

# excluded - accepted by both compilers but not strictly conforming C89
excluded() {
	case "$1" in
	fp-cmp-[123].c)	echo "signal handler of an incompatible type" ;;
	pr15262-1.c)	echo "implicitly declared malloc assigned to a pointer" ;;
	*)		return 1 ;;
	esac
	return 0
}

# localsrc file - file and the files it includes with "..." from its
# directory, directly or not; some files include themselves
localsrc() {
	dir=`dirname $1` todo=$1 seen=
	while [ -n "$todo" ]; do
		set -- $todo; f=$1; shift; todo="$*"
		case " $seen " in *" $f "*) continue ;; esac
		seen="$seen $f"
		for i in `sed -n 's/^[ 	]*#[ 	]*include[ 	]*"\([^"]*\)".*/\1/p' $f`; do
			[ -f "$dir/$i" ] && todo="$todo $dir/$i"
		done
	done
	echo $seen
}

isc89() {
	excluded `basename $1` >/dev/null && return 1
	srcs=`localsrc $1`
	grep -E "$GNUKW" $srcs >/dev/null && return 1
	if grep -ohE '\b__[A-Z][A-Z0-9_]*__\b' $srcs |
	   grep -vE '^__(STDC|FILE|LINE|DATE|TIME)__$' >/dev/null; then
		return 1
	fi
	grep -E '^[ 	]*#[ 	]*pragma' $srcs >/dev/null && return 1
	grep -w va_copy $srcs >/dev/null && return 1	# C99
	clang $CLANG89 -I`dirname $1` "$1" >/dev/null 2>&1 || return 1
	gcc $GCC89 -I`dirname $1` "$1" >/dev/null 2>&1 || return 1
	# calls through an implicit int declaration are fine only if the
	# function really returns int (or void and the value is not used)
	w=`clang -std=c89 -fsyntax-only -Wimplicit-function-declaration -I\`dirname $1\` "$1" 2>&1`
	if echo "$w" | grep 'implicitly declaring library function' |
	   grep -vE "with type '(int|void)[ (]" >/dev/null; then
		return 1
	fi
	fns=`echo "$w" | sed -n "s/.*implicit declaration of function '\([A-Za-z_0-9]*\)'.*/\1/p" | sort -u`
	if [ -n "$fns" ]; then	# defined in the file, or of unknown type?
		clang -std=c89 -w -c -o $T/isc89.o -I`dirname $1` "$1" 2>/dev/null || return 1
		undef=`nm -u $T/isc89.o`
		for fn in $fns; do
			echo "$undef" | grep -w "$fn" >/dev/null && return 1
		done
	fi
	return 0
}

# fpcmp a b tol - like diff, but numbers may differ by a relative tol
fpcmp() {
	cmp -s "$1" "$2" && return 0
	[ -z "$3" ] && return 1
	awk -v tol="$3" -v other="$2" '
	function isnum(s) { return s ~ /^[-+]?([0-9]+\.?[0-9]*|\.[0-9]+)([eE][-+]?[0-9]+)?$/ }
	{
		if ((getline line < other) <= 0) exit 1
		n = split($0, a); m = split(line, b)
		if (n != m) exit 1
		for (i = 1; i <= n; i++) {
			if (a[i] == b[i]) continue
			if (!isnum(a[i]) || !isnum(b[i])) exit 1
			d = a[i] - b[i]; if (d < 0) d = -d
			r = b[i] < 0 ? -b[i] : b[i]
			if (d > tol * r && d > tol) exit 1
		}
	}
	END { if ((getline line < other) > 0) exit 1 }' "$1"
}

npass=0 nfail=0 ncfail=0 ntimeout=0 nskip=0
report() {	# status name [reason]
	case "$1" in
	PASS)	npass=`expr $npass + 1` ;;
	FAIL)	nfail=`expr $nfail + 1` ;;
	CFAIL)	ncfail=`expr $ncfail + 1` ;;
	TIMEOUT) ntimeout=`expr $ntimeout + 1` ;;
	SKIP)	nskip=`expr $nskip + 1` ;;
	esac
	if [ $quiet = 0 -o "$1" != PASS -a "$1" != SKIP ]; then
		printf '%-8s%s%s\n' "$1" "$2" "${3:+  ($3)}"
	fi
}

# compile name src: build $T/name; report CFAIL and return 1 on failure
compile() {
	errout=
	[ $ownlcc = 1 -a "$suite" = lcc ] && errout="-Wf-errout=$T/$1.2"
	rm -f $T/$1.2
	if ! timeout 300 $LCC $errout -o $T/$1 $2 $LCCLIBS >$T/$1.cerr 2>&1; then
		[ -r $T/$1.2 ] && cat $T/$1.2 >>$T/$1.cerr
		report CFAIL $1 "`grep -v -e 'Unknown preprocessor control' -e ': warning: ' $T/$1.cerr | head -1`"
		return 1
	fi
	verify $1 "$2"
}

# verify name src: with lcc itself, check the IR of src; report CFAIL
# and return 1 if it does not verify
verify() {
	[ $ownlcc = 1 ] || return 0
	rm -f $T/$1.ll $T/$1.mlir	# not to check what an earlier run left
	if ! timeout 300 $LCC -S -o $T/$1.ll $2 >/dev/null 2>&1 ||
	   ! verifyir $T/$1 >$T/$1.verr 2>&1; then
		report CFAIL $1 "IR does not verify: `head -1 $T/$1.verr`"
		return 1
	fi
	return 0
}

# verifyir base: verify base.ll from lcc -S; if that is MLIR (a module op
# after any type aliases), move it to base.mlir and verify what it lowers to
verifyir() {
	case "`sed -n '/^!/!{p;q;}' $1.ll`" in
	module*)
		mv $1.ll $1.mlir &&
		$MLIRBIN/mlir-opt --convert-arith-to-llvm --convert-cf-to-llvm \
			--convert-func-to-llvm --reconcile-unrealized-casts \
			--emit-bytecode $1.mlir -o $1.mlirbc &&
		$MLIRBIN/mlir-translate --mlir-to-llvmir $1.mlirbc -o $1.ll &&
		$MLIRBIN/opt -passes=verify -disable-output $1.ll ;;
	*)	$OPT -passes=verify -disable-output $1.ll ;;
	esac
}

# sizes name: the data objects of $T/name.o whose copies r_NAME the unit
# test driver defines have their sizes; print those that do not
sizes() {
	LC_ALL=C $NM -S $T/$1.o 2>/dev/null |
	awk 'NF == 4 && $3 ~ /^[bBdDrR]$/ { print $4, $2 }' | LC_ALL=C sort >$T/$1.sz1
	LC_ALL=C $NM -S $T/$1 2>/dev/null |
	awk 'NF == 4 && $3 ~ /^[bBdDrR]$/ && $4 ~ /^r_/ { print substr($4, 3), $2 }' |
	LC_ALL=C sort >$T/$1.sz2
	LC_ALL=C join $T/$1.sz1 $T/$1.sz2 | awk '$2 != $3 {
		printf "size of %s is 0x%s, not 0x%s\n", $1, $2, $3; bad = 1 }
	END { exit bad }'
}

# refcompiles name src: without an OS, whether gcc compiles src for the
# target (some tests need POSIX headers); report SKIP if it does not
refcompiles() {
	[ -z "$NOOS" ] && return 0
	$REFCC -std=gnu89 -w -c -o $T/$1.refo -I`dirname $2` $2 >/dev/null 2>&1 && return 0
	report SKIP $1 "gcc does not compile it for this target"
	return 1
}

# benchrun name prog: run the benchmark prog; its output and "exit N"
# go to $T/name.out; set $status
benchrun() {
	timeout 300 $RUN $2 </dev/null >$T/$1.out 2>&1
	status=$?
	[ $status = 124 ] && return
	if [ -s $T/$1.out ] && [ -n "`tail -c 1 $T/$1.out`" ]; then
		echo >>$T/$1.out
	fi
	echo "exit $status" >>$T/$1.out
}

# refruns name src ref tol: without an OS, whether the benchmark src
# built by gcc runs as ref says on the target (memory is small); report
# SKIP if it does not
refruns() {
	[ -z "$NOOS" ] && return 0
	if $REFCC $SMALL -std=gnu89 -w -O1 -o $T/$1.ref $2 -lm >/dev/null 2>&1; then
		benchrun $1.ref $T/$1.ref
		[ $status != 124 ] && fpcmp $T/$1.ref.out $3 $4 && return 0
	fi
	report SKIP $1 "gcc's build does not run on this target either"
	return 1
}

# execute name input seconds: run $T/name; set $status
execute() {
	timeout $3 $RUN $T/$1 <$2 >$T/$1.out 2>$T/$1.err
	status=$?
}

# attrs file: the attributes NS.op and NS.args of the functions of the
# MLIR file, a line "name attributes" for each, without the llvm.linkage
# of a static func.func
attrs() {
	sed -n 's/^[a-z]*\.func \(internal \)\{0,1\}@"\([^"]*\)"(.* attributes {\(.*\.op = .*\)} {$/\2 \3/p' "$1" |
	sed 's/^\([^ ]*\) llvm\.linkage = #llvm\.linkage<internal>, /\1 /'
}

# tollvm base: base.ll from base.mlir, as the driver lowers it
tollvm() {
	$MLIRBIN/mlir-opt --convert-arith-to-llvm --convert-cf-to-llvm \
		--convert-func-to-llvm --reconcile-unrealized-casts \
		--emit-bytecode $1.mlir -o $1.mlirbc &&
	$MLIRBIN/mlir-translate --mlir-to-llvmir $1.mlirbc -o $1.ll
}

# isattrs name file: report whether file is the attributes $T/name.abk
isattrs() {
	if cmp -s $2 $T/$1.abk; then report PASS $1
	else report FAIL $1 "attributes differ from $2"; fi
}

# pragmattr: tst/pragma/attr.c, which lcc must compile without messages
pragmattr() {
	src=tst/pragma/attr.c
	rm -f $T/attr.ll $T/attr.mlir $T/attr-off.ll $T/attr-off.mlir
	if ! timeout 300 $LCC $P -S -o $T/attr.ll $src >$T/attr.cerr 2>&1 ||
	   [ -s $T/attr.cerr ]; then
		report CFAIL attr "`head -1 $T/attr.cerr`"
		return
	fi
	timeout 300 $LCC -S -o $T/attr-off.ll $src >$T/attr-off.cerr 2>&1
	case "`sed -n '/^!/!{p;q;}' $T/attr.ll`" in
	module*)
		mv $T/attr.ll $T/attr.mlir
		mv $T/attr-off.ll $T/attr-off.mlir
		attrs $T/attr.mlir >$T/attr.abk
		isattrs attr tst/pragma/attr.abk
		timeout 300 $LCC $P -Wf-llvmfunc -S -o $T/attr-llvmfunc.mlir $src \
			>$T/attr-llvmfunc.cerr 2>&1
		attrs $T/attr-llvmfunc.mlir >$T/attr-llvmfunc.abk
		isattrs attr-llvmfunc tst/pragma/attr.abk
		if ! $MLIRBIN/mlir-opt $T/attr.mlir -o $T/attr-opt.mlir >$T/attr-opt.err 2>&1; then
			report FAIL attr-mlir-opt "`head -1 $T/attr-opt.err`"
		elif $MLIRBIN/mlir-opt --emit-bytecode $T/attr.mlir -o $T/attr.mlirbc &&
		     $MLIRBIN/mlir-opt $T/attr.mlirbc -o $T/attr-bc.mlir &&
		     cmp -s $T/attr-opt.mlir $T/attr-bc.mlir; then
			report PASS attr-mlir-opt
		else
			report FAIL attr-mlir-opt "not the same through bytecode"
		fi
		if [ -n "$LCCOPT" ]; then
			if $LCCOPT $T/attr.mlir -o $T/attr-lccopt.mlir >$T/attr-lccopt.err 2>&1 &&
			   [ "`grep -c '\.op = ' $T/attr-lccopt.mlir`" = "`grep -c '\.op = ' $T/attr-opt.mlir`" ]; then
				report PASS attr-lcc-opt
			else
				report FAIL attr-lcc-opt "`head -1 $T/attr-lccopt.err`"
			fi
		fi
		# the objects the pragmas name are defined, also after symbol-dce
		$MLIRBIN/mlir-opt --symbol-dce $T/attr.mlir -o $T/attr-dce.mlir 2>/dev/null
		why=
		for name in `sed 's/^[^ ]* //' $T/attr.abk | grep -o '@"[^"]*"' | sed 's/^@"//; s/"$//' | sort -u`; do
			grep "^llvm\.mlir\.global [a-z ]*@\"$name\"(" $T/attr.mlir >/dev/null ||
				why="$why $name"
			grep "llvm\.mlir\.global [a-z ]*@$name(" $T/attr-dce.mlir >/dev/null ||
				why="$why $name(symbol-dce)"
		done
		[ "`grep -c '\.op = ' $T/attr-dce.mlir`" = "`grep -c '\.op = ' $T/attr-opt.mlir`" ] ||
			why="$why attributes(symbol-dce)"
		if [ -z "$why" ]; then report PASS attr-names
		else report FAIL attr-names "not defined:$why"; fi
		if src/mlir-lint.sh $T/attr.mlir >$T/attr.lint 2>&1; then report PASS attr-lint
		else report FAIL attr-lint "`head -1 $T/attr.lint`"; fi
		# the LLVM IR of the driver is the same as without the options
		if tollvm $T/attr && tollvm $T/attr-off && cmp -s $T/attr.ll $T/attr-off.ll &&
		   ! grep '\.op = ' $T/attr-off.mlir >/dev/null; then
			report PASS attr-ll
		else
			report FAIL attr-ll "not the same LLVM IR as without -Wf-pragma"
		fi ;;
	*)	if cmp -s $T/attr.ll $T/attr-off.ll; then report PASS attr-ll
		else report FAIL attr-ll "not the same LLVM IR as without -Wf-pragma"; fi ;;
	esac
	if ! timeout 300 $LCC $P -o $T/attr $src $LCCLIBS >$T/attr-run.cerr 2>&1; then
		report CFAIL attr-run "`head -1 $T/attr-run.cerr`"
		return
	fi
	execute attr /dev/null 60
	if [ $status = 124 ]; then report TIMEOUT attr-run
	elif [ $status != 0 ]; then report FAIL attr-run "exit status $status"
	elif ! cmp -s tst/pragma/attr.1bk $T/attr.out; then
		report FAIL attr-run "stdout differs from tst/pragma/attr.1bk"
	else report PASS attr-run; fi
}

# pragmalong: a #pragma longer than rcc's input buffer
pragmalong() {
	awk 'BEGIN {
		printf "void f(void)\n{\n#pragma knpu longline "
		for (i = 0; i < 600; i++) printf "k"
		printf "=-1 big=("
		for (i = 0; i < 3000; i++) printf "%s%d", i ? "," : "", i
		printf ") s=\""
		for (i = 0; i < 5000; i++) printf "x"
		printf "\\n\"\n}\n"
	}' >$T/long.c
	awk 'BEGIN {
		printf "f knpu.op = \"longline\", knpu.args = {"
		for (i = 0; i < 600; i++) printf "k"
		printf " = -1 : i64, big = array<i64: "
		for (i = 0; i < 3000; i++) printf "%s%d", i ? ", " : "", i
		printf ">, s = \""
		for (i = 0; i < 5000; i++) printf "x"
		printf "\\0A\"}\n"
	}' >$T/long.ok
	rm -f $T/long.ll $T/long-off.ll
	if ! timeout 300 $LCC $P -S -o $T/long.ll $T/long.c >$T/long.cerr 2>&1 ||
	   [ -s $T/long.cerr ]; then
		report CFAIL long "`head -1 $T/long.cerr`"
		return
	fi
	case "`sed -n '/^!/!{p;q;}' $T/long.ll`" in
	module*)
		attrs $T/long.ll >$T/long.abk
		isattrs long $T/long.ok ;;
	*)	timeout 300 $LCC -S -o $T/long-off.ll $T/long.c >/dev/null 2>&1
		if cmp -s $T/long.ll $T/long-off.ll; then report PASS long
		else report FAIL long "not the same LLVM IR as without -Wf-pragma"; fi ;;
	esac
}

# pragmaoption: rcc rejects -pragma= without a name space
pragmaoption() {
	why=
	for o in ref '' int 1a a-b; do
		if timeout 60 $LCC -Wf-pragma=$o -S -o $T/option.ll tst/pragma/attr.c >$T/option.cerr 2>&1; then
			why="$why -pragma=$o accepted"
		elif ! grep "invalid pragma name space \`$o'" $T/option.cerr >/dev/null; then
			why="$why -pragma=$o: `head -1 $T/option.cerr`"
		fi
	done
	if [ -z "$why" ]; then report PASS option
	else report FAIL option "$why"; fi
}

selected() {	# name: is it among the names given on the command line?
	[ -z "$names" ] && return 0
	for s in $names; do
		[ "$s" = "$1" -o "$s.c" = "$1" ] && return 0
	done
	return 1
}
names="$*"

case "$suite" in
lcc)
	for src in tst/*.c; do
		n=`basename $src .c`; selected $n || continue
		out1=llvm/$TARGET/tst/$n.1bk
		[ -r $out1 ] || out1=llvm/x86_64-linux/tst/$n.1bk
		[ -r $out1 ] || out1=x86/linux/tst/$n.1bk
		out2=llvm/$TARGET/tst/$n.2bk
		[ -r $out2 ] || out2=llvm/x86_64-linux/tst/$n.2bk
		[ -r $out2 ] || out2=x86/linux/tst/$n.2bk
		if [ ! -r $out1 ]; then		# compile only, compare messages
			if [ $ownlcc = 0 ]; then report SKIP $n "checks only lcc's messages"; continue; fi
			timeout 300 $LCC -Wf-errout=$T/$n.2 -S -o $T/$n.ll $src >$T/$n.cerr 2>&1
			if cmp -s $out2 $T/$n.2; then report PASS $n
			else report FAIL $n "messages differ from $out2"; fi
			continue
		fi
		compile $n $src || continue
		in=tst/$n.0; [ -r $in ] || in=/dev/null
		execute $n $in 60
		if [ $status = 124 ]; then report TIMEOUT $n; continue; fi
		why=
		cmp -s $out1 $T/$n.out || why="stdout differs from $out1"
		if [ $ownlcc = 1 -a -r $out2 ] && ! cmp -s $out2 $T/$n.2; then
			why="${why:+$why; }messages differ from $out2"
		fi
		if [ -n "$why" ]; then report FAIL $n "$why"; else report PASS $n; fi
	done ;;
unit)
	for src in tst/llvm/*.c; do
		case $src in *-drv.c) continue ;; esac
		n=`basename $src .c`; selected $n || continue
		if ! timeout 300 $LCC -c -o $T/$n.o $src >$T/$n.cerr 2>&1; then
			report CFAIL $n "`grep -v -e 'Unknown preprocessor control' -e ': warning: ' $T/$n.cerr | head -1`"
			continue
		fi
		if [ $ownlcc = 1 -a -r tst/llvm/$n.2bk ] && ! cmp -s tst/llvm/$n.2bk $T/$n.cerr; then
			report FAIL $n "messages differ from tst/llvm/$n.2bk"
			continue
		fi
		verify $n $src || continue
		if ! $DRVCC ${DRVOPT--O0} -fwrapv -ffp-contract=off -w -o $T/$n tst/llvm/$n-drv.c $T/$n.o $LCCLIBS -lm >$T/$n.lerr 2>&1; then
			report CFAIL $n "driver: `head -1 $T/$n.lerr`"
			continue
		fi
		if ! sizes $n >$T/$n.szerr; then
			report FAIL $n "`head -1 $T/$n.szerr`"
			continue
		fi
		execute $n /dev/null 60
		if [ $status = 124 ]; then report TIMEOUT $n
		elif [ $status != 0 ]; then report FAIL $n "`tail -1 $T/$n.out`"
		else report PASS $n "`tail -1 $T/$n.out`"; fi
	done ;;
c-testsuite)
	for src in $dir/tests/single-exec/*.c; do
		n=`basename $src .c`; selected $n || continue
		grep -w c89 $src.tags >/dev/null 2>&1 || continue
		isc89 $src || continue
		refcompiles $n $src || continue
		compile $n $src || continue
		timeout 60 $RUN $T/$n </dev/null >$T/$n.out 2>&1
		status=$?
		if [ $status = 124 ]; then report TIMEOUT $n
		elif [ $status != 0 ]; then report FAIL $n "exit status $status"
		elif ! cmp -s $src.expected $T/$n.out; then report FAIL $n "output differs"
		else report PASS $n; fi
	done ;;
torture)
	for src in $dir/*.c $dir/ieee/*.c; do
		[ -r $src ] || continue
		n=`basename $src .c`; selected $n || continue
		isc89 $src || continue
		refcompiles $n $src || continue
		compile $n $src || continue
		execute $n /dev/null 60
		if [ $status = 124 ]; then report TIMEOUT $n
		elif [ $status != 0 ]; then report FAIL $n "exit status $status"
		else report PASS $n; fi
	done ;;
llvm-bench)
	for d in Stanford McGill Shootout Dhrystone; do
		tol=
		case $d in Stanford|McGill) tol=0.001 ;; esac
		for src in $dir/SingleSource/Benchmarks/$d/*.c; do
			n=`basename $src .c`; selected $n || continue
			ref=`dirname $src`/$n.reference_output
			[ -r $ref ] || continue
			[ -n "$SMALL" -a -r $ref.small ] && ref=$ref.small
			isc89 $src || continue
			refruns $n $src $ref $tol || continue
			compile $n "$SMALL $src" || continue
			benchrun $n $T/$n
			if [ $status = 124 ]; then report TIMEOUT $n; continue; fi
			if fpcmp $T/$n.out $ref $tol; then report PASS $n
			else report FAIL $n "output differs from `basename $ref`"; fi
		done
	done ;;
pragma)
	if [ $ownlcc = 0 ]; then
		report SKIP pragma "checks only lcc's messages and IR"
	else
		P="-Wf-pragma=knpu -Wf-pragma=npu2"
		for src in tst/pragma/err*.c tst/pragma/err*.i; do
			n=`basename $src`; n=${n%.*}; selected $n || continue
			timeout 300 $LCC $P -Wf-e1000 -S -o $T/$n.ll $src >$T/$n.cerr 2>&1
			if cmp -s tst/pragma/$n.2bk $T/$n.cerr; then report PASS $n
			else report FAIL $n "messages differ from tst/pragma/$n.2bk"; fi
		done
		if selected ref; then
			timeout 300 $LCC $P -A -A -S -o $T/ref.ll tst/pragma/ref.c >$T/ref.cerr 2>&1
			if cmp -s tst/pragma/ref.2bk $T/ref.cerr; then report PASS ref
			else report FAIL ref "messages differ from tst/pragma/ref.2bk"; fi
		fi
		selected attr && pragmattr
		selected long && pragmalong
		selected option && pragmaoption
	fi ;;
esac

total=`expr $npass + $nfail + $ncfail + $ntimeout`
echo "$suite: $total tests, $npass passed, $nfail failed, $ncfail did not compile, $ntimeout timed out, $nskip skipped"
[ $total = $npass ]
