// Copyright 2024 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Every `hal.executable.export` gets a `count` region containing a
// `iree_tensor_ext.dispatch.workgroup_count_from_slice` placeholder before
// any backend-specific translation runs. Real backends resolve it via
// Codegen's `ReconcileTranslationInfoPass` + `ResolveWorkgroupCountHintsPass`
// - but both of those only touch placeholders that got moved into an
// `iree_codegen.dispatch_config` op, which itself is only created for
// functions carrying a `TranslationInfo` attribute set by an earlier
// "select lowering strategy" pass (see
// Codegen/Common/ReconcileTranslationInfo.cpp's `configMap` handling).
// hw_emulator's `buildTranslationPassPipeline` never runs such a pass - we
// do no real codegen (see PluginRegistration.cpp) - so those two passes are
// silent no-ops for us and the placeholder survives untouched all the way
// to VM conversion, where it crashes (`materializeTargetConversion`
// assertion; confirmed empirically on 2026-09-10, see
// project_hw_emulator_overview.md memory for the full debugging trail).
//
// Since hw_emulator's kernel ABI has no tiling/grid concept (every dispatch
// is a single fixed-shape call into a named kernel; see
// hw_runtime_plugin/kernel_registry.h), the workgroup count is always
// trivially {1, 1, 1} - so instead of engaging Codegen's general-purpose
// (and, for us, non-functional) machinery, this pass just replaces any
// leftover `workgroup_count_from_slice` op directly in each export's count
// region with that constant.

#include "ResolveTrivialWorkgroupCount.h"

#include "iree/compiler/Dialect/HAL/IR/HALOps.h"
#include "iree/compiler/Dialect/TensorExt/IR/TensorExtOps.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/IR/Builders.h"

namespace mlir::iree_compiler::IREE::HAL {
namespace {

struct ResolveTrivialWorkgroupCountPass
    : public PassWrapper<ResolveTrivialWorkgroupCountPass,
                         OperationPass<IREE::HAL::ExecutableVariantOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(ResolveTrivialWorkgroupCountPass)

  StringRef getArgument() const final {
    return "hw-emulator-resolve-trivial-workgroup-count";
  }
  StringRef getDescription() const final {
    return "Replaces unresolved iree_tensor_ext.dispatch."
           "workgroup_count_from_slice ops in hal.executable.export count "
           "regions with a constant {1, 1, 1} (hw_emulator's kernel ABI has "
           "no tiling/grid concept)";
  }

  void runOnOperation() final {
    // NOTE: must run directly on the ExecutableVariantOp, NOT nested into
    // its inner `builtin.module` - `hal.executable.export` is a *sibling* of
    // that module (both are direct children of the variant), not nested
    // inside it, so a ModuleOp-scoped walk here would silently never find
    // it. Confirmed empirically on 2026-09-10 after nesting into ModuleOp
    // (matching AnnotateMatmulAffinity's unrelated ModuleOp-scoped pass)
    // turned out to be a no-op: `--compile-to=executable-targets` showed the
    // placeholder still present verbatim after this pass supposedly ran.
    IREE::HAL::ExecutableVariantOp variantOp = getOperation();
    variantOp.walk([&](IREE::HAL::ExecutableExportOp exportOp) {
      Region &countRegion = exportOp.getWorkgroupCount();
      if (countRegion.empty()) {
        return;
      }
      SmallVector<IREE::TensorExt::DispatchWorkgroupCountFromSliceOp>
          fromSliceOps;
      countRegion.walk(
          [&](IREE::TensorExt::DispatchWorkgroupCountFromSliceOp op) {
            fromSliceOps.push_back(op);
          });
      for (auto fromSliceOp : fromSliceOps) {
        OpBuilder builder(fromSliceOp);
        Location loc = fromSliceOp.getLoc();
        Value one = arith::ConstantIndexOp::create(builder, loc, 1);
        fromSliceOp->replaceAllUsesWith(ValueRange{one, one, one});
        fromSliceOp->erase();
      }
    });
  }
};

} // namespace

std::unique_ptr<Pass> createResolveTrivialWorkgroupCountPass() {
  return std::make_unique<ResolveTrivialWorkgroupCountPass>();
}

} // namespace mlir::iree_compiler::IREE::HAL
