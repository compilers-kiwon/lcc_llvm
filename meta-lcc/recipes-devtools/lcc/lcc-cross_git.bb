SUMMARY = "lcc as a cross compiler, with the LLVM back end"
DESCRIPTION = "lcc, Fraser and Hanson's retargetable ANSI C (C89) compiler, \
with a back end that emits LLVM IR. It runs on the build host: lcc's cpp \
and rcc turn C into LLVM IR, llc of llvm-native makes the object, and the \
toolchain's gcc links it against the target sysroot."

require lcc.inc

PN = "lcc-cross-${TARGET_ARCH}"

# llc is llvm-native's; the gcc that links is the toolchain's, which every
# target recipe has already.
DEPENDS = "llvm-native"

inherit cross

# etc/oe-linux.c is the driver: OEHOST gives the target's host file (cpp's
# predefined macros, rcc's target and llc's options), OECC the gcc that
# links. The quotes survive make, so that both are C strings.
OECFLAGS = "-DOEHOST='\"llvm-${LCCTARGET}.c\"' -DOECC='\"${TARGET_PREFIX}gcc\"'"

EXTRA_OEMAKE += "HOSTFILE=etc/oe-linux.c CC='${BUILD_CC}' \
                LD='${BUILD_CC}' LDFLAGS='${BUILD_LDFLAGS}'"

# lcc's makefile builds with -g alone; Yocto optimizes. lcc's original back
# ends, which are in rcc too, read a double's words through an unsigned *,
# which gcc -O2 breaks unless aliasing is left alone.
do_compile() {
    oe_runmake "CFLAGS=${BUILD_CFLAGS} -fno-strict-aliasing ${OECFLAGS}" rcc lcc cpp
    oe_runmake INCDIR=include/llvm/${LCCTARGET} headers
}

do_install() {
    install -d ${D}${libexecdir}/lcc ${D}${bindir}
    install -m 0755 ${B}/rcc ${B}/cpp ${D}${libexecdir}/lcc/
    cp -R --no-preserve=ownership ${B}/include ${D}${libexecdir}/lcc/
    install -m 0755 ${B}/lcc ${D}${bindir}/${TARGET_PREFIX}lcc
    # LCCDIR, where the driver finds cpp, rcc and the headers, relative to
    # the driver: each recipe's sysroot is somewhere else.
    create_wrapper ${D}${bindir}/${TARGET_PREFIX}lcc LCCDIR=${libexecdir}/lcc
}
