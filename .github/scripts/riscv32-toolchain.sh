#!/bin/sh
# riscv32-toolchain.sh prefix - build the rv32gc/ilp32d Linux toolchain
# (gcc, glibc) that the riscv32-linux target uses, which Ubuntu does not
# package, from riscv-gnu-toolchain at the commit it was checked with.
set -e
P=${1:?usage: riscv32-toolchain.sh prefix}
REV=317ee3dd0f45156fbfbd112db9899e0933fb2911
S=${TMPDIR:-/tmp}/riscv-gnu-toolchain
rm -rf "$S"
git init -q "$S"
git -C "$S" remote add origin https://github.com/riscv-collab/riscv-gnu-toolchain.git
git -C "$S" fetch -q --depth 1 origin $REV
git -C "$S" checkout -q FETCH_HEAD
cd "$S"
./configure --prefix="$P" --with-arch=rv32gc --with-abi=ilp32d --disable-gdb
make -j`nproc` linux
rm -rf "$P/share"
