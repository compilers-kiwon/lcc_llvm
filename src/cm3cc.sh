#!/bin/sh
# cm3cc.sh builddir gcc|clang [option|file]... - compile for the Cortex-M3
# of qemu-system-arm -M mps2-an385 with gcc or clang, as run-llvm.sh does
# for the unit test drivers.  With -c, -S or -E the compiler runs alone;
# otherwise each .c is compiled to an object and everything is linked by
# arm-none-eabi-gcc with newlib, semihosting (rdimon), builddir/cortexm3.ld
# and builddir/vectors.o, as lcc's driver does.  gcc gets -fno-short-enums:
# lcc's enums have 4 bytes, as clang's.
B=$1
GCC="arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb"
case $2 in
gcc)	C="$GCC -fno-short-enums" ;;
clang)	C="clang --target=thumbv7m-none-eabi -mcpu=cortex-m3 -mfloat-abi=soft --sysroot=/usr/lib/arm-none-eabi" ;;
*)	echo "usage: $0 builddir gcc|clang args..." 1>&2; exit 2 ;;
esac
shift 2
for a; do
	case $a in -c|-S|-E) exec $C "$@" ;; esac
done
T=`mktemp -d ${TMPDIR:-/tmp}/cm3cc.XXXXXX` || exit 1
trap 'rm -rf "$T"' 0
trap 'exit 1' 1 2 15
cflags= lflags= objs= out=a.out next= n=0
for a; do
	if [ -n "$next" ]; then out=$a; next=; continue; fi
	case $a in
	-o)	next=1 ;;
	-l*|-Wl,*) lflags="$lflags $a" ;;
	*.c)	n=`expr $n + 1`
		$C $cflags -c -o $T/$n.o $a || exit 1
		objs="$objs $T/$n.o" ;;
	*.o|*.a) objs="$objs $a" ;;
	*)	cflags="$cflags $a" ;;
	esac
done
$GCC --specs=rdimon.specs -T $B/cortexm3.ld -Wl,--no-enum-size-warning \
	-Wl,--no-warn-execstack -o $out $objs $B/vectors.o -L$B $lflags
