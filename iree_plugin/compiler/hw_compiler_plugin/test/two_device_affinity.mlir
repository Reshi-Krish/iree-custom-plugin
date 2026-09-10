// Hand-annotated proof-of-concept for routing specific functions to specific
// HAL target devices via explicit `stream.affinity`, ahead of writing an
// automatic op-matching pass. See ../README.md's "Next steps" and the
// project memory notes for the research this is based on: IREE's compiler
// has NO automatic per-op fallback between target backends (a backend's
// buildTranslationPassPipeline failing aborts the whole compilation) - which
// backend handles a dispatch is decided purely by which *device* it's
// affinity-scoped to, and that has to be decided before HAL ever sees it.
//
// @on_hw is annotated for the "hw" device (-> hw_emulator backend) and
// contains linalg.matmul - the one op hw_emulator will eventually recognize.
// @on_cpu is annotated for the "cpu" device (-> vmvx, via the generic
// "local" device) and contains something hw_emulator never will.
//
// This is NOT a lit/FileCheck test (no lit config in this out-of-tree
// plugin) - it's meant to be run by hand with the commands below. Confirmed
// working manually on 2026-09-10:
//   - `--compile-to=stream` shows both functions' ops correctly tagged with
//     their own device's `#hal.device.affinity<@hw>` / `<@cpu>`.
//   - `--compile-to=hal` reaches HWEmulatorTargetBackend::serializeExecutable
//     for @on_hw's dispatch only (proving @on_cpu's addf dispatch never
//     reaches the hw_emulator backend at all) - this crashed on a real bug
//     (VectorType::get with a zero-length dimension in the still-empty
//     serializeExecutable placeholder), now fixed in PluginRegistration.cpp.
//
// Run (from this directory or anywhere, adjusting the path to this file):
//   ITOOLS=/home/mcw/Desktop/IREE-internal/iree-build/tools
//   $ITOOLS/iree-compile \
//       --iree-hal-target-device=hw=hw_emulator \
//       --iree-hal-target-device=cpu=local \
//       --iree-hal-local-target-device-backends=vmvx \
//       --compile-to=stream \
//       two_device_affinity.mlir -o /tmp/stream_out.mlir
//   # Inspect /tmp/stream_out.mlir: every op from @on_hw should carry
//   # on(#hal.device.affinity<@hw>), every op from @on_cpu should carry
//   # on(#hal.device.affinity<@cpu>).
//
//   $ITOOLS/iree-compile \
//       --iree-hal-target-device=hw=hw_emulator \
//       --iree-hal-target-device=cpu=local \
//       --iree-hal-local-target-device-backends=vmvx \
//       --compile-to=hal \
//       two_device_affinity.mlir -o /tmp/hal_out.mlir
//   # Inspect /tmp/hal_out.mlir: @on_hw's executable should have exactly one
//   # hal.executable.variant, target_backend = "hw_emulator"; @on_cpu's
//   # executable should have exactly one variant, target_backend = "vmvx".
//   # Neither executable should have a variant for the other backend.
//
// (Requires iree-compile to have been rebuilt after any PluginRegistration.cpp
// change - see project_build_env_constraints memory note: relink this
// yourself rather than backgrounding it, this machine's RAM is tight.)

func.func @on_hw(%arg0: tensor<2x2xf32>, %arg1: tensor<2x2xf32>) -> tensor<2x2xf32> attributes {
  stream.affinity = #hal.device.promise<@hw>
} {
  %init = tensor.empty() : tensor<2x2xf32>
  %cst = arith.constant 0.0 : f32
  %filled = linalg.fill ins(%cst : f32) outs(%init : tensor<2x2xf32>) -> tensor<2x2xf32>
  %result = linalg.matmul ins(%arg0, %arg1 : tensor<2x2xf32>, tensor<2x2xf32>)
      outs(%filled : tensor<2x2xf32>) -> tensor<2x2xf32>
  return %result : tensor<2x2xf32>
}

func.func @on_cpu(%arg0: tensor<4xf32>) -> tensor<4xf32> attributes {
  stream.affinity = #hal.device.promise<@cpu>
} {
  %result = arith.addf %arg0, %arg0 : tensor<4xf32>
  return %result : tensor<4xf32>
}
