#!/bin/sh
# selfhost.sh - build lcc with itself and check that it reaches a fixed point
#
# usage: src/selfhost.sh [-t llvm|mlir] [-x target] -b builddir [-o outdir]
#
# builddir has lcc for llvm/TARGET (or mlir/TARGET with -t mlir) built by
# the host compiler (stage 1), as by make BUILDDIR=builddir
# HOSTFILE=etc/llvm-TARGET.c all (etc/mlir-TARGET.c) with
# include/llvm/TARGET copied to builddir/include (with its subdirectories:
# cp -R include/llvm/TARGET/. builddir/include); TARGET is x86_64-linux,
# or aarch64-linux, riscv64-linux, armhf-linux or riscv32-linux, whose
# stage 1 is a cross compiler (riscv32-linux finds its toolchain at
# builddir/riscv32).  Stage 2 is this
# repository built by the stage 1 lcc in outdir/2, and stage 3 is built by
# the stage 2 lcc in outdir/3; outdir is builddir/selfhost by default.
#
# Then every C source of the build is compiled to LLVM IR (MLIR) by the lcc
# of each stage, the back ends that lburg generates taken from stage 1 so
# that the input is the same.  Stages 2 and 3 must give the same .ll
# (.mlir), and their lburg must generate the same back ends (but for the
# time stamp in them).  Differences between stages 1 and 2 are only
# reported: the long double of stage 1 has the host's 80
# bits and the others' 64, so constant folding may differ.
#
# The exit status is 0 only if stages 2 and 3 agree.  To run the tests with
# stage 2, pass outdir/2 to run-llvm.sh -b.
#
# For the other targets, stages 2 and 3 are programs of the
# target, which run with qemu through binfmt_misc (the build runs their
# lburg, and stage 3 is built by stage 2); QEMU_LD_PREFIX gives qemu the
# target's libraries.  The long double of stage 2 is IEEE quad, or double
# on armhf, so stages 1 and 2 may differ where constants of type double
# are folded.

cd `dirname $0`/.. || exit 1

B=
O=
TGT=llvm
X=x86_64-linux
while [ $# -gt 0 ]; do
	case "$1" in
	-b)	B=$2; shift 2 ;;
	-t)	TGT=$2; shift 2 ;;
	-x)	X=$2; shift 2 ;;
	-o)	O=$2; shift 2 ;;
	*)	sed -n '2,/^$/s/^# \{0,1\}//p' $0 1>&2; exit 2 ;;
	esac
done
if [ -z "$B" ] || [ ! -x "$B/lcc" ]; then
	sed -n '2,/^$/s/^# \{0,1\}//p' $0 1>&2
	exit 2
fi
B=`cd $B && pwd`
O=${O:-$B/selfhost}
mkdir -p $O || exit 1
O=`cd $O && pwd`
case $TGT in
llvm)	SUF=ll ;;
mlir)	SUF=mlir ;;
*)	echo "$0: unknown target $TGT" 1>&2; exit 2 ;;
esac
case $X in
x86_64-linux) ;;
aarch64-linux|riscv64-linux)
	QEMU_LD_PREFIX=/usr/`echo $X | sed 's/-linux$//'`-linux-gnu
	export QEMU_LD_PREFIX ;;
armhf-linux)
	QEMU_LD_PREFIX=/usr/arm-linux-gnueabihf
	export QEMU_LD_PREFIX ;;
riscv32-linux)	# builddir/riscv32 is the toolchain
	QEMU_LD_PREFIX=`cd $B/riscv32 && pwd`/sysroot
	export QEMU_LD_PREFIX ;;
*)	echo "$0: unknown target $X" 1>&2; exit 2 ;;
esac
HOSTFILE=etc/$TGT-$X.c

# build stage $1 with the lcc in $2
stage() {
	D=$O/$1
	rm -rf $D
	mkdir -p $D/include || exit 1
	cp -R include/llvm/$X/. $D/include/ || exit 1
	[ -e $B/riscv32 ] && ln -s `cd $B/riscv32 && pwd` $D/riscv32
	echo "stage $1: building with $2/lcc"
	if ! make BUILDDIR=$D HOSTFILE=$HOSTFILE \
	    CC="$2/lcc -Wo-lccdir=$2" CFLAGS= LDFLAGS= all >$D/make.log 2>&1; then
		echo "stage $1: build failed; see $D/make.log"
		exit 1
	fi
}
stage 2 $B
stage 3 $O/2

GEN="dagcheck alpha mips sparc x86 x86linux"
SRCS="`ls src/*.c cpp/*.c lburg/*.c lib/*.c etc/lcc.c etc/bprint.c etc/llvm-*.c etc/mlir-*.c`"
for g in $GEN; do
	SRCS="$SRCS $B/$g.c"
done

# compile every source to LLVM IR (MLIR) with the lcc in $2, into $O/ll$1
compile() {
	mkdir -p $O/ll$1 || exit 1
	for f in $SRCS; do
		n=`echo $f | sed "s|^$B/|gen/|; s|/|_|g; s|\.c$||"`
		if ! $2/lcc -Wo-lccdir=$2 -S -Isrc -Icpp -Ilburg -o $O/ll$1/$n.$SUF $f \
		    >$O/ll$1/$n.err 2>&1; then
			echo "stage $1: $f does not compile; see $O/ll$1/$n.err"
			exit 1
		fi
	done
}
compile 1 $B
compile 2 $O/2
compile 3 $O/3

# samegen a b - whether the back ends that lburg generated are the same,
# but for the time they were generated at
samegen() {
	sed '/^generated at /d' $1 >$O/gen.a
	sed '/^generated at /d' $2 >$O/gen.b
	cmp -s $O/gen.a $O/gen.b
}

status=0
n=0 d12=0 d23=0
for f in $O/ll1/*.$SUF; do
	n=`expr $n + 1`
	ll=`basename $f`
	if ! cmp -s $O/ll2/$ll $O/ll3/$ll; then
		echo "stages 2 and 3 differ: $ll"
		d23=`expr $d23 + 1`
		status=1
	fi
	if ! cmp -s $O/ll1/$ll $O/ll2/$ll; then
		echo "stages 1 and 2 differ: $ll"
		d12=`expr $d12 + 1`
	fi
done
for g in $GEN; do
	if ! samegen $O/2/$g.c $O/3/$g.c; then
		echo "lburg of stages 2 and 3 generates different $g.c"
		status=1
	fi
	if ! samegen $B/$g.c $O/2/$g.c; then
		echo "lburg of stages 1 and 2 generates different $g.c"
	fi
done
echo "$n sources: stages 2 and 3 differ in $d23, stages 1 and 2 in $d12"
exit $status
