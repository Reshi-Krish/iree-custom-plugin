# Learnings: Integrating a Custom Accelerator into IREE

## What `hardware_emulator` is

`hardware_emulator` is a simple simulated device: a device-side memory pool
(a "buffer"), an asynchronous execute engine (run a kernel on a background
thread, expose a completion flag/status a host can poll), and a small,
fixed library of hand-written kernels (`hardware_kernels/`) that can be
invoked by name against buffers in that pool.

It exists as a proof of concept for a specific, common situation: a new DSP,
NPU, or other fixed-function accelerator whose vendor toolchain does not fit
into any of IREE's existing codegen pipelines (LLVM/CPU, SPIR-V, PTX, etc.)
- either because it has no general-purpose compiler at all (just a small set
of hand-written or vendor-provided kernels), or because integrating its real
compiler is a separate, larger effort. `hardware_emulator` stands in for
that hardware so this project could work through, concretely and completely,
what it actually takes to plug such a device into IREE end-to-end: a runtime
HAL driver so IREE can allocate memory on it and dispatch work to it, and a
compiler plugin so IREE's compiler knows when to route work to it.

## 1. Runtime plugin creation

### Workflow

The runtime plugin (`iree_plugin/hw_runtime_plugin/`) was built bottom-up, in
the order the HAL's own object graph depends on itself:

```
driver -> device -> buffer -> allocator -> semaphore
       -> executable + command buffer -> channel/event (closing work)
```

Each of these is a small vtable-based "object" - a common
`iree_hal_resource_t` base plus a type-checked cast (`IREE_HAL_ASSERT_TYPE`)
- that the generic IREE HAL API dispatches into. Building and testing in
this order meant each piece could be exercised against real IREE HAL calls
before the next one, which depends on it, was written.

### Central learning: handle ownership

The device stores the *one* real handle to the hardware; every other
component only ever borrows it. Concretely: `iree_hal_hw_emulator_device_t`
owns the single `hw_device_t*` for as long as the device exists. Buffers,
the allocator, command buffers, and the semaphore all receive a reference to
that same handle to do their work, but none of them owns it and none of
them destroys it - only the device does, when the device itself is torn
down. This is the natural shape for a real HAL driver wrapping real
hardware too: one handle to the physical device, borrowed (never owned) by
every surrounding HAL object.

### Components

- **Driver** - registers the available hardware runtime. This is the entry
  point (`driver_module.c`, registered via
  `iree_register_external_hal_driver`) that IREE's driver registry calls to
  enumerate what devices of this kind exist and to create one.
- **Device** - contains the main handle to the hardware, which it
  initializes (creating/owning the single `hw_device_t*` for its lifetime).
  It's also where a HAL implementation answers "what can you do" - e.g.
  which executable formats it accepts, exposed through capability queries
  like `hal.device.id` / `hal.executable.format`.
- **Buffer** - defines the properties of memory that lives in the device's
  buffer/memory pool: how a region within that pool is described (offset,
  length, usage/type flags) so the rest of the HAL can reference it
  uniformly, regardless of what "memory" actually means on the real device.
- **Allocator** - collects the handle from the device and uses it to
  allocate (and import/export) device-specific buffers. It's the thing that
  actually turns "give me N bytes visible to the device" into a real
  reservation against the device's memory pool.
- **Semaphore** - queries the device for execution completion, to manage
  IREE's timelines. This is the piece that bridges IREE's generic
  timeline/timepoint abstraction (used everywhere in the HAL to express
  "wait until this work is done") to whatever this specific device actually
  exposes as a completion signal.
- **Executable and command buffer** - manage loading the executable (which
  kernel(s) a compiled artifact names) and issuing dispatches against it -
  resolving a compiled reference to an actual kernel to run, and driving the
  device through one recorded (or immediately-applied) sequence of work.

### Cross-process constraint: no raw pointers in a compiled artifact

