# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

add_mlir_library(LccOptPasses
  InlinePrep.cpp
  Pipelines.cpp
  RotateDoWhile.cpp
  UpliftWhile.cpp

  ADDITIONAL_HEADER_DIRS
  ${PROJECT_SOURCE_DIR}/include/lcc-opt

  DEPENDS
  LccOptPassIncGen

  EXCLUDE_FROM_LIBMLIR
  DISABLE_INSTALL
  )
mlir_target_link_libraries(LccOptPasses PUBLIC
  MLIRArithDialect
  MLIRArithToLLVM
  MLIRControlFlowToLLVM
  MLIRControlFlowToSCF
  MLIRDataLayoutInterfaces
  MLIRFuncDialect
  MLIRFuncToLLVM
  MLIRFunctionInterfaces
  MLIRLLVMDialect
  MLIRPass
  MLIRReconcileUnrealizedCasts
  MLIRSCFDialect
  MLIRSCFToControlFlow
  MLIRSCFTransforms
  MLIRTransforms
  MLIRUBToLLVM
  )
