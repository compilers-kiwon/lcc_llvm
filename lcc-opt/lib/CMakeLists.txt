# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

add_mlir_library(LccOptPasses
  AffineToLinalg.cpp
  CheckText.cpp
  DistinctSuccessors.cpp
  DistributeReductions.cpp
  ExpandShapes.cpp
  GlobalsToMemref.cpp
  IndexLoops.cpp
  InlinePrep.cpp
  LiftLLVMFunc.cpp
  MemcpyIntrinsic.cpp
  Pipelines.cpp
  RotateDoWhile.cpp
  ScfToAffine.cpp
  SplitAggregates.cpp
  UpliftWhile.cpp

  ADDITIONAL_HEADER_DIRS
  ${PROJECT_SOURCE_DIR}/include/lcc-opt

  DEPENDS
  LccOptPassIncGen

  EXCLUDE_FROM_LIBMLIR
  DISABLE_INSTALL
  )
mlir_target_link_libraries(LccOptPasses PUBLIC
  MLIRAffineAnalysis
  MLIRAffineDialect
  MLIRAffineToStandard
  MLIRArithDialect
  MLIRArithToLLVM
  MLIRControlFlowDialect
  MLIRControlFlowToLLVM
  MLIRControlFlowToSCF
  MLIRDataLayoutInterfaces
  MLIRFuncDialect
  MLIRFuncToLLVM
  MLIRFunctionInterfaces
  MLIRLinalgDialect
  MLIRLLVMDialect
  MLIRMemRefDialect
  MLIRMemRefToLLVM
  MLIRMemRefTransforms
  MLIRPass
  MLIRReconcileUnrealizedCasts
  MLIRSCFDialect
  MLIRSCFToControlFlow
  MLIRTransformUtils
  MLIRTransforms
  MLIRUBDialect
  MLIRUBToLLVM
  MLIRVectorToLLVMPass
  MLIRVectorToSCF
  )
