#!/bin/sh
# cmp-backends.sh - compare lcc's llvm and mlir back ends
#
# usage: src/cmp-backends.sh -l llvmdir -m mlirdir [-o outdir] [file.c|dir ...]
#
# llvmdir and mlirdir are build directories of lcc for llvm/x86_64-linux
# and mlir/x86_64-linux (HOSTFILE=etc/llvm-x86_64-linux.c and
# etc/mlir-x86_64-linux.c), with include/llvm/x86_64-linux/*.h in their
# include.  Run from anywhere; outdir is mlirdir/cmp by default.
#
# 1. Every program given, or every .c in a directory given, that the llvm
#    lcc compiles and that runs to its end within 60 seconds is compiled by
#    the mlir lcc too and run: its standard output and exit status must be
#    the same.  Programs that the llvm lcc does not compile or that time
#    out are skipped.  The programs read nothing (</dev/null).
# 2. Mixed links: each driver tst/llvm/NAME-drv.c that the llvm lcc
#    compiles is linked with tst/llvm/NAME.c compiled by the mlir lcc, and
#    must exit with 0.
# 3. The instrumenting options -n (null pointer checks), -t (tracing) and
#    -Wf-b (basic block counts in prof.out) must give the same output with
#    both back ends.
#
# The exit status is 0 if nothing differs or fails.

cd `dirname $0`/.. || exit 1

L=
M=
O=
while [ $# -gt 0 ]; do
	case "$1" in
	-l)	L=$2; shift 2 ;;
	-m)	M=$2; shift 2 ;;
	-o)	O=$2; shift 2 ;;
	-*)	sed -n '2,/^$/s/^# \{0,1\}//p' $0 1>&2; exit 2 ;;
	*)	break ;;
	esac
done
if [ -z "$L" ] || [ ! -x "$L/lcc" ] || [ -z "$M" ] || [ ! -x "$M/lcc" ]; then
	sed -n '2,/^$/s/^# \{0,1\}//p' $0 1>&2
	exit 2
fi
L=`cd $L && pwd`
M=`cd $M && pwd`
O=${O:-$M/cmp}
mkdir -p $O || exit 1
O=`cd $O && pwd`
TOP=`pwd`
LLCC="$L/lcc -Wo-lccdir=$L -I$TOP/include/llvm/x86_64-linux"
MLCC="$M/lcc -Wo-lccdir=$M -I$TOP/include/llvm/x86_64-linux"
status=0

# 1. the same results
n=0 same=0 differ=0 cfail=0 skip=0
for a in "$@"; do
	if [ -d "$a" ]; then ls $a/*.c; else echo $a; fi
done >$O/progs
while read f; do
	b=`basename $f .c`
	if ! timeout 300 $LLCC -w -o $O/$b.l $f -lm >/dev/null 2>&1; then
		skip=`expr $skip + 1`
		continue
	fi
	timeout 60 $O/$b.l </dev/null >$O/$b.lout 2>/dev/null
	ls=$?
	if [ $ls = 124 ]; then
		skip=`expr $skip + 1`
		continue
	fi
	n=`expr $n + 1`
	if ! timeout 300 $MLCC -w -o $O/$b.m $f -lm >$O/$b.merr 2>&1; then
		echo "CFAIL   $f"
		cfail=`expr $cfail + 1`
		status=1
		continue
	fi
	timeout 60 $O/$b.m </dev/null >$O/$b.mout 2>/dev/null
	ms=$?
	if [ $ls = $ms ] && cmp -s $O/$b.lout $O/$b.mout; then
		same=`expr $same + 1`
		rm -f $O/$b.l $O/$b.m $O/$b.lout $O/$b.mout $O/$b.merr
	else
		echo "DIFFER  $f (exit $ls, $ms)"
		differ=`expr $differ + 1`
		status=1
	fi
done <$O/progs
echo "same results: $n programs, $same the same, $differ differ, $cfail did not compile with mlir ($skip skipped)"

# 2. mixed links
n=0 pass=0 skip=0
for d in tst/llvm/*-drv.c; do
	b=`basename $d -drv.c`
	if ! $LLCC -w -c -Itst/llvm -o $O/$b-drv.o $d >/dev/null 2>&1; then
		skip=`expr $skip + 1`
		continue
	fi
	n=`expr $n + 1`
	if $MLCC -w -c -o $O/$b.o tst/llvm/$b.c >$O/$b.cerr 2>&1 &&
	    $LLCC -o $O/$b.mix $O/$b-drv.o $O/$b.o -lm >>$O/$b.cerr 2>&1 &&
	    timeout 60 $O/$b.mix </dev/null >$O/$b.mixout 2>&1; then
		pass=`expr $pass + 1`
	else
		echo "FAIL    mixed $b; see $O/$b.cerr, $O/$b.mixout"
		status=1
	fi
done
echo "mixed links: $n drivers, $pass passed ($skip drivers not compiled by lcc)"

# 3. instrumenting options
cat >$O/null.c <<'EOF'
int *p;
int main(void) { return *p; }
EOF
smoke() {	# smoke name options file
	for t in L M; do
		eval c=\$${t}LCC
		mkdir -p $O/$1.$t
		(cd $O/$1.$t && rm -f prof.out &&
		 $c $2 -w -o x $3 -lm >cerr 2>&1 &&
		 timeout 60 ./x </dev/null >out 2>&1; echo "exit $?" >>out)
	done
	if cmp -s $O/$1.L/out $O/$1.M/out && { [ ! -f $O/$1.L/prof.out ] ||
	    cmp -s $O/$1.L/prof.out $O/$1.M/prof.out; }; then
		echo "same    $1"
	else
		echo "DIFFER  $1; see $O/$1.L and $O/$1.M"
		status=1
	fi
}
smoke null -n $O/null.c
smoke null8q -n $TOP/tst/8q.c
smoke trace -t $TOP/tst/8q.c
smoke prof -Wf-b $TOP/tst/sort.c
[ -f $O/prof.L/prof.out ] || { echo "FAIL    -Wf-b wrote no prof.out"; status=1; }
exit $status
