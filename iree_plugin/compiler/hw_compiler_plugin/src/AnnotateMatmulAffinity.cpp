// Copyright 2024 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Automatically assigns hw_emulator device affinity to any function
// containing a linalg.matmul, replacing the hand-written
// `stream.affinity = #hal.device.promise<@hw>` annotation that
// test/two_device_affinity.mlir used to prove the routing mechanism works
// (see that file and README.md for why routing has to happen this early -
// before HAL/MaterializeInterfaces ever sees the program - rather than
// inside HWEmulatorTargetBackend).
//
// Runs via extendPreprocessingPassPipeline, i.e. before Flow dispatch-region
// formation, so `linalg.matmul` is still visible as such (not yet grouped
// into a dispatch region) and `stream.affinity` on the enclosing function is
// exactly the annotation the `annotate_affinities.mlir`/schedule_execution
// tests show being read and propagated by IREE's own Stream-level affinity
// solver.
//
// LIMITATION (known, not yet handled): this annotates the *whole enclosing
// function* for hw_emulator as soon as it contains at least one
// linalg.matmul anywhere in it. A function that mixes a matmul with other,
// unsupported ops would incorrectly route all of it to hw_emulator (which
// cannot execute anything but matmul) instead of splitting the two. Fine for
// the current milestone (one function == one op); revisit with per-op
// (rather than per-function) affinity annotation - propagated the same way
// annotate_affinities.mlir shows for individual ops - once mixed-content
// functions need to be supported.
//
// IMPORTANT #1: by the time extendPreprocessingPassPipeline runs, the
// ABI-import phase (which runs before Preprocessing) has already rewritten
// public `func.func` entry points into `util.func` - confirmed empirically
// via `--compile-to=preprocessing` (an earlier version of this pass walked
// mlir::func::FuncOp, matched nothing at this phase, and silently annotated
// zero functions). We match the enclosing function generically via
// mlir::FunctionOpInterface (implemented by both func.func and util.func, and
// the pattern IREE's own passes use for exactly this phase-independence
// reason - see e.g. Stream/Transforms/AnnotateDispatchAssumptions.cpp) so
// this keeps working regardless of which concrete function op is present.
//
// IMPORTANT #2: we annotate with #hal.device.affinity<@hw>, NOT
// #hal.device.promise<@hw>, even though the latter is what a genuinely
// upfront (present in the original input, before any compiler pass runs)
// annotation would use (see two_device_affinity.mlir). Promises exist for
// annotations that predate the named device's declaration; ours doesn't
// qualify. Empirically confirmed (2026-09-10) that
// IREE::HAL::buildHALDeviceAssignmentPassPipeline - which creates the
// `@hw`/`@cpu` !hal.device globals from --iree-hal-target-device= AND runs
// ResolveDevicePromisesPass, resolving any #hal.device.promise present at
// that point into #hal.device.affinity - runs between the ABI and
// Preprocessing phases, i.e. strictly *before* this pass. A promise attached
// here would never be seen by that resolution step and stays a dangling,
// invalid promise forever (verified: produced "op references a promised
// device that was not declared" errors downstream in Stream). By the time we
// run, `@hw` already exists as a real global, so we can and must reference
// it directly as a resolved affinity instead.

#include "iree/compiler/Dialect/HAL/IR/HALTypes.h"
#include "mlir/Dialect/Linalg/IR/Linalg.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/FunctionInterfaces.h"
#include "mlir/Pass/Pass.h"

namespace mlir::iree_compiler::IREE::HAL {
namespace {

// Must match the device name given to --iree-hal-target-device=hw=hw_emulator
// at compile time (see test/two_device_affinity.mlir and README.md) - this
// must already be a declared !hal.device global by the time this pass runs
// (see IMPORTANT #2 above).
constexpr llvm::StringLiteral kHwEmulatorDeviceName = "hw";

struct AnnotateMatmulAffinityPass
    : public PassWrapper<AnnotateMatmulAffinityPass,
                         OperationPass<ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(AnnotateMatmulAffinityPass)

  StringRef getArgument() const final {
    return "hw-emulator-annotate-matmul-affinity";
  }
  StringRef getDescription() const final {
    return "Assigns hw_emulator device affinity to functions containing a "
           "linalg.matmul";
  }

  void runOnOperation() final {
    ModuleOp moduleOp = getOperation();
    auto deviceSymbolRef =
        SymbolRefAttr::get(&getContext(), kHwEmulatorDeviceName);
    auto affinityAttr =
        DeviceAffinityAttr::get(&getContext(), deviceSymbolRef,
                                /*queueMask=*/-1);
    bool matched = false;
    moduleOp.walk([&](linalg::MatmulOp matmulOp) {
      auto parentFuncOp =
          matmulOp->getParentOfType<mlir::FunctionOpInterface>();
      if (parentFuncOp) {
        parentFuncOp->setAttr("stream.affinity", affinityAttr);
        matched = true;
      }
    });
    // If we matched something but there's no `@hw` global to reference (e.g.
    // the user forgot --iree-hal-target-device=hw=hw_emulator), fail loudly
    // here rather than letting a confusing verifier error surface far
    // downstream in Stream lowering.
    if (matched &&
        !SymbolTable::lookupNearestSymbolFrom(moduleOp, deviceSymbolRef)) {
      moduleOp.emitError()
          << "found a linalg.matmul to route to device @"
          << kHwEmulatorDeviceName
          << " but no such device is declared; pass "
             "--iree-hal-target-device=hw=hw_emulator (plus another device "
             "for everything else, e.g. --iree-hal-target-device=cpu=local "
             "--iree-hal-local-target-device-backends=vmvx)";
      return signalPassFailure();
    }
  }
};

} // namespace

std::unique_ptr<Pass> createAnnotateMatmulAffinityPass() {
  return std::make_unique<AnnotateMatmulAffinityPass>();
}

} // namespace mlir::iree_compiler::IREE::HAL
