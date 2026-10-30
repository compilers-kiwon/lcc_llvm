#!/bin/sh
# run-llvm.sh - compile, run and check test programs with -target=llvm/x86_64-linux
#
# usage: src/run-llvm.sh [-b builddir] [-o outdir] [-q] suite [dir] [name ...]
#
# suites:
#   lcc               tst/*.c; input tst/NAME.0, stdout compared with NAME.1bk
#                     and compiler messages with NAME.2bk, taken from
#                     llvm/x86_64-linux/tst if there, else x86/linux/tst;
#                     without NAME.1bk only the messages are compared
#   c-testsuite DIR   DIR/tests/single-exec/*.c tagged c89; stdout+stderr
#                     compared with NAME.c.expected, exit status must be 0
#   torture DIR       DIR/*.c and DIR/ieee/*.c of gcc.c-torture/execute;
#                     exit status must be 0
#   llvm-bench DIR    DIR/SingleSource/Benchmarks/{Stanford,McGill,Shootout,
#                     Dhrystone}/*.c of the LLVM test-suite; stdout+stderr and
#                     "exit N" compared with NAME.reference_output
#
# Programs of the external suites are used only if they are C89: no GNU
# keywords, GCC predefined macros or #pragma, accepted by clang and gcc in
# strict C89 mode, no implicitly declared function that may not return int,
# and not on the exclusion list below.  The test files are not copied into
# this repository; pass the directory of a checkout instead.
#
# $LCC replaces the compiler, e.g. LCC=gcc LCCLIBS=-lm to check the suites
# themselves; compiler messages are then not compared, and tests that check
# only messages are skipped.  $LCCLIBS goes after the source file.
#
# Results are PASS, FAIL (wrong output or exit status), CFAIL (does not
# compile), TIMEOUT and SKIP.  The exit status is 0 only if every test that
# is not skipped passes.

cd `dirname $0`/.. || exit 1

BUILDDIR=${BUILDDIR-.}
quiet=0
outdir=
while [ $# -gt 0 ]; do
	case "$1" in
	-b)	BUILDDIR=$2; shift 2 ;;
	-o)	outdir=$2; shift 2 ;;
	-q)	quiet=1; shift ;;
	-*)	echo "$0: unknown option $1" 1>&2; exit 2 ;;
	*)	break ;;
	esac
done
suite=$1
[ $# -gt 0 ] && shift
case "$suite" in
lcc)	dir= ;;
c-testsuite|torture|llvm-bench)
	dir=$1
	if [ -z "$dir" -o ! -d "$dir" ]; then
		echo "$0: $suite needs the directory of the suite" 1>&2; exit 2
	fi
	shift ;;
*)	sed -n '2,/^$/s/^# \{0,1\}//p' $0 1>&2; exit 2 ;;
esac

if [ -n "$LCC" ]; then
	ownlcc=0
else
	ownlcc=1
	LCC="$BUILDDIR/lcc -Wo-lccdir=$BUILDDIR -Iinclude/llvm/x86_64-linux"
fi
T=${outdir:-$BUILDDIR/llvm/x86_64-linux/tst/$suite}
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
	enum-3.c)	echo "relies on an enum type being compatible with int" ;;
	pr71626-2.c)	echo "includes pr71626-1.c, which uses GNU extensions" ;;
	*)		return 1 ;;
	esac
	return 0
}

isc89() {
	excluded `basename $1` >/dev/null && return 1
	grep -E "$GNUKW" "$1" >/dev/null && return 1
	if grep -oE '\b__[A-Z][A-Z0-9_]*__\b' "$1" |
	   grep -vE '^__(STDC|FILE|LINE|DATE|TIME)__$' >/dev/null; then
		return 1
	fi
	grep -E '^[ 	]*#[ 	]*pragma' "$1" >/dev/null && return 1
	grep -w va_copy "$1" >/dev/null && return 1	# C99
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
	return 0
}

# execute name input seconds: run $T/name; set $status
execute() {
	timeout $3 $T/$1 <$2 >$T/$1.out 2>$T/$1.err
	status=$?
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
		out1=llvm/x86_64-linux/tst/$n.1bk; [ -r $out1 ] || out1=x86/linux/tst/$n.1bk
		out2=llvm/x86_64-linux/tst/$n.2bk; [ -r $out2 ] || out2=x86/linux/tst/$n.2bk
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
c-testsuite)
	for src in $dir/tests/single-exec/*.c; do
		n=`basename $src .c`; selected $n || continue
		grep -w c89 $src.tags >/dev/null 2>&1 || continue
		isc89 $src || continue
		compile $n $src || continue
		timeout 60 $T/$n </dev/null >$T/$n.out 2>&1
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
			isc89 $src || continue
			compile $n $src || continue
			timeout 300 $T/$n </dev/null >$T/$n.out 2>&1
			status=$?
			if [ $status = 124 ]; then report TIMEOUT $n; continue; fi
			if [ -s $T/$n.out ] && [ -n "`tail -c 1 $T/$n.out`" ]; then
				echo >>$T/$n.out
			fi
			echo "exit $status" >>$T/$n.out
			if fpcmp $T/$n.out $ref $tol; then report PASS $n
			else report FAIL $n "output differs from $n.reference_output"; fi
		done
	done ;;
esac

total=`expr $npass + $nfail + $ncfail + $ntimeout`
echo "$suite: $total tests, $npass passed, $nfail failed, $ncfail did not compile, $ntimeout timed out, $nskip skipped"
[ $total = $npass ]
