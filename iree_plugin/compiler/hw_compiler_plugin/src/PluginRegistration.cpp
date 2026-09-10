// Copyright 2024 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Registers a HAL TargetDevice and TargetBackend under the id "hw_emulator"
// so that `--iree-hal-target-device=hw_emulator` is accepted and produces a
// `hal.executable.variant` tagged with format "{hw_emulator-executable}" -
// the format the hw_emulator runtime driver (iree_plugin/hw_runtime_plugin)
// already knows how to load (see executable_cache.c's can_prepare_format and
// device.c's query_i64 for "hal.executable.format").
//
// Device placement (which dispatch goes to hw_emulator vs. another target
// like vmvx) is decided upstream of this file entirely, by
// AnnotateMatmulAffinity.cpp running during preprocessing (see that file):
// IREE's compiler has no per-op fallback between target backends, so
// buildTranslationPassPipeline/serializeExecutable below can assume any
// dispatch reaching them is already a matmul (routing happened earlier).
// buildTranslationPassPipeline still runs no passes (nothing to lower - see
// README.md), and serializeExecutable emits the real
// iree_hal_hw_emulator_executable_export_t-shaped bytes for the "matmul"
// kernel rather than the earlier placeholder byte.

#include <array>
#include <cstring>

#include "AnnotateMatmulAffinity.h"
#include "ResolveTrivialWorkgroupCount.h"
#include "iree/compiler/Dialect/HAL/Target/TargetRegistry.h"
#include "iree/compiler/PluginAPI/Client.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/Linalg/IR/Linalg.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/SymbolTable.h"

namespace mlir::iree_compiler::IREE::HAL {
namespace {

// Must match IREE_HAL_HW_EMULATOR_EXECUTABLE_FORMAT in
// iree_plugin/hw_runtime_plugin/executable.h. The compiler plugin and the
// runtime plugin are built into entirely separate binaries (iree-compile vs.
// whatever hosts the runtime driver) with no shared header between them, so
// this string is duplicated by convention rather than shared - see
// README.md for why, and keep the two in sync by hand if either changes.
constexpr llvm::StringLiteral kExecutableFormat = "{hw_emulator-executable}";
constexpr llvm::StringLiteral kTargetId = "hw_emulator";

struct HWEmulatorOptions {
  void bindOptions(OptionsBinder &binder) {}
};

// Finds the single linalg.matmul inside |exportOp|'s corresponding function
// in |moduleOp| and returns its {m, n, k} shape. Every export reaching this
// backend is guaranteed to contain exactly one linalg.matmul with fully
// static operand shapes (see AnnotateMatmulAffinity.cpp: routing only
// happens for functions containing one, and IREE requires static shapes for
// this project's dispatches - see executable.h's kernel_params writeup for
// why dynamic shapes are out of scope).
static FailureOr<std::array<int64_t, 3>>
computeMatmulShape(ModuleOp moduleOp, ExecutableExportOp exportOp) {
  auto funcOp = dyn_cast_or_null<mlir::func::FuncOp>(
      SymbolTable::lookupSymbolIn(moduleOp, exportOp.getSymNameAttr()));
  if (!funcOp) {
    return exportOp.emitError()
           << "no function found matching export '" << exportOp.getSymName()
           << "'";
  }
  linalg::MatmulOp matmulOp;
  funcOp.walk([&](linalg::MatmulOp op) { matmulOp = op; });
  if (!matmulOp) {
    return exportOp.emitError()
           << "export '" << exportOp.getSymName()
           << "' routed to hw_emulator but contains no linalg.matmul";
  }
  auto lhsType = dyn_cast<RankedTensorType>(matmulOp.getInputs()[0].getType());
  auto rhsType = dyn_cast<RankedTensorType>(matmulOp.getInputs()[1].getType());
  if (!lhsType || !rhsType || lhsType.isDynamicDim(0) ||
      lhsType.isDynamicDim(1) || rhsType.isDynamicDim(1)) {
    return matmulOp.emitError()
           << "hw_emulator requires a statically-shaped linalg.matmul";
  }
  return std::array<int64_t, 3>{lhsType.getDimSize(0), rhsType.getDimSize(1),
                                lhsType.getDimSize(1)};
}

class HWEmulatorTargetBackend final : public TargetBackend {
public:
  explicit HWEmulatorTargetBackend(const HWEmulatorOptions &options)
      : options(options) {}

  std::string getLegacyDefaultDeviceID() const final {
    return kTargetId.str();
  }

