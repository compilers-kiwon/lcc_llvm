#!/bin/sh
# run-x86.sh - compile tst/*.c with -target=x86/linux, run them with gcc -m32
#
# usage: src/run-x86.sh [-b builddir] [-o outdir] [-q] [name ...]
#
# Each tst/NAME.c is preprocessed by builddir/cpp with the options of
# etc/linux.c, with include/x86/linux and gcc's include directory for -m32
# before /usr/include, compiled by builddir/rcc -target=x86/linux (any
# build has every target, the llvm one too), and assembled and linked by
# gcc -m32, which needs gcc-multilib.  The compiler messages are compared
# with x86/linux/tst/NAME.2bk, and the output for input tst/NAME.0 with
# NAME.1bk, taken from x86/linux/tst/glibc if there, else x86/linux/tst.
# x86/linux/tst/glibc has the outputs of the current glibc where they differ
# from the original ones: paranoia, whose sqrt is now correctly rounded and
# whose 0/0 prints as -nan.  Without NAME.1bk only the messages are
# compared.  The assembly is compared with NAME.sbk only to report it,
# since it depends on the headers.
#
# Results are PASS, FAIL (wrong output or messages), CFAIL (does not
# compile or link) and TIMEOUT.  The exit status is 0 only if every test
# passes.

cd `dirname $0`/.. || exit 1

BUILDDIR=${BUILDDIR-.}
quiet=0
outdir=
while [ $# -gt 0 ]; do
	case "$1" in
	-b)	BUILDDIR=$2; shift 2 ;;
	-o)	outdir=$2; shift 2 ;;
	-q)	quiet=1; shift ;;
	-*)	sed -n '2,/^$/s/^# \{0,1\}//p' $0 1>&2; exit 2 ;;
	*)	break ;;
	esac
done
T=${outdir:-$BUILDDIR/x86/linux/tst}
mkdir -p $T || exit 1
GCCINC=`gcc -m32 -print-file-name=include`
CPPFLAGS="-U__GNUC__ -D_POSIX_SOURCE -D__STDC__=1 -D__STRICT_ANSI__ -Dunix -Di386
	-Dlinux -D__unix__ -D__i386__ -D__linux__ -D__signed__=signed
	-Ualpha -Usun -Uvax -Umips -Ux86 -Dx86
	-Iinclude/x86/linux -I$GCCINC -I/usr/include"

npass=0 nfail=0 ncfail=0 ntimeout=0
report() {	# status name [reason]
	case "$1" in
	PASS)	npass=`expr $npass + 1` ;;
	FAIL)	nfail=`expr $nfail + 1` ;;
	CFAIL)	ncfail=`expr $ncfail + 1` ;;
	TIMEOUT) ntimeout=`expr $ntimeout + 1` ;;
	esac
	if [ $quiet = 0 -o "$1" != PASS ]; then
		printf '%-8s%s%s\n' "$1" "$2" "${3:+  ($3)}"
	fi
}

selected() {	# name: is it among the names given on the command line?
	[ -z "$names" ] && return 0
	for s in $names; do
		[ "$s" = "$1" -o "$s.c" = "$1" ] && return 0
	done
	return 1
}
names="$*"

for src in tst/*.c; do
	n=`basename $src .c`; selected $n || continue
	out1=x86/linux/tst/glibc/$n.1bk; [ -r $out1 ] || out1=x86/linux/tst/$n.1bk
	out2=x86/linux/tst/$n.2bk
	$BUILDDIR/cpp $CPPFLAGS $src >$T/$n.i 2>$T/$n.cerr
	rm -f $T/$n.2
	timeout 300 $BUILDDIR/rcc -target=x86/linux -g0 -errout=$T/$n.2 $T/$n.i >$T/$n.s 2>>$T/$n.cerr
	ok=$?
	if [ -r $out2 ] && ! cmp -s $out2 $T/$n.2; then
		report FAIL $n "messages differ from $out2"; continue
	fi
	if [ ! -r $out1 ]; then
		report PASS $n "messages only"; continue
	fi
	if [ $ok != 0 ]; then
		report CFAIL $n "`grep -v ': warning: ' $T/$n.2 $T/$n.cerr | head -1`"; continue
	fi
	note=
	[ -r x86/linux/tst/$n.sbk ] && ! cmp -s x86/linux/tst/$n.sbk $T/$n.s &&
		note="assembly differs from $n.sbk"
	if ! gcc -m32 -o $T/$n $T/$n.s -lm >$T/$n.lerr 2>&1; then
		report CFAIL $n "`head -1 $T/$n.lerr`"; continue
	fi
	timeout 60 $T/$n <tst/$n.0 >$T/$n.1 2>$T/$n.err
	if [ $? = 124 ]; then report TIMEOUT $n; continue; fi
	if cmp -s $out1 $T/$n.1; then report PASS $n "$note"
	else report FAIL $n "output differs from $out1${note:+; $note}"; fi
done

total=`expr $npass + $nfail + $ncfail + $ntimeout`
echo "x86/linux: $total tests, $npass passed, $nfail failed, $ncfail did not compile, $ntimeout timed out"
[ $total = $npass ]
