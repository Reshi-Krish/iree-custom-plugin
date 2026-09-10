# hw_emulator compiler plugin

This registers a HAL `TargetDevice` + `TargetBackend` pair under the id
`"hw_emulator"` so that `--iree-hal-target-device=hw_emulator` is accepted by
`iree-compile` and produces a `hal.executable.variant` tagged with format
`"{hw_emulator-executable}"` — the format the runtime driver
(`iree_plugin/hw_runtime_plugin`) already knows how to load (see
`executable_cache.c`'s `can_prepare_format` and `device.c`'s `query_i64` for
category `"hal.executable.format"`).

**Status: skeleton only.** `buildTranslationPassPipeline` runs no passes and
`serializeExecutable` always emits zero exports. This proves the
registration/serialization *mechanics* work end to end; it does not yet
recognize any ops. Compiling a real program and pointing it at
`--iree-hal-target-device=hw_emulator` will currently produce an executable
with nothing in it — the op-matching pass (recognize `linalg.matmul`, decide
when to fall back to another target like `vmvx`) is the next step, not this
one.

## What CUDA's plugin showed us

`compiler/plugins/target/CUDA/CUDATarget.cpp` is the reference for the
*registration* mechanics — the piece the generic
`samples/compiler_plugins/example` template doesn't cover (it never touches
`populateHALTargetDevices`/`populateHALTargetBackends` at all, so it's not
enough on its own to build a HAL target from).

- A single `PluginSession` overrides **both** `populateHALTargetDevices` and
  `populateHALTargetBackends`, registering the **same id string** (`"cuda"`)
  in both lists. We do the same with `"hw_emulator"`.
- `CUDATargetDevice::getDefaultDeviceTarget` is genuinely tiny (~20 lines):
  build a `DeviceTargetAttr` carrying the device id, and populate its
  executable-target list by asking the registry for the backend with the
  same id and calling *its* `getDefaultExecutableTargets`. There is no
  device-side logic beyond that — the backend is where all the real work
  (and all the complexity CUDA has, which we don't need) lives.
- CUDA needed its own `TargetDevice` (rather than piggybacking on an
  existing generic device, see VMVX below) because it's reached through its
  own dedicated HAL driver, not a shared local-CPU one. Same reasoning
  applies to us: `hw_emulator` is a standalone custom driver
  (`iree_plugin/hw_runtime_plugin`), so it needs its own device id too.

## What VMVX's plugin showed us

`compiler/plugins/target/VMVX/VMVXTarget.cpp` is the closer *behavioral*
reference — no real machine-code codegen, "just" serializes a data format —
but it's still not a perfect match for what we're building:

- VMVX registers **no `TargetDevice` of its own** — it rides on the generic
  `"local"` device (shared with `local-sync`/`local-task`). That only works
  because VMVX is reachable through those existing CPU HAL drivers. We can't
  do that; see above.
- `serializeExecutable` confirmed the actual API for embedding a custom
  binary format: build your bytes into a buffer, wrap them with
  `DenseIntElementsAttr::get(VectorType::get({n}, i8Type), bytes)`, and hand
  that to `IREE::HAL::ExecutableBinaryOp::create(builder, loc,
  variantOp.getSymName(), variantOp.getTarget().getFormat(), bufferAttr)`.
  This is the exact mechanism our own `serializeExecutable` uses (currently
  with `bytes` always empty).
- VMVX still runs a *real* pass pipeline (`buildVMVXTransformPassPipeline`
  down to VM bytecode via `translateModuleToBytecode`) — more machinery than
  we want. We have no arbitrary-code story at all: the "kernel" a dispatch
  maps to is always one of a small, fixed set of hand-written C functions
  (`hardware_kernels`), so our `buildTranslationPassPipeline` doesn't need a
  transform pipeline — just a future matching pass that decides "does this
  dispatch look like one of our known kernels?" and if so records which one.

**No single example matches our shape.** We're combining CUDA's
registration skeleton with VMVX's "just serialize a data blob, no codegen"
philosophy, and simplifying past even VMVX, since we don't serialize
arbitrary compiled code at all — just a name.

## Design decisions

- **`serializeExecutable` will emit our own tiny format, not Flatbuffers or
  VM bytecode.** The runtime side (`iree_plugin/hw_runtime_plugin/executable.h`)
  defines `executable_data` as a raw array of fixed-size, pointer-free
  structs — each one a 32-byte NUL-terminated kernel name
  (`iree_hal_hw_emulator_executable_export_t`), resolved at load time
  through a static kernel registry (`kernel_registry.h`) rather than
  embedding a function pointer (which would be meaningless across the
  compiler/runtime process boundary — see that header's comments for the
  full reasoning). Once the matching pass exists, `serializeExecutable`
  here will write exactly that struct layout, one entry per matched export,
  instead of today's always-empty placeholder.

- **The format string is duplicated, not shared, between the two plugins.**
  `IREE_HAL_HW_EMULATOR_EXECUTABLE_FORMAT` lives in the runtime plugin's C
  header; this compiler plugin is C++, built into a completely separate
  binary (`iree-compile` vs. whatever hosts the runtime driver), with no
  build-time relationship between the two. Rather than invent a shared
  header spanning both build graphs for one string constant, it's a
  `constexpr llvm::StringLiteral kExecutableFormat` here, kept in sync with
  the runtime side by comment and convention. **If either side's format
  string changes, the other must be updated by hand** — there is no
  compiler check that would catch a mismatch beyond a dispatch simply
  failing to find a compatible device at runtime.

- **No pass pipeline yet.** `buildTranslationPassPipeline` is intentionally
  empty right now rather than a placeholder that does something
  plausible-looking. The next real step is the op-matching pass described
  in the status note above.

## Next steps

1. Write the op-matching pass: recognize `linalg.matmul` (and eventually
   other ops as `hardware_kernels` grows) inside a dispatch region.
2. On a match, have `serializeExecutable` emit the real
   `iree_hal_hw_emulator_executable_export_t`-shaped bytes naming the
   matched kernel.
3. On *no* match, the pass should fail to match cleanly (not error out) so
   IREE's multi-target compilation falls back to another registered target
   (e.g. `vmvx`) for that dispatch — this is what makes "VMVX provides
   host-side execution and fallback" from the project proposal actually
   work.
4. End-to-end: compile a small MLIR program containing `linalg.matmul`
   through `iree-compile --iree-hal-target-device=hw_emulator`, run it via
   `iree-run-module --device=hw_emulator`, and confirm it produces the same
   result the runtime-only test (`queue_execute_matmul_smoke_test.c`)
   already proved by hand.
