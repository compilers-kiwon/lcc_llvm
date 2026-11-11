SUMMARY = "A C89 program compiled by lcc into the image"
DESCRIPTION = "Shows lcc-cross at work in a recipe: the target's lcc \
compiles and links a C file against the recipe's sysroot."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://hello.c"

DEPENDS = "lcc-cross-${TARGET_ARCH} lcc-runtime"

S = "${UNPACKDIR}"
B = "${WORKDIR}/build"

do_compile() {
    mkdir -p ${B}
    # --sysroot is what Yocto's CC carries too. CFLAGS are gcc's options,
    # not lcc's, and stay out; LDFLAGS are the distribution's link policy
    # (GNU hash, relro) and go to the gcc that lcc links with.
    ${TARGET_PREFIX}lcc --sysroot=${STAGING_DIR_TARGET} ${LDFLAGS} \
        -o ${B}/lcc-hello ${S}/hello.c
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${B}/lcc-hello ${D}${bindir}/lcc-hello
}
