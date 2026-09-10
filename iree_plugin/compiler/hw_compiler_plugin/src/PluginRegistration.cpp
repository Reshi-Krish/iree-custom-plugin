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
// SKELETON ONLY: buildTranslationPassPipeline runs no passes and
// serializeExecutable always emits zero exports - there is no op-matching
// pass yet to decide which dispatches map to which hw_emulator kernel. This
// step only proves the registration/serialization mechanics; see README.md
// in this directory for what's here, what's missing, and the design
// decisions behind both.

#include "iree/compiler/Dialect/HAL/Target/TargetRegistry.h"
#include "iree/compiler/PluginAPI/Client.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinTypes.h"

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
    // TODO: op-matching pass (recognize linalg.matmul etc and annotate the
    // variant with which hw_runtime_plugin kernel_registry.h name to
    // reference; fall through unmatched dispatches to another registered
    // backend, e.g. vmvx). No passes run yet, so serializeExecutable below
    // has nothing to name and always emits an empty executable.
  }

  LogicalResult serializeExecutable(const SerializationOptions &serOptions,
                                    IREE::HAL::ExecutableVariantOp variantOp,
                                    OpBuilder &executableBuilder) final {
    // TODO: once buildTranslationPassPipeline can annotate exports with a
    // matched kernel name, emit one fixed 32-byte NUL-terminated
    // kernel_name entry per export here (matching
    // iree_hal_hw_emulator_executable_export_t in
    // iree_plugin/hw_runtime_plugin/executable.h) instead of this always-
    // placeholder byte. A genuinely zero-length buffer isn't used here
    // because MLIR's VectorType rejects a zero-sized dimension.
    SmallVector<char> data(1, '\0');

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
