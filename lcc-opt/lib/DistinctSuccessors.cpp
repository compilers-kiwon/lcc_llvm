//===- DistinctSuccessors.cpp - Split repeated successors, in order -------===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// lcc-distinct-successors: see Passes.td.  It does what
// mlir::LLVM::ensureDistinctSuccessors (LegalizeForExport.cpp of MLIR 23)
// does when mlir-translate legalizes the module for export, the same new
// blocks for the same occurrences, but in the order of the successors.
// The MLIR function collects the repeated successors in a SmallDenseMap
// keyed by Block pointers and creates the new blocks iterating over it,
// so with two or more repeated successors in one terminator their order
// depends on the addresses of the blocks.
//
//===----------------------------------------------------------------------===//

#include "lcc-opt/Passes.h"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/Builders.h"
#include "llvm/ADT/SmallPtrSet.h"

namespace lcc {
#define GEN_PASS_DEF_LCCDISTINCTSUCCESSORS
#include "lcc-opt/Passes.h.inc"
} // namespace lcc

using namespace mlir;

/// Retargets the second and later occurrences of each successor with
/// arguments of the terminator of `block` to a new block, appended to the
/// region, that branches to the successor with its own arguments.
static void distinctSuccessors(Block &block) {
  if (block.empty() || !block.back().hasTrait<OpTrait::IsTerminator>())
    return;
  Operation *terminator = &block.back();
  unsigned n = terminator->getNumSuccessors();
  if (n < 2)
    return;
  // Only membership is asked of the set, never its order.
  llvm::SmallPtrSet<Block *, 4> seen;
  OpBuilder builder(terminator->getContext());
  for (unsigned i = 0; i < n; i++) {
    Block *successor = terminator->getSuccessor(i);
    // A block without arguments needs no PHI nodes, so it may repeat.
    if (successor->getNumArguments() == 0 || seen.insert(successor).second)
      continue;
    Block *dummy = builder.createBlock(block.getParent());
    for (BlockArgument arg : successor->getArguments())
      dummy->addArgument(arg.getType(), arg.getLoc());
    LLVM::BrOp::create(builder, terminator->getLoc(), dummy->getArguments(),
                       successor);
    terminator->setSuccessor(dummy, i);
  }
}

namespace {

struct DistinctSuccessorsPass
    : public lcc::impl::LccDistinctSuccessorsBase<DistinctSuccessorsPass> {
  void runOnOperation() override {
    // The blocks first: the pass appends new ones to their regions.
    SmallVector<Block *> blocks;
    getOperation()->walk([&](Operation *op) {
      for (Region &region : op->getRegions())
        for (Block &block : region)
          blocks.push_back(&block);
    });
    for (Block *block : blocks)
      distinctSuccessors(*block);
  }
};

} // namespace