  void getDefaultExecutableTargets(
      MLIRContext *context, StringRef deviceID, DictionaryAttr deviceConfigAttr,
      SmallVectorImpl<IREE::HAL::ExecutableTargetAttr> &executableTargetAttrs)
      const final {
    Builder b(context);
    executableTargetAttrs.push_back(b.getAttr<IREE::HAL::ExecutableTargetAttr>(
        b.getStringAttr(kTargetId), b.getStringAttr(kExecutableFormat),
        b.getDictionaryAttr({})));
  }

  void buildTranslationPassPipeline(IREE::HAL::ExecutableTargetAttr targetAttr,
                                    OpPassManager &passManager) final {
    // No codegen needed: AnnotateMatmulAffinity.cpp (run during
    // preprocessing, well before HAL ever sees the program) guarantees every
    // dispatch reaching this backend is a linalg.matmul - there is nothing
    // left to transform, only a name to emit in serializeExecutable below.
    //
    // But every export still carries a `count` region seeded with a
    // `iree_tensor_ext.dispatch.workgroup_count_from_slice` placeholder,
    // which every real backend resolves as part of its codegen pipeline
    // (Codegen's ReconcileTranslationInfoPass + ResolveWorkgroupCountHintsPass
    // - tried here first, but both are no-ops without a prior "select
    // lowering strategy" pass having attached TranslationInfo, which we
    // never run). Left unresolved, the placeholder survives all the way to
    // VM conversion, which has no legalization pattern for it and crashes
    // with a materializeTargetConversion assertion - confirmed empirically
    // on 2026-09-10 (see ResolveTrivialWorkgroupCount.cpp for the full
    // writeup and the actual fix: hw_emulator's kernel ABI has no
    // tiling/grid concept, so we just resolve every export's count region to
    // a constant {1, 1, 1} directly instead of engaging Codegen's
    // general-purpose machinery).
    //
    // `passManager` here is scoped to `hal.executable.variant` itself (see
    // ResolveTrivialWorkgroupCount.cpp for why the pass must run at exactly
    // this scope, not nested into the inner builtin.module).
    passManager.addPass(createResolveTrivialWorkgroupCountPass());
  }

  LogicalResult serializeExecutable(const SerializationOptions &serOptions,
                                    IREE::HAL::ExecutableVariantOp variantOp,
                                    OpBuilder &executableBuilder) final {
    // One iree_hal_hw_emulator_executable_export_t-shaped entry per export:
    //  - a fixed 64-byte NUL-terminated export_name (the compiler-assigned
    //    `hal.executable.export` symbol, e.g.
    //    "on_hw_dispatch_0_matmul_2x2x2_f32" - matched at runtime against
    //    `hal.executable.lookup.function`'s name argument, which always
    //    resolves indirectly by this symbol even though it's statically
    //    known here; using the fixed kernel name "matmul" instead was a real
    //    bug, hit and fixed 2026-09-10 - see executable.h/executable.c),
    //  - a fixed 32-byte NUL-terminated kernel_name (see
    //    iree_plugin/hw_runtime_plugin/kernel_registry.h) - every export
    //    reaching this backend is a matmul (see buildTranslationPassPipeline
    //    above), so this is always "matmul",
    //  - a fixed 24-byte kernel_params blob: the matmul's static {m, n, k}
    //    packed as three native int64_t, matching hw_matmul_params_t's
    //    layout exactly (see executable.h - also hit and fixed 2026-09-10,
    //    same debugging session: without this the kernel ran with
    //    zeroed/garbage shape and failed "matmul dimensions must be
    //    positive").
    constexpr size_t kExportNameCapacity = 64;
    constexpr size_t kKernelNameCapacity = 32;
    constexpr size_t kKernelParamsCapacity = 24;
    constexpr size_t kEntrySize =
        kExportNameCapacity + kKernelNameCapacity + kKernelParamsCapacity;
    constexpr llvm::StringLiteral kMatmulKernelName = "matmul";
    static_assert(kMatmulKernelName.size() < kKernelNameCapacity,
                 "kernel name must leave room for the NUL terminator");
    static_assert(kKernelParamsCapacity == 3 * sizeof(int64_t),
                 "kernel_params capacity must match hw_matmul_params_t");

    ModuleOp innerModuleOp = variantOp.getInnerModule();
    SmallVector<char> data;
    for (auto exportOp : variantOp.getBlock().getOps<ExecutableExportOp>()) {
      StringRef exportName = exportOp.getSymName();
      if (exportName.size() >= kExportNameCapacity) {
        return exportOp.emitError()
               << "export name '" << exportName << "' (" << exportName.size()
               << " bytes) does not fit hw_emulator's fixed "
               << kExportNameCapacity << "-byte wire format field";
      }
      FailureOr<std::array<int64_t, 3>> shape =
          computeMatmulShape(innerModuleOp, exportOp);
      if (failed(shape)) {
        return failure();
      }

      size_t entryStart = data.size();
      data.resize(entryStart + kEntrySize, '\0');
      for (size_t i = 0; i < exportName.size(); ++i) {
        data[entryStart + i] = exportName[i];
      }
      size_t kernelNameStart = entryStart + kExportNameCapacity;
      for (size_t i = 0; i < kMatmulKernelName.size(); ++i) {
        data[kernelNameStart + i] = kMatmulKernelName[i];
      }
      size_t kernelParamsStart = kernelNameStart + kKernelNameCapacity;
      std::memcpy(&data[kernelParamsStart], shape->data(),
                 kKernelParamsCapacity);
    }
    if (data.empty()) {
      // No exports (shouldn't happen given affinity routing upstream, but
      // keep serialization itself well-defined): MLIR's VectorType rejects
      // a zero-length dimension, so fall back to a single placeholder byte.
      data.push_back('\0');
    }

    auto bufferAttr = DenseIntElementsAttr::get(
        VectorType::get({static_cast<int64_t>(data.size())},
                        IntegerType::get(executableBuilder.getContext(), 8)),
        ArrayRef<char>(data));

    // NOTE: this snapshots the builder state at the time it is called;
    // matches the pattern used by every in-tree TargetBackend.
    auto binaryOp = IREE::HAL::ExecutableBinaryOp::create(
        executableBuilder, variantOp.getLoc(), variantOp.getSymName(),
        variantOp.getTarget().getFormat(), bufferAttr);
    binaryOp.setMimeTypeAttr(
        executableBuilder.getStringAttr("application/octet-stream"));

    return success();
  }

private:
  const HWEmulatorOptions &options;
};

class HWEmulatorTargetDevice final : public TargetDevice {
public:
  explicit HWEmulatorTargetDevice(const HWEmulatorOptions &options) {}

