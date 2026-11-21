# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# lcc-opt: an mlir-opt for the output of lcc's mlir targets.  Built out of
# tree like mlir/examples/standalone against an installed MLIR 23, by default
# the Debian/Ubuntu packages under /usr/lib/llvm-23.  Like the packaged
# mlir-opt it links libMLIR.so and libLLVM.so, never the static MLIR or LLVM
# libraries: mixing them would put a second copy of MLIR (and its registries)
# into the program.

cmake_minimum_required(VERSION 3.20)
project(lcc-opt LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17 CACHE STRING "C++ standard to conform to")
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(MLIR_DIR /usr/lib/llvm-23/lib/cmake/mlir CACHE PATH
  "Directory with MLIRConfig.cmake of MLIR 23")
find_package(MLIR REQUIRED CONFIG)
message(STATUS "Using MLIRConfig.cmake in: ${MLIR_DIR}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

if(NOT LLVM_VERSION_MAJOR EQUAL 23)
  message(FATAL_ERROR "lcc-opt needs MLIR 23, but ${MLIR_DIR} belongs to "
    "LLVM ${LLVM_PACKAGE_VERSION}; set MLIR_DIR to the lib/cmake/mlir "
    "directory of MLIR 23")
endif()
if(NOT MLIR_LINK_MLIR_DYLIB OR NOT LLVM_LINK_LLVM_DYLIB)
  message(FATAL_ERROR "lcc-opt links libMLIR.so and libLLVM.so, but the "
    "MLIR in ${MLIR_DIR} does not offer them (MLIR_LINK_MLIR_DYLIB="
    "${MLIR_LINK_MLIR_DYLIB}, LLVM_LINK_LLVM_DYLIB=${LLVM_LINK_LLVM_DYLIB})")
endif()

set(LLVM_RUNTIME_OUTPUT_INTDIR ${CMAKE_BINARY_DIR}/bin)
set(LLVM_LIBRARY_OUTPUT_INTDIR ${CMAKE_BINARY_DIR}/lib)
set(MLIR_BINARY_DIR ${CMAKE_BINARY_DIR})

list(APPEND CMAKE_MODULE_PATH "${MLIR_CMAKE_DIR}" "${LLVM_CMAKE_DIR}")
include(TableGen)
include(AddLLVM)
include(AddMLIR)
include(HandleLLVMOptions)

# lit runs with this Python.  LLVMConfig.cmake names llvm-lit, which the
# packages do not install; they install lit next to FileCheck.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(LLVM_EXTERNAL_LIT "${LLVM_TOOLS_BINARY_DIR}/lit" CACHE STRING
  "Command used to spawn lit")

include_directories(${LLVM_INCLUDE_DIRS})
include_directories(${MLIR_INCLUDE_DIRS})
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${PROJECT_BINARY_DIR}/include)
separate_arguments(LCC_OPT_LLVM_DEFINITIONS NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LCC_OPT_LLVM_DEFINITIONS})

add_subdirectory(include/lcc-opt)
add_subdirectory(lib)

set(LLVM_LINK_COMPONENTS Support)
add_llvm_executable(lcc-opt lcc-opt.cpp)
llvm_update_compile_flags(lcc-opt)
target_link_libraries(lcc-opt PRIVATE LccOptPasses)
mlir_target_link_libraries(lcc-opt PRIVATE
  MLIROptLib
  MLIRRegisterAllDialects
  MLIRRegisterAllExtensions
  MLIRRegisterAllPasses
  )
mlir_check_all_link_libraries(lcc-opt)

add_subdirectory(test)
