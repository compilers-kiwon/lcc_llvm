#!/bin/sh
# ci-target.sh deps|build target backend [builddir] - set up and build lcc
# for one target and back end, as CI does, on Ubuntu 24.04
#
#   deps   install what building and testing need: clang 18 and lld (llvm),
#          LLVM 21 with MLIR from apt.llvm.org (mlir), the target's cross gcc
#          and C library, qemu (user mode; system for cortexm3-eabi) and
#          binfmt_misc for selfhost.sh
#   build  build lcc in builddir with etc/BACKEND-TARGET.c: cpp, rcc, the
#          driver, the target's headers and liblcc, built by the target's
#          gcc, and the board files for cortexm3-eabi; riscv32-linux takes
#          its toolchain from $RISCV32 (.github/scripts/riscv32-toolchain.sh);
#          $SANITIZE builds lcc itself with -fsanitize=$SANITIZE
#
# target is x86_64-linux, aarch64-linux, riscv64-linux, armhf-linux,
# riscv32-linux or cortexm3-eabi, backend llvm or mlir.
set -e
cmd=$1 T=$2 BE=$3 B=$4
case "$BE" in llvm|mlir) ;; *) echo "usage: $0 deps|build target llvm|mlir [builddir]" 1>&2; exit 2 ;; esac

case $cmd in
deps)
	pkgs="make gcc clang lld llvm qemu-user qemu-user-binfmt"
	case $T in
	x86_64-linux)	pkgs="$pkgs gcc-multilib" ;;
	aarch64-linux)	pkgs="$pkgs gcc-aarch64-linux-gnu libc6-dev-arm64-cross" ;;
	riscv64-linux)	pkgs="$pkgs gcc-riscv64-linux-gnu libc6-dev-riscv64-cross" ;;
	armhf-linux)	pkgs="$pkgs gcc-arm-linux-gnueabihf libc6-dev-armhf-cross" ;;
	riscv32-linux)	;;
	cortexm3-eabi)	pkgs="$pkgs gcc-arm-none-eabi libnewlib-arm-none-eabi qemu-system-arm" ;;
	*)	echo "$0: unknown target $T" 1>&2; exit 2 ;;
	esac
	sudo apt-get update -q
	sudo apt-get install -y -q --no-install-recommends $pkgs
	if [ $BE = mlir ]; then
		# the mlir drivers run mlir-opt, mlir-translate and clang of LLVM 21
		wget -qO /tmp/llvm.sh https://apt.llvm.org/llvm.sh
		sudo bash /tmp/llvm.sh 21
		sudo apt-get install -y -q --no-install-recommends \
			clang-21 lld-21 llvm-21 mlir-21-tools
	fi ;;
build)
	[ -n "$B" ] || { echo "usage: $0 build target backend builddir" 1>&2; exit 2; }
	mkdir -p "$B"
	H=etc/$BE-$T.c
	if [ -n "$SANITIZE" ]; then
		set -- "CFLAGS=-g -O1 -fno-omit-frame-pointer -fsanitize=$SANITIZE" \
			"LDFLAGS=-fsanitize=$SANITIZE"
	else
		set --
	fi
	case $T in
	x86_64-linux)
		if [ -n "$SANITIZE" ]; then	# not liblcc, which programs link
			make BUILDDIR=$B HOSTFILE=$H "$@" rcc lcc cpp headers
			make BUILDDIR=$B liblcc
		else
			make BUILDDIR=$B HOSTFILE=$H all headers
		fi ;;
	riscv32-linux)
		[ -x "$RISCV32/bin/riscv32-unknown-linux-gnu-gcc" ] ||
			{ echo "$0: no riscv32 toolchain in \$RISCV32" 1>&2; exit 1; }
		ln -sfn "$RISCV32" $B/riscv32
		make BUILDDIR=$B HOSTFILE=$H "$@" rcc lcc cpp headers
		make BUILDDIR=$B CC="$B/riscv32/bin/riscv32-unknown-linux-gnu-gcc -fPIE" liblcc ;;
	cortexm3-eabi)
		make BUILDDIR=$B HOSTFILE=$H "$@" rcc lcc cpp headers board
		make BUILDDIR=$B CC="arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -fno-short-enums" liblcc ;;
	*)
		case $T in
		aarch64-linux)	cc=aarch64-linux-gnu-gcc ;;
		riscv64-linux)	cc=riscv64-linux-gnu-gcc ;;
		armhf-linux)	cc=arm-linux-gnueabihf-gcc ;;
		esac
		make BUILDDIR=$B HOSTFILE=$H "$@" rcc lcc cpp headers
		make BUILDDIR=$B CC=$cc liblcc ;;
	esac ;;
*)
	echo "usage: $0 deps|build target llvm|mlir [builddir]" 1>&2; exit 2 ;;
esac
