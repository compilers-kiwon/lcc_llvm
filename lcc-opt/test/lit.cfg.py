# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# Configuration file for the lit test runner of lcc-opt.

import os

import lit.formats
from lit.llvm import llvm_config
from lit.llvm.subst import ToolSubst

config.name = "lcc-opt"
# lit's internal shell: the lit of LLVM 23 rejects an external one.
config.test_format = lit.formats.ShTest()
config.suffixes = [".mlir"]
config.test_source_root = os.path.dirname(__file__)
config.test_exec_root = os.path.join(config.lcc_opt_obj_root, "test")

# FileCheck, count and not of the MLIR's own LLVM (llvm_tools_dir, from
# LLVM_TOOLS_BINARY_DIR), and lcc-opt of this build, by full path.
llvm_config.use_default_substitutions()
llvm_config.add_tool_substitutions(
    [ToolSubst("lcc-opt", unresolved="fatal")], [config.lcc_opt_tools_dir]
)
