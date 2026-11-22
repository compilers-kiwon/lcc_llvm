#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# check.sh - check lcc-opt on the kernels of this directory
#
# usage: lcc-opt/kernels/check.sh [-o outdir] builddir lcc-opt target
#
# builddir holds an lcc built with etc/mlir-TARGET.c, lcc-opt is the
# program, and target x86_64-linux or aarch64-linux.  The kernels are
# convfix.c (the kernel of the completion criteria), convw.c, convff.c,
# mm.c and conv3.c; main.c runs each on inputs from an LCG and prints
# checksums of what it writes.  The checks:
#
#   shapes  the IR of each kernel: lcc-to-affine raises convfix to three
#           affine.for, affine-loop-tile and affine-super-vectorize (with
#           vectorize-reductions) change that, and lcc-to-linalg raises
#           every kernel to the linalg operations it should, without loops
#           (convfix to a linalg.broadcast, a linalg.matmul and an
#           element-wise linalg.generic); after the transform scripts
#           tile.mlir, tile-vector.mlir and mask-vector.mlir, the linalg
#           operations are in tile loops and, after the latter two, vector
#           operations, masked after mask-vector.mlir
#   runs    each kernel in each configuration below goes through lcc -S,
#           lcc-opt with lcc-lower, mlir-translate, opt -passes=verify and
#           clang -O0 and -O2, and must print, linked with main.c, the same
#           as when gcc -O2 compiles it: bit for bit.  Some configurations
#           add the float sum of convff.c in another order, which can round
#           otherwise; for them the script only reports how many values
#           differ and by how much
#   same    lcc-opt and mlir-translate give the same LLVM IR when run twice
#
# The configurations: lcc (lcc's own driver, without lcc-opt), affine
# (lcc-to-affine), affine-tile (and affine-loop-tile), affine-vector (and
# affine-super-vectorize, which reorders the float sum), linalg
# (lcc-to-linalg), linalg-tile (and tile.mlir), tile-vector (and
# tile-vector.mlir), mask-vector (and mask-vector.mlir).  affine-vector is
# not run on mm.c: MLIR 23's affine-super-vectorize vectorizes the sum of
# mm2 over k and reads b[k][j], 8 elements apart, as if they were next to
# each other, which gives wrong values (see lcc-opt/README.md).
#
# gcc and clang get -ffp-contract=off, which keeps a product and a sum
# from becoming one fused operation.  For aarch64-linux the C compiler is
# aarch64-linux-gnu-gcc and the programs run with qemu-aarch64.  $LLVMBIN
# (default /usr/lib/llvm-23/bin) has mlir-translate, opt and clang.  The
# files go to outdir, by default builddir/kernels.  The exit status is 0
# only if every check passes.

usage() {
	echo "usage: $0 [-o outdir] builddir lcc-opt x86_64-linux|aarch64-linux" 1>&2
	exit 2
}
O=
while [ $# -gt 0 ]; do
	case "$1" in
	-o)	[ $# -ge 2 ] || usage; O=$2; shift 2 ;;
	-*)	usage ;;
	*)	break ;;
	esac
