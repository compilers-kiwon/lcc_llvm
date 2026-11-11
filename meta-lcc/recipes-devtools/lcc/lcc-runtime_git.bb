SUMMARY = "liblcc, linked into the programs lcc compiles"
DESCRIPTION = "lcc's small run-time library: _YYnull, which code compiled \
with lcc -n calls on a null pointer, and lcc's profiling support. lcc's \
driver links it into every program."

require lcc.inc

# Code of lcc itself, linked into the target's programs: CPYRIGHT does not
# let it be sold as a significant part of a product.
LICENSE_FLAGS = "commercial"

# Built by the target's gcc, as the makefile's liblcc target has it.
do_compile() {
    oe_runmake CC="${CC}" CFLAGS="${CFLAGS}" \
        AR="${AR} ruv" RANLIB="${RANLIB}" liblcc
}

do_install() {
    install -d ${D}${libdir}
    install -m 0644 ${B}/liblcc.a ${D}${libdir}/
}

# A static library only: it lands in -staticdev and the main package is empty.
ALLOW_EMPTY:${PN} = "1"
