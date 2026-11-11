# meta-lcc

A Yocto layer that puts lcc, with its LLVM back end, into the cross
toolchain, the way gcc-cross is there: recipes can compile C89 programs for
the target with `${TARGET_PREFIX}lcc`.

| Recipe | Runs on | Purpose |
| :- | :- | :- |
| `lcc-cross-${TARGET_ARCH}` | build host | `x86_64-oe-linux-lcc` and so on: lcc's driver, cpp, rcc and headers |
| `lcc-runtime` | target | `liblcc.a`, which lcc links into every program |
| `lcc-hello` | target | an example: a C file compiled by lcc into a package |

## How lcc works here

`x86_64-oe-linux-lcc --sysroot=DIR file.c` runs

1. lcc's `cpp`, with lcc's own headers and then `DIR/usr/include`;
2. `rcc -target=llvm/x86_64-linux`, which emits LLVM IR;
3. `llc` of `llvm-native`, which makes a position-independent object;
4. `x86_64-oe-linux-gcc --sysroot=DIR`, which links it with `-llcc -lm`.

The driver is `etc/oe-linux.c`. It takes the target's own host file
(`etc/llvm-x86_64-linux.c` and so on) for cpp's predefined macros and rcc's
target, and that file's `LLCFLAGS` for llc: what clang gives the target by
default (the RISC-V float ABI, ARM's VFP), since lcc's other drivers have
clang make the object.

| `TARGET_ARCH` | lcc target | example `MACHINE` |
| :- | :- | :- |
| `x86_64` | `x86_64-linux` | `qemux86-64` |
| `aarch64` | `aarch64-linux` | `qemuarm64` |
| `riscv64` | `riscv64-linux` | `qemuriscv64` |
| `arm`, hard float | `armhf-linux` | `qemuarm` |
| `riscv32` | `riscv32-linux` | `qemuriscv32` |

Any other target skips the recipes.

## Using lcc in a recipe

```
DEPENDS = "lcc-cross-${TARGET_ARCH} lcc-runtime"

do_compile() {
    ${TARGET_PREFIX}lcc --sysroot=${STAGING_DIR_TARGET} ${LDFLAGS} \
        -o ${B}/prog ${S}/prog.c
}
```

`--sysroot` is what Yocto's `${CC}` carries for gcc. `${LDFLAGS}` is the
distribution's link policy (GNU hash, relro, now), and lcc passes it to the
gcc that links; gcc's `-Wl,...` options go through whole. `${CFLAGS}` stays
out: those are gcc's options, and lcc reads `-Wf...` and `-Wp...` as its
own.

The toolchain lcc lives in the recipe's sysroot, as gcc-cross does, and is
not in the SDK.

## Licence

lcc's licence is its `CPYRIGHT` file: lcc may be redistributed with that
file, but neither lcc nor a product of which it is a significant part may be
sold. `liblcc.a` is lcc's code linked into every program, so `lcc-runtime`
carries `LICENSE_FLAGS = "commercial"`, and a build has to accept it:

```
LICENSE_FLAGS_ACCEPTED += "commercial_lcc-runtime"
```

## Limits

- C89 only, as lcc. Some glibc headers go beyond C89; see the LIMITATIONS
  section of the repository's README.
- On 32-bit ARM, lcc's `time_t` has 32 bits while Yocto builds everything
  else with `_TIME_BITS=64`: an interface with `time_t` in it, between lcc's
  code and another library, does not match. See the README.
- Only the LLVM back end; the MLIR one needs an MLIR build, which
  openembedded-core does not have.
- `LAYERSERIES_COMPAT` names `blacksail` alone, the series this was built
  with.

## Status

Built against openembedded-core `c2ea956` (blacksail, 2026-11-08) with
LLVM 23.1.3 and GCC 16.2, and checked with
[scripts/check-tests.sh](scripts/check-tests.sh): `lcc-hello` prints
`gcd 6` under user-mode qemu on every machine, and every suite passes.

| `MACHINE` | lcc | unit | c-testsuite | torture | llvm-bench |
| :- | :- | :- | :- | :- | :- |
| `qemux86-64` | 18/18 | 25/25 | 144/144 | 618/618 | 26/26 |
| `qemuarm64` | 18/18 | 25/25 | 144/144 | 618/618 | 26/26 |
| `qemuriscv64` | 18/18 | 25/25 | 144/144 | 618/618 | 26/26 |
| `qemuarm` | 18/18 | 25/25 | 144/144 | 618/618 | 26/26 |
| `qemuriscv32` | 18/18 | 25/25 | 144/144 | 618/618 | 26/26 |

Two things only the Yocto build found:

| Symptom | Cause |
| :- | :- |
| rcc crashes at the first function | Yocto builds it with `-O2`, lcc's makefile with `-g`; lcc's allocator aligned for `double`, not for the `long double` in its symbols |
| `qemuarm` programs ask for `/usr/lib/ld-linux.so.3` | Yocto's gcc gets `-mfloat-abi=hard` from `${CC}`, not by default; the driver passes it when it links for `armhf-linux` |

## Use

[BUILDING.md](BUILDING.md) goes from an empty machine to a checked build.
