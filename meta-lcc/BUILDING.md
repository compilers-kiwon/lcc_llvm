# Building meta-lcc from scratch

The steps that were run, on Ubuntu 24.04 under WSL2 with 24 cores and 15 GB.

## 1. Host packages

```bash
sudo apt install -y git python3 gawk wget diffstat chrpath cpio zstd lz4 file \
                    rpcsvc-proto python3-websockets
```

`rpcsvc-proto` gives `rpcgen`, which BitBake requires before it reads any
layer; `python3-websockets` lets it reach the hash equivalence server that
the sstate mirror of step 5 needs.

To run the checks of step 8 as well:

```bash
sudo apt install -y qemu-user clang gcc llvm
```

## 2. Locale

BitBake does not start without `en_US.UTF-8`:

```bash
sudo sed -i 's/^# *en_US.UTF-8 UTF-8/en_US.UTF-8 UTF-8/' /etc/locale.gen
sudo locale-gen
```

## 3. BitBake, openembedded-core and lcc

BitBake and openembedded-core are separate repositories (poky's master is no
longer updated); the layer is in lcc's repository.

```bash
mkdir -p ~/yocto/oe && cd ~/yocto/oe
git clone --depth 1 https://git.openembedded.org/bitbake
git clone --depth 1 https://git.openembedded.org/openembedded-core
git clone https://github.com/compilers-kiwon/lcc_llvm.git
```

## 4. A build directory

```bash
cd ~/yocto/oe
export BITBAKEDIR=$HOME/yocto/oe/bitbake
source openembedded-core/oe-init-build-env build
```

## 5. Configure

Append to `conf/local.conf`:

```
MACHINE = "qemux86-64"

BB_NUMBER_THREADS = "4"
PARALLEL_MAKE = "-j 6"

# finished task output from the Yocto Project, where the hashes match
SSTATE_MIRRORS ?= "file://.* http://sstate.yoctoproject.org/all/PATH;downloadfilename=PATH"
BB_HASHSERVE_UPSTREAM = "wss://hashserv.yoctoproject.org/ws"

# liblcc is lcc's code; see the licence in README.md
LICENSE_FLAGS_ACCEPTED += "commercial_lcc-runtime"

# only on a distribution not on Yocto's tested list
SANITY_TESTED_DISTROS = ""
```

`BB_NUMBER_THREADS` recipes run at once, each with up to `PARALLEL_MAKE`
compilers: size the two by memory. 4 and `-j 6` fit in 15 GB.

## 6. Add the layer and build

```bash
bitbake-layers add-layer ~/yocto/oe/lcc_llvm/meta-lcc
bitbake lcc-hello
```

The first build makes the cross toolchain, glibc and `llvm-native` (for
`llc`); with the sstate mirror some of them come down ready-made. The lcc
recipes themselves take a minute.

## 7. Check what came out

```bash
W=tmp/work/x86-64-v3-oe-linux/lcc-hello/1.0
file $W/build/lcc-hello
echo "270 192" | qemu-x86_64 -L $W/recipe-sysroot \
    -E LD_LIBRARY_PATH=/usr/lib:/lib $W/build/lcc-hello
```

It prints `gcd 6`. The program is the target's: its loader and glibc are in
the sysroot, not on the host, even for `qemux86-64`. `LD_LIBRARY_PATH` keeps
the target's loader from reading the host's `ld.so.cache`. The packages are
in `tmp/deploy/ipk/`.

To put it in an image, add `IMAGE_INSTALL:append = " lcc-hello"` to
`local.conf` and build `core-image-minimal`.

## 8. Check lcc on every machine

```bash
~/yocto/oe/lcc_llvm/meta-lcc/scripts/check-tests.sh \
    qemux86-64 qemuarm64 qemuriscv64 qemuarm qemuriscv32
```

For each machine it builds and runs `lcc-hello`, then runs the repository's
`src/run-llvm.sh` with that machine's lcc: the suites `lcc` (lcc's own
`tst/*.c`) and `unit` (`tst/llvm`, whose drivers the toolchain's gcc
compiles). Each machine costs a cross toolchain and a glibc. The external
suites take their checkouts:

```bash
SUITES="lcc unit c-testsuite torture llvm-bench" \
C_TESTSUITE=/path/to/c-testsuite \
TORTURE=/path/to/llvm-test-suite/SingleSource/Regression/C/gcc-c-torture/execute \
LLVM_TESTSUITE=/path/to/llvm-test-suite \
    ~/yocto/oe/lcc_llvm/meta-lcc/scripts/check-tests.sh qemux86-64
```

The results are in `tmp/lcc-tests/MACHINE/`.

## Bumping the revision

`lcc-cross` and `lcc-runtime` fetch the commit `SRCREV` in `lcc.inc`; move it
to a pushed commit and rerun step 8. To build a working tree instead:

```
INHERIT += "externalsrc"
EXTERNALSRC_SYMLINKS = ""
EXTERNALSRC:pn-lcc-cross-x86_64 = "/path/to/lcc_llvm"
```

with one `EXTERNALSRC:pn-lcc-cross-ARCH` line per architecture. Leave
`lcc-runtime` out unless `lib/` changed: packaging a target recipe
hard-links its sources into the debug package and changes their mode,
which in an external tree means the working tree's own files.
