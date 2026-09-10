# iree-custom-plugin

An IREE integration for a simulated low-power AI accelerator ("hw_emulator").
This is a learning project demonstrating the full path:

```
linalg.matmul -> automatic device-affinity routing -> custom HAL dispatch
             -> hw_emulator -> custom matmul kernel
```

while everything else in a program (any op other than `linalg.matmul`) falls
back to IREE's built-in VMVX backend running on the host CPU. Routing is
fully automatic - a compiler pass detects `linalg.matmul` and assigns it to
the emulator device, with no hand-written device annotations required in the
input MLIR.

This has been verified end-to-end: compiling a two-function MLIR program
with `iree-compile` and executing it with `iree-run-module` produces the
mathematically correct matmul result on `hw_emulator` and the correct result
for the other function on VMVX, in the same run.

## Repository layout

```
hardware_emulator/     Plain C "hardware" model (memory pool, async execute,
                       completion-flag polling) - no IREE dependency.
hardware_kernels/      Hand-written kernels the emulator can run (matmul,
                       a no-op template) with a small fixed ABI.
iree_plugin/
  hw_runtime_plugin/    The IREE HAL driver (runtime side) - registers
                       "hw_emulator" as a HAL driver/device.
  compiler/
    hw_compiler_plugin/ The IREE compiler plugin (compiler side) - registers
                       "hw_emulator" as a HAL target device/backend and the
                       pass that auto-routes matmuls to it.
tests/                 Standalone unit tests for hardware_emulator/kernels,
                       independent of IREE.
```

Each subdirectory has its own `README.md` with implementation-level detail
and design-decision writeups; this file covers the project as a whole and
how to build and run it from scratch.

- [hardware_emulator/README.md](hardware_emulator/README.md)
- [hardware_kernels/README.md](hardware_kernels/README.md)
- [iree_plugin/hw_runtime_plugin/README.md](iree_plugin/hw_runtime_plugin/README.md)
- [iree_plugin/compiler/hw_compiler_plugin/README.md](iree_plugin/compiler/hw_compiler_plugin/README.md)

## Prerequisites

