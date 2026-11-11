# What the lcc recipes share: the source, its licence, and lcc's name for
# the target.

HOMEPAGE = "https://github.com/compilers-kiwon/lcc_llvm"
# lcc's own licence, CPYRIGHT: free to redistribute with that file, but
# lcc, and a product of which it is a significant part, may not be sold.
LICENSE = "LicenseRef-lcc"
NO_GENERIC_LICENSE[lcc] = "CPYRIGHT"
LIC_FILES_CHKSUM = "file://CPYRIGHT;md5=39e00b4ccb5ceedf0afbbec04b63c7bc"

SRC_URI = "git://github.com/compilers-kiwon/lcc_llvm.git;protocol=https;branch=master"
SRCREV = "14683712ca850c825bc6cc0785acf27eb5def870"
PV = "4.2+git"

# lcc's makefile runs in the source tree, with paths relative to it, and
# builds wherever BUILDDIR says.
B = "${WORKDIR}/build"
EXTRA_OEMAKE = "-C ${S} BUILDDIR=${B}"

# The target as lcc names it (rcc -target=llvm/LCCTARGET, etc/llvm-LCCTARGET.c
# and include/llvm/LCCTARGET): one of lcc's Linux targets, or none.
def lcc_target(d):
    arch = d.getVar('TARGET_ARCH')
    if arch == 'arm':
        # armhf-linux is AAPCS-VFP: hard float only
        return 'armhf-linux' if d.getVar('TARGET_FPU') == 'hard' else ''
    return {'x86_64': 'x86_64-linux', 'aarch64': 'aarch64-linux',
            'riscv64': 'riscv64-linux', 'riscv32': 'riscv32-linux'}.get(arch, '')

LCCTARGET = "${@lcc_target(d)}"

python () {
    if not d.getVar('LCCTARGET'):
        raise bb.parse.SkipRecipe("lcc has no back end for %s (%s)"
            % (d.getVar('TARGET_ARCH'), d.getVar('TARGET_FPU') or 'no FPU setting'))
}
