# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

add_mlir_library(LccOptPasses
  Pipelines.cpp

  ADDITIONAL_HEADER_DIRS
  ${PROJECT_SOURCE_DIR}/include/lcc-opt

  EXCLUDE_FROM_LIBMLIR
  DISABLE_INSTALL
  )
mlir_target_link_libraries(LccOptPasses PUBLIC
  MLIRPass
  MLIRTransforms
  )