- `git`
- CMake >= 3.26
- Ninja
- A C/C++ compiler (developed with `clang`/`clang++`; GCC should also work)
- Python 3 (needed by IREE's own build)
- Enough disk space for a full IREE + LLVM checkout and build (double-digit
  GB) and a good amount of RAM/swap for linking `iree-compile` (see note
  below)

## 1. Clone IREE and this repository

IREE and this repository are cloned as sibling directories:

```bash
mkdir -p ~/iree-work && cd ~/iree-work

git clone https://github.com/iree-org/iree.git
cd iree
git submodule update --init --recursive
cd ..

git clone https://github.com/Reshi-Krish/iree-custom-plugin.git
```

You should now have `~/iree-work/iree` and `~/iree-work/iree-custom-plugin`
side by side.

## 2. Configure and build IREE with this plugin linked in

This plugin is built *into* IREE via IREE's out-of-tree plugin mechanism
(`IREE_CMAKE_PLUGIN_PATHS`), which picks up both the runtime driver
(`iree_plugin/iree_runtime_plugin.cmake`) and the compiler plugin
(`iree_plugin/iree_compiler_plugin.cmake`) from the same path, and
`IREE_EXTERNAL_HAL_DRIVERS=hw_emulator`, which registers the runtime driver
so it's linked into the runtime tools/tests.

```bash
cd ~/iree-work

cmake -GNinja -B iree-build -S iree \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DIREE_ENABLE_ASSERTIONS=ON \
  -DIREE_BUILD_COMPILER=ON \
  -DIREE_BUILD_TESTS=ON \
  -DIREE_CMAKE_PLUGIN_PATHS=$(pwd)/iree-custom-plugin/iree_plugin \
  -DIREE_EXTERNAL_HAL_DRIVERS=hw_emulator

cmake --build iree-build --target iree-compile iree-run-module -j2
```

> **Note on build resources:** this project was developed on a machine with
> only 7.5GB of RAM. Linking `iree-compile` statically pulls in all of
> LLVM/MLIR/IREE and can be memory-heavy enough to make the machine
> unresponsive (or crash) at high parallelism. If you hit this, lower `-j`
> further (e.g. `-j1`), close other applications, and make sure you have
> swap configured. Building just `iree-compile`/`iree-run-module` (as above)
> rather than the full `all` target keeps this to a minimum.

To rebuild after changing plugin source under `iree-custom-plugin/`, re-run
the same `cmake --build` command - CMake picks up the changes automatically.

### Optional: standalone hardware_emulator tests

The emulator and kernels can be built and tested completely independently of
IREE:

```bash
cd ~/iree-work/iree-custom-plugin
cmake -B build -S .
cmake --build build
ctest --test-dir build
```

### Optional: runtime HAL driver smoke tests

These are built as part of the main IREE build above (since
`IREE_BUILD_TESTS=ON`) and exercise the driver directly against production
IREE HAL code paths:

```bash
ctest --test-dir ~/iree-work/iree-build -R hw_runtime_plugin
```

## 3. Compile and run the matmul + VMVX fallback demo

The test file
[`iree_plugin/compiler/hw_compiler_plugin/test/auto_matmul_routing.mlir`](iree_plugin/compiler/hw_compiler_plugin/test/auto_matmul_routing.mlir)
has two functions and **no hand-written device annotations**:

- `@on_hw` - contains a `linalg.matmul`, auto-detected and routed to
  `hw_emulator`.
- `@on_cpu` - an `arith.addf`, left on the default device (VMVX, via the
  `local` HAL device).

Set up some variables for convenience:

```bash
export ITOOLS=~/iree-work/iree-build/tools
export PLUGIN=~/iree-work/iree-custom-plugin/iree_plugin/compiler/hw_compiler_plugin
```

### Compile

```bash
$ITOOLS/iree-compile \
    --iree-hal-target-device=hw=hw_emulator \
    --iree-hal-target-device=cpu=local \
    --iree-hal-local-target-device-backends=vmvx \
    $PLUGIN/test/auto_matmul_routing.mlir \
    -o /tmp/auto_matmul_routing.vmfb
```

This declares two named target devices - `hw` (this plugin's `hw_emulator`)
and `cpu` (the generic `local` device running the `vmvx` backend) - and lets
the compiler's `AnnotateMatmulAffinity` pass assign `@on_hw` to `hw`
automatically; `@on_cpu` defaults to `cpu`.

### Run the matmul (on hw_emulator)

```bash
$ITOOLS/iree-run-module \
    --module=/tmp/auto_matmul_routing.vmfb \
    --device=hw_emulator \
    --device=local-sync \
    --function=on_hw \
    --input=2x2xf32=1,2,3,4 \
    --input=2x2xf32=5,6,7,8
```

Expected output (A=[[1,2],[3,4]] x B=[[5,6],[7,8]]):

```
result[0]: hal.buffer_view
2x2xf32=[19 22][43 50]
```

### Run the fallback function (on VMVX)

```bash
$ITOOLS/iree-run-module \
    --module=/tmp/auto_matmul_routing.vmfb \
    --device=hw_emulator \
    --device=local-sync \
    --function=on_cpu \
    --input=4xf32=1,2,3,4
```

Expected output:

```
result[0]: hal.buffer_view
4xf32=2 4 6 8
```

> `--device=local-sync` (not `--device=local`) is the actual registered HAL
> *runtime* driver ID for the CPU device; `local` is only the name used on
> the *compiler* side (`--iree-hal-target-device=cpu=local`). Run
> `iree-run-module --list_drivers` to see all registered runtime drivers.

## Known limitations

- **Function-granularity routing**: `AnnotateMatmulAffinity` routes an
  entire function to `hw_emulator` as soon as it contains at least one
  `linalg.matmul` anywhere in it. A function mixing a matmul with other,
  unsupported ops would incorrectly route all of it to the emulator instead
  of splitting the two.
- **Static shapes only**: the emulator's kernel ABI has no notion of a
  dynamic shape or a workgroup grid; `hw_emulator` requires a fully
  statically-shaped `linalg.matmul` (the compiler bakes the shape into the
  compiled artifact at compile time).
- **Not a portable compiled artifact**: unlike real backends (which embed
  actual compiled code - VM bytecode, object code, PTX, etc.), the compiled
  `.vmfb`'s `hw_emulator` executable only contains a kernel *name* and
  shape metadata. It only runs against a runtime process that has this
  exact plugin's fixed kernel set (`hardware_kernels`) linked in.

See the per-component `README.md` files (linked above) for the full
architecture and design-decision writeups behind each of these.