done
[ $# -eq 3 ] || usage
B=$1 LCCOPT=$2 TARGET=$3
K=`dirname "$0"`
K=`cd "$K" && pwd` || exit 2
B=`cd "$B" && pwd` || exit 2
[ -x "$B/lcc" ] || { echo "$0: no lcc in $B" 1>&2; exit 2; }
case "$LCCOPT" in /*) ;; *) LCCOPT=`pwd`/$LCCOPT ;; esac
[ -x "$LCCOPT" ] || { echo "$0: $LCCOPT is not a program" 1>&2; exit 2; }
LLVMBIN=${LLVMBIN-/usr/lib/llvm-23/bin}
case $TARGET in
x86_64-linux)
	CC=gcc CLANG="$LLVMBIN/clang" RUN= ;;
aarch64-linux)
	CC=aarch64-linux-gnu-gcc
	CLANG="$LLVMBIN/clang --target=aarch64-linux-gnu"
	RUN="qemu-aarch64 -L /usr/aarch64-linux-gnu" ;;
*)	usage ;;
esac
O=${O:-$B/kernels}
mkdir -p "$O" && O=`cd "$O" && pwd` || exit 2
CFLAGS="-std=c89 -O2 -ffp-contract=off"

KERNELS="convfix convw convff mm conv3"
CONFIGS="lcc affine affine-tile affine-vector linalg linalg-tile tile-vector mask-vector"

passed=0 failed=0
pass() {
	passed=`expr $passed + 1`
	echo "PASS $*"
}
fail() {
	failed=`expr $failed + 1`
	echo "FAIL $*"
}

# macro kernel: the macro of main.c for kernel
macro() {
	echo "$1" | tr a-z A-Z
}

# raise config in out: in (lcc's .mlir) through lcc-opt with the passes
# of config; to text unless lower is set, else lowered to bytecode
raise() {
	rc=$1 rin=$2 rout=$3
	case $rc in
	affine)	set -- --lcc-to-affine ;;
	affine-tile)
		set -- --lcc-to-affine --affine-loop-tile=tile-sizes=8,8 ;;
	affine-vector)
		set -- --lcc-to-affine \
			"--affine-super-vectorize=virtual-vector-size=8 vectorize-reductions=true" ;;
	linalg)	set -- --lcc-to-linalg ;;
	linalg-tile|tile-vector|mask-vector)
		rs=$rc.mlir
		[ $rc = linalg-tile ] && rs=tile.mlir
		set -- --lcc-to-linalg \
			--transform-preload-library=transform-library-paths=$K/$rs \
			--transform-interpreter ;;
	*)	echo "$0: unknown configuration $rc" 1>&2; return 1 ;;
	esac
	if [ -n "$lower" ]; then
		"$LCCOPT" "$@" --lcc-lower --emit-bytecode "$rin" -o "$rout"
	else
		"$LCCOPT" "$@" "$rin" -o "$rout"
	fi
}

# count pattern file: the number of lines of file that match pattern, an
# extended regular expression
count() {
	grep -c -E -e "$1" "$2"
}

# expect what n pattern file: there are n lines of file matching pattern
# (n is a number, or +N for N or more)
expect() {
	en=$2 ef=$4
	egot=`count "$3" "$ef"`
	case $en in
	+*)	[ "$egot" -ge "${en#+}" ] ;;
	*)	[ "$egot" -eq "$en" ] ;;
	esac && return 0
	fail "shapes: $1: $egot lines of `basename $ef` match '$3', not $en"
	return 1
}

# the C files to IR, and what gcc makes of them
for k in $KERNELS; do
	rm -f "$O/$k.mlir" "$O/$k.ref" "$O/$k.ref.v" "$O/main-$k.o"
	if ! "$B/lcc" -Wo-lccdir="$B" -S -o "$O/$k.mlir" "$K/$k.c" >"$O/$k.lccerr" 2>&1; then
		fail "lcc -S $k.c: `head -1 "$O/$k.lccerr"`"
		continue
	fi
	if ! $CC $CFLAGS -D`macro $k` -c -o "$O/main-$k.o" "$K/main.c" ||
	   ! $CC $CFLAGS -c -o "$O/$k.gcc.o" "$K/$k.c" ||
	   ! $CC -o "$O/$k.gcc" "$O/main-$k.o" "$O/$k.gcc.o" ||
	   ! $RUN "$O/$k.gcc" >"$O/$k.ref" ||
	   ! $RUN "$O/$k.gcc" -v >"$O/$k.ref.v"; then
		fail "$k.c compiled by $CC does not run"
		rm -f "$O/$k.ref"
	fi
done

# shapes
lower=
f=$O/convfix
if [ -f $f.mlir ]; then
	if raise affine $f.mlir $f.affine.mlir; then
		expect "lcc-to-affine raises convfix to three affine.for" 3 'affine\.for' $f.affine.mlir &&
		expect "no scf.for after lcc-to-affine" 0 'scf\.for' $f.affine.mlir &&
		pass "shapes: lcc-to-affine raises convfix to three affine.for"
	else
		fail "shapes: lcc-opt --lcc-to-affine on convfix"
	fi
	if raise affine-tile $f.mlir $f.affine-tile.mlir; then
		expect "affine-loop-tile tiles convfix's two outer loops" 5 'affine\.for' $f.affine-tile.mlir &&
		pass "shapes: affine-loop-tile changes convfix (5 affine.for)"
	else
		fail "shapes: affine-loop-tile on convfix"
	fi
	if raise affine-vector $f.mlir $f.affine-vector.mlir; then
		expect "affine-super-vectorize reads vectors" +2 'vector\.transfer_read' $f.affine-vector.mlir &&
		expect "affine-super-vectorize reduces a vector" +1 'vector\.reduction <add>' $f.affine-vector.mlir &&
		pass "shapes: affine-super-vectorize changes convfix (vector reads, vector.reduction)"
	else
		fail "shapes: affine-super-vectorize on convfix"
	fi
fi
for k in $KERNELS; do
	f=$O/$k
	[ -f $f.mlir ] || continue
	case $k in
	convfix|convw|convff)
		ops="linalg.broadcast=1 linalg.matmul=1 linalg.generic=1" ;;
	mm)	ops="linalg.matmul=3 linalg.fill=2 linalg.copy=1" ;;
	conv3)	ops="linalg.conv_2d=2 linalg.fill=1 linalg.copy=1" ;;
	esac
	if ! raise linalg $f.mlir $f.linalg.mlir; then
		fail "shapes: lcc-opt --lcc-to-linalg on $k"
		continue
	fi
	ok=1
	for o in $ops; do
		expect "lcc-to-linalg on $k" ${o#*=} "${o%=*} " $f.linalg.mlir || ok=
	done
	expect "lcc-to-linalg on $k" 0 'affine\.for|scf\.for' $f.linalg.mlir || ok=
	case $k in
	convfix|convw|convff)
		expect "the clamp of $k is element-wise" 1 'linalg\.generic.*iterator_types = \["parallel", "parallel"\]' $f.linalg.mlir || ok= ;;
	esac
	[ -n "$ok" ] && pass "shapes: lcc-to-linalg raises $k to $ops"
	for c in linalg-tile tile-vector mask-vector; do
		if ! raise $c $f.mlir $f.$c.mlir; then
			fail "shapes: $c on $k"
			continue
		fi
		ok=1
		expect "$c tiles $k" +2 'scf\.for' $f.$c.mlir || ok=
		case $c in
		linalg-tile)
			expect "$c keeps the linalg operations of $k" +3 'linalg\.' $f.$c.mlir || ok=
			expect "$c does not vectorize $k" 0 'vector\.' $f.$c.mlir || ok= ;;
		*)	expect "$c vectorizes $k" +1 'vector\.transfer_read' $f.$c.mlir || ok=
			expect "$c vectorizes $k" +1 'vector\.transfer_write' $f.$c.mlir || ok=
			case $k in
			conv3)	expect "$c leaves the convolutions of $k" 2 'linalg\.conv_2d' $f.$c.mlir || ok= ;;
			*)	expect "$c leaves no linalg operation in $k" 0 'linalg\.' $f.$c.mlir || ok= ;;
			esac
			[ $c = mask-vector ] &&
				{ expect "$c masks $k" +1 'vector\.mask' $f.$c.mlir || ok=; } ;;
		esac
		[ -n "$ok" ] && pass "shapes: $c on $k"
	done
done

# reorders config kernel: whether config adds the float sum of kernel in
# another order than C
reorders() {
	[ $2 = convff ] && [ $1 = affine-vector ]
}

# compare name ref.v out.v: report how many values differ and the largest
# relative difference, for a configuration that reorders a float sum
compare() {
	awk -v name="$1" '
	$1 == "seed" { s = $2; next }
	NR == FNR { ref[s " " $1] = $2; next }
	{
		n++
		if (ref[s " " $1] != $2) {
			d++
			a = ref[s " " $1] + 0; b = $2 + 0
			r = a - b; if (r < 0) r = -r
			m = a < 0 ? -a : a; if ((b < 0 ? -b : b) > m) m = b < 0 ? -b : b
			if (m > 0 && r / m > big) big = r / m
		}
	}
	END {
		if (d == 0) printf "NOTE %s: the same as gcc\n", name
		else printf "NOTE %s: %d of %d values differ from gcc, by at most %.3g relatively\n", name, d, n, big
	}' "$2" "$3"
}

# runs
lower=1
for k in $KERNELS; do
	f=$O/$k
	[ -f $f.mlir ] && [ -f $f.ref ] || continue
	for c in $CONFIGS; do
		case $c$k in
		affine-vectormm) continue ;;
		esac
		g=$f.$c
		rm -f $g.mlirbc $g.ll $g.again.mlirbc $g.again.ll
		if [ $c = lcc ]; then
			:
		elif ! raise $c $f.mlir $g.mlirbc >$g.err 2>&1 ||
		   ! "$LLVMBIN/mlir-translate" --mlir-to-llvmir $g.mlirbc -o $g.ll >>$g.err 2>&1 ||
		   ! "$LLVMBIN/opt" -passes=verify -disable-output $g.ll >>$g.err 2>&1; then
			fail "$k $c: `head -1 $g.err`"
			continue
		elif ! raise $c $f.mlir $g.again.mlirbc >/dev/null 2>&1 ||
		   ! "$LLVMBIN/mlir-translate" --mlir-to-llvmir $g.again.mlirbc -o $g.again.ll ||
		   ! cmp -s $g.ll $g.again.ll; then
			fail "same: $k $c: the LLVM IR differs when made again"
		else
			pass "same: $k $c"
		fi
		for opt in -O0 -O2; do
			h=$g$opt
			rm -f $h.o $h $h.out $h.out.v
			if [ $c = lcc ]; then
				"$B/lcc" -Wo-lccdir="$B" -c -Wa$opt -Wa-ffp-contract=off -o $h.o "$K/$k.c" >$h.err 2>&1
			else
				$CLANG $opt -ffp-contract=off -c -o $h.o $g.ll >$h.err 2>&1
			fi &&
			$CC -o $h "$O/main-$k.o" $h.o >>$h.err 2>&1 &&
			$RUN $h >$h.out 2>>$h.err &&
			$RUN $h -v >$h.out.v 2>>$h.err
			if [ $? -ne 0 ]; then
				fail "$k $c $opt: `head -1 $h.err`"
			elif reorders $c $k; then
				compare "$k $c $opt" $f.ref.v $h.out.v
			elif cmp -s $f.ref $h.out && cmp -s $f.ref.v $h.out.v; then
				pass "$k $c $opt"
			else
				fail "$k $c $opt: prints other values than gcc (`diff $f.ref $h.out | grep -c '^>'` checksums)"
			fi
		done
	done
done

echo "$passed passed, $failed failed"
[ $failed -eq 0 ]
