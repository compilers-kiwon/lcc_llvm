SUMMARY = "lcc as a cross compiler, with the LLVM and MLIR back ends"
DESCRIPTION = "lcc, Fraser and Hanson's retargetable ANSI C (C89) compiler, \
with back ends that emit LLVM IR and MLIR. It runs on the build host: lcc's \
cpp and rcc turn C into LLVM IR (or MLIR, which mlir-native lowers to it), \
llc of llvm-native makes the object, and the toolchain's gcc links it \
against the target sysroot."

require lcc.inc

PN = "lcc-cross-${TARGET_ARCH}"

# llc is llvm-native's; the gcc that links is the toolchain's, which every
# target recipe has already.
DEPENDS = "llvm-native"

inherit cross

# The back ends whose drivers are built: ${TARGET_PREFIX}lcc for llvm, and
# ${TARGET_PREFIX}lcc-mlir for mlir, which needs an MLIR build, mlir-native.
# Set LCC_BACKENDS = "llvm mlir" in local.conf for both.
LCC_BACKENDS ??= "llvm"
PACKAGECONFIG ??= "${@bb.utils.filter('LCC_BACKENDS', 'mlir', d)}"
PACKAGECONFIG[mlir] = ",,mlir-native"

# etc/oe-linux.c is the driver: OEHOST gives the target's host file (cpp's
# predefined macros, rcc's target and llc's options), OECC the gcc that
# links. The quotes survive make and its shell, so that both are C strings.
OECFLAGS = "-DOEHOST='\"llvm-${LCCTARGET}.c\"' -DOECC='\"${TARGET_PREFIX}gcc\"'"

EXTRA_OEMAKE += "HOSTFILE=etc/oe-linux.c CC='${BUILD_CC}' \
                LD='${BUILD_CC}' LDFLAGS='${BUILD_LDFLAGS}'"

# lcc's makefile builds with -g alone; Yocto optimizes. lcc's original back
# ends, which are in rcc too, read a double's words through an unsigned *,
# which gcc -O2 breaks unless aliasing is left alone.
LCC_CFLAGS = "${BUILD_CFLAGS} -fno-strict-aliasing"

do_compile() {
    oe_runmake "CFLAGS=${LCC_CFLAGS} ${OECFLAGS}" rcc lcc cpp
    oe_runmake INCDIR=include/llvm/${LCCTARGET} headers
    # the mlir driver: the same lcc.o with the mlir host file; rcc, cpp and
    # the headers are the same for both back ends
    if ${@bb.utils.contains('PACKAGECONFIG', 'mlir', 'true', 'false', d)}; then
        ${BUILD_CC} ${LCC_CFLAGS} -DOEHOST='"mlir-${LCCTARGET}.c"' \
            -DOECC='"${TARGET_PREFIX}gcc"' \
            -c -o ${B}/host-mlir.o ${S}/etc/oe-linux.c
        ${BUILD_CC} ${BUILD_LDFLAGS} -o ${B}/lcc-mlir ${B}/lcc.o ${B}/host-mlir.o
    fi
}

do_install() {
    install -d ${D}${libexecdir}/lcc ${D}${bindir}
    install -m 0755 ${B}/rcc ${B}/cpp ${D}${libexecdir}/lcc/
    cp -R --no-preserve=ownership ${B}/include ${D}${libexecdir}/lcc/
    # LCCDIR, where a driver finds cpp, rcc and the headers, relative to the
    # driver: each recipe's sysroot is somewhere else.
    install -m 0755 ${B}/lcc ${D}${bindir}/${TARGET_PREFIX}lcc
    create_wrapper ${D}${bindir}/${TARGET_PREFIX}lcc LCCDIR=${libexecdir}/lcc
    if ${@bb.utils.contains('PACKAGECONFIG', 'mlir', 'true', 'false', d)}; then
        install -m 0755 ${B}/lcc-mlir ${D}${bindir}/${TARGET_PREFIX}lcc-mlir
        create_wrapper ${D}${bindir}/${TARGET_PREFIX}lcc-mlir LCCDIR=${libexecdir}/lcc
    fi
}