A compiled artifact (the `.vmfb` `iree-compile` produces) is a static blob of
bytes that is written out by one process (the compiler) and loaded, often
much later and always in a separate process, by another (the runtime). That
sounds obvious once stated, but it has a concrete consequence for any HAL
backend whose "kernel" is just a function living in the runtime process:
**the compiler cannot embed a pointer to it.** A function pointer is only
ever meaningful within the address space of the process that produced it -
ASLR alone guarantees it will not point at the right code (if anything
mapped at all) in a different process loading the same bytes later.

This project's first pass at the executable wire format did exactly that -
embedded a raw `hw_kernel_fn_t` pointer directly in `executable_data` -
before this was caught. The fix was to make the wire format identify a
kernel *by name* instead: the compiler writes a fixed-size, NUL-terminated
string naming one of a small, fixed set of kernels (see
`hardware_kernels/`), and the runtime resolves that name to a real function
pointer itself, at executable-load time, against its own statically-linked
`kernel_registry.c` table. Nothing pointer-shaped ever crosses the
compiler/runtime boundary - only plain data (names, and later, static shape
metadata for the matmul kernel's `m`/`n`/`k`), which is exactly what a wire
format has to be limited to once you take seriously that the two sides of
it run as different processes, quite possibly on different machines
entirely.

This is also why workflow 1's "executable" is not a self-contained portable
artifact the way a real compiled binary would be: it only runs against a
runtime process that happens to have this exact kernel library linked in.
Workflow 2 (an executable containing a vendor compiler's actual compiled
output) would not have this limitation - the bytes it embeds are genuinely
loadable code, not just a name a matching runtime happens to recognize.

## 2. Compiler plugin creation

There are two different workflows for making IREE's *compiler* aware of new
hardware:

1. **Set up a kernel library and add passes that map MLIR operators onto
   it.** The hardware only runs a small, fixed set of kernels (not arbitrary
   compiled code); the compiler's job is to recognize which ops in the input
   program those kernels can handle and route them to the new device -
   there is no real code generation involved.
2. **Map an existing codegen framework into the new HAL.** The hardware (or
   its vendor SDK) has its own real compiler/code generator; the work is to
   plug that existing framework in as IREE's "last mile" lowering step for a
   new HAL target, so IREE can drive it like any other backend.

This project focuses entirely on workflow 1.

### Workflow 1: kernel library + operator-matching passes (what we built)

- A HAL `TargetDevice` and `TargetBackend` are registered under a fixed id
  (`hw_emulator`) via `populateHALTargetDevices`/`populateHALTargetBackends` -
  the same registration mechanism any HAL backend uses.
- **Key finding that shaped everything else:** IREE's compiler has *no*
  automatic per-op fallback between target backends. Which backend a
  dispatch is translated by is decided purely by which *device* it is
  affinity-scoped to - a static, per-device, op-blind decision made by
  `MaterializeInterfaces` before any backend ever sees the dispatch. If a
  backend's `buildTranslationPassPipeline` fails on a dispatch it's been
  given, the *entire compilation* aborts; there is no hook to decline a
  dispatch and no retry-with-a-different-backend mechanism. This means
  routing ("which ops go to the new hardware") has to happen *upstream* of
  the backend entirely, via explicit device affinity on the program, not by
  hoping the new backend's translation gracefully fails for
  unsupported ops.
- An `AnnotateMatmulAffinity` pass does that routing: it walks the input for
  the op(s) our kernel library supports (`linalg.matmul`) and tags the
  enclosing function with a device affinity pointing at our device. It runs
  early (`extendPreprocessingPassPipeline`, before Flow dispatch-region
  formation) but still after IREE's own HAL device-assignment pipeline has
  already materialized the named device globals - a real timing bug we hit
  and fixed was using the wrong affinity attribute (`#hal.device.promise`,
  meant for annotations that predate a device's declaration) instead of the
  right one (`#hal.device.affinity`, for referencing an already-declared
  device) for this pass's position in the pipeline.
- Because there is no real codegen - the hardware only ever runs one of a
  handful of hand-written kernels - `buildTranslationPassPipeline`'s job is
  not to lower the dispatch to machine code at all. It only has to finish
  the bookkeeping IREE requires of *every* backend regardless of whether it
  does codegen (e.g. resolving the workgroup-count placeholder every
  dispatch gets). `serializeExecutable` then emits a small custom wire
  format naming which fixed kernel to run, plus enough static shape metadata
  for that kernel to execute correctly (e.g. a matmul's m/n/k) - not any
  actual compiled kernel body. That name is resolved against a statically
  linked kernel library in the runtime process.
- The kernel library itself (`hardware_kernels/`) is just hand-written C
  functions with a small, fixed ABI (inputs, outputs, an opaque params
  struct) - one entry per operator the annotate/routing pass knows how to
  recognize.
- Net effect: for this workflow, "writing a compiler plugin" is mostly (a) a
  small number of MLIR passes that recognize supported ops and assign them
  to the new device, (b) a target device/backend registration that is
  almost entirely bookkeeping rather than lowering, and (c) a
  name-plus-shape handoff protocol to the runtime, rather than a real code
  generator.

### Workflow 2: mapping an existing codegen framework into the new HAL (not explored here)

This project did not build this, but IREE's plugin architecture is
explicitly designed to support it too, using the *same* `TargetBackend`
extension point workflow 1 used:

- IREE's own documentation describes the HAL Target plugin point as being
  for "last-mile lowering of device executables to a concrete platform," and
  says such a plugin should depend on "the core code generation facilities
  of the LLVM project, the lowest level/corresponding parts of the vendor
  SDK, or low-level device-side processor support libraries that can be
  packaged with the compiler."
- This is exactly what IREE's in-tree backends already do, through this
  same interface: LLVMCPU lowers through LLVM's own codegen, CUDA/HIP
  through NVVM/ROCDL and NVPTX/LLVM, Vulkan/Metal through SPIR-V tooling.
  Our `buildTranslationPassPipeline` (left empty in this project) is the
  identical hook a "real" backend fills with an entire external compiler.
- IREE's Codegen dialect guidance for adding a genuinely new codegen path is
  to add lowering patterns under `iree/compiler/Codegen/`, and to prefer
  emitting target-specific code mixed with IREE-compiler-generated code at
  the *highest* level of abstraction the target pipeline reaches, rather
  than reinventing everything from the bottom.
- A real-world example of this exact pattern (from general knowledge, not
  independently verified during this session - worth confirming
  directly before relying on it): AMD's `iree-amd-aie` project integrates
  AMD's AI Engine (AIE) compiler stack as an out-of-tree IREE HAL
  target/backend/device plugin, lowering IREE's tensor/Linalg-level IR
  through AIE-specific codegen passes to produce a real compiled AIE
  binary - i.e. wrapping an existing, separate vendor codegen framework
  behind IREE's plugin interface, rather than a fixed hand-written kernel
  library as this project does.
- Takeaway for a real DSP/NPU with its own vendor compiler: workflow 2 would
  mean writing a `buildTranslationPassPipeline` that lowers as far as IREE's
  own dialects reasonably go, then hands off to the vendor toolchain (as a
  library call, a subprocess invocation, or an MLIR translation pass into
  the vendor's own IR) for the actual "last mile" codegen - with
  `serializeExecutable` embedding that toolchain's real compiled output,
  rather than a name string like this project's.

### Further reading

- [IREE - Proposal to Build IREE Compiler Plugin Mechanism (Issue #12520)](https://github.com/iree-org/iree/issues/12520)
- [IREE HAL dialect reference](https://iree.dev/reference/mlir-dialects/HAL/)
- [IREE Codegen dialect reference](https://iree.dev/reference/mlir-dialects/IREECodegen/)
- [IREE extensions overview](https://iree.dev/reference/extensions/)