  IREE::HAL::DeviceTargetAttr
  getDefaultDeviceTarget(MLIRContext *context,
                         const TargetRegistry &targetRegistry) const final {
    Builder b(context);

    // hw_emulator is a single, standalone device with exactly one executable
    // target (itself) - unlike VMVX, which piggybacks on the generic "local"
    // device, hw_emulator needs its own device id here because it is reached
    // through a custom HAL driver, not local-sync/local-task. See README.md.
    SmallVector<IREE::HAL::ExecutableTargetAttr> executableTargetAttrs;
    targetRegistry.getTargetBackend(kTargetId)->getDefaultExecutableTargets(
        context, kTargetId, b.getDictionaryAttr({}), executableTargetAttrs);

    return IREE::HAL::DeviceTargetAttr::get(context, b.getStringAttr(kTargetId),
                                            b.getDictionaryAttr({}),
                                            executableTargetAttrs);
  }
};

struct HWEmulatorSession final
    : PluginSession<HWEmulatorSession, HWEmulatorOptions,
                    PluginActivationPolicy::DefaultActivated> {
  void populateHALTargetDevices(IREE::HAL::TargetDeviceList &targets) final {
    // #hal.device.target<"hw_emulator", ...
    targets.add(kTargetId, [&]() {
      return std::make_shared<HWEmulatorTargetDevice>(options);
    });
  }
  void populateHALTargetBackends(IREE::HAL::TargetBackendList &targets) final {
    // #hal.executable.target<"hw_emulator", ...
    targets.add(kTargetId, [&]() {
      return std::make_shared<HWEmulatorTargetBackend>(options);
    });
  }
  void extendPreprocessingPassPipeline(OpPassManager &passManager) final {
    passManager.addPass(createAnnotateMatmulAffinityPass());
  }
};

} // namespace
} // namespace mlir::iree_compiler::IREE::HAL

IREE_DEFINE_COMPILER_OPTION_FLAGS(
    mlir::iree_compiler::IREE::HAL::HWEmulatorOptions);

extern "C" bool iree_register_compiler_plugin_hw_emulator(
    mlir::iree_compiler::PluginRegistrar *registrar) {
  registrar->registerPlugin<mlir::iree_compiler::IREE::HAL::HWEmulatorSession>(
      "hw_emulator");
  return true;
}
