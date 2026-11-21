# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

set(LLVM_TARGET_DEFINITIONS Passes.td)
mlir_tablegen(Passes.h.inc -gen-pass-decls -name Lcc)
add_public_tablegen_target(LccOptPassIncGen)
