#!/bin/bash
# yocto-layer-check.sh layer [workdir] - parse meta-lcc against
# openembedded-core and check what its recipes resolve to.
#
# A real build compiles a cross toolchain and LLVM and takes hours; parsing
# takes seconds and still catches the layer going stale: a recipe that stops
# resolving, a variable openembedded-core renamed, a LAYERSERIES_COMPAT that
# no longer exists.  The build itself is meta-lcc/scripts/check-tests.sh.
#
#     .github/scripts/yocto-layer-check.sh "$PWD/meta-lcc" /tmp/oe
set -eo pipefail

LAYER=${1:?usage: yocto-layer-check.sh layer [workdir]}
WORK=${2:-$PWD/oe}

banner() { echo; echo "--- $* ---"; }

banner "bitbake and openembedded-core"
mkdir -p "$WORK"
cd "$WORK"
[ -d bitbake ]           || git clone -q --depth 1 https://git.openembedded.org/bitbake
[ -d openembedded-core ] || git clone -q --depth 1 https://git.openembedded.org/openembedded-core
echo "bitbake           $(git -C bitbake rev-parse --short HEAD)"
echo "openembedded-core $(git -C openembedded-core rev-parse --short HEAD)"

banner "build directory"
export BITBAKEDIR="$WORK/bitbake"
# shellcheck disable=SC1091
source openembedded-core/oe-init-build-env build > /dev/null
grep -q lcc-check conf/local.conf || cat >> conf/local.conf <<'CONF'
# lcc-check
SANITY_TESTED_DISTROS = ""
LICENSE_FLAGS_ACCEPTED += "commercial_lcc-runtime"
LCC_BACKENDS = "llvm mlir"
CONF

banner "add the layer"
bitbake-layers show-layers | grep -q "^lcc " ||
    bitbake-layers add-layer "$LAYER"

banner "parse every recipe"
bitbake -p

banner "what the recipes resolve to"
fails=0
has() {
    if grep -q -- "$1" "$2"; then
        echo "  ok   $3"
    else
        echo "ERROR: $3"
        fails=$((fails + 1))
    fi
}

for r in lcc-cross-x86_64 lcc-runtime lcc-hello mlir-native; do
    bitbake -e "$r" > "$WORK/$r.env"
    has "^PN=\"$r\"" "$WORK/$r.env" "PN is $r"
done

# the compiler is a cross tool of x86_64 that needs llc and, with mlir, MLIR
has 'CLASSOVERRIDE="class-cross"'  "$WORK/lcc-cross-x86_64.env" "lcc-cross is a cross recipe"
has '^LCCTARGET="x86_64-linux"'     "$WORK/lcc-cross-x86_64.env" "lcc-cross targets x86_64-linux"
has '^DEPENDS=.*llvm-native'        "$WORK/lcc-cross-x86_64.env" "lcc-cross depends on llvm-native"
has '^DEPENDS=.*mlir-native'        "$WORK/lcc-cross-x86_64.env" "with mlir, lcc-cross depends on mlir-native"
has 'OEHOST=.*llvm-x86_64-linux.c'  "$WORK/lcc-cross-x86_64.env" "the driver takes llvm-x86_64-linux.c"
# the runtime is the target's and carries lcc's licence flag
has '^LICENSE="LicenseRef-lcc"'     "$WORK/lcc-runtime.env"      "lcc-runtime's licence is lcc's"
has '^LICENSE_FLAGS="commercial"'   "$WORK/lcc-runtime.env"      "lcc-runtime is flagged commercial"
has '^EXTRA_OEMAKE=.*-r -C'         "$WORK/lcc-runtime.env"      "make runs without built-in rules"
# the example uses the toolchain's lcc
has '^DEPENDS=.*lcc-cross-x86_64'   "$WORK/lcc-hello.env"        "lcc-hello depends on lcc-cross"
has '^DEPENDS=.*lcc-runtime'        "$WORK/lcc-hello.env"        "lcc-hello depends on lcc-runtime"
# MLIR is built standalone against llvm-native, with the two tools
has '^DEPENDS=.*llvm-native'        "$WORK/mlir-native.env"      "mlir-native depends on llvm-native"
has 'OECMAKE_SOURCEPATH=.*/mlir"'   "$WORK/mlir-native.env"      "mlir-native builds the mlir directory"
has 'mlir-opt mlir-translate'       "$WORK/mlir-native.env"      "mlir-native builds mlir-opt and mlir-translate"

echo
if [ "$fails" -ne 0 ]; then
    echo "ERROR: $fails check(s) failed"
    exit 1
fi
echo "all checks passed"
