# hardware_kernels

Kernels that can be executed on the hardware emulator (see
`hardware_emulator/`) via `hw_execute()`. This document describes the ABI
every kernel in this directory conforms to, and what a kernel may and may not
assume about its arguments.

## The kernel function signature

Every kernel matches `hw_kernel_fn_t`, declared in
`hardware_emulator/include/hardwareEmulator.h`:

```c
typedef hw_status_t (*hw_kernel_fn_t)(void* const* inputs, size_t num_inputs,
                                       void* const* outputs,
                                       size_t num_outputs, void* params);
```

It is invoked by `hw_execute(device, kernel, inputs, num_inputs, outputs,
num_outputs, params)`, which does the following before calling the kernel:

1. Resolves each `hw_buffer_t` in `inputs`/`outputs` to a host pointer via
   `hw_buffer_map()`.
2. Calls `kernel(resolved_inputs, num_inputs, resolved_outputs, num_outputs,
   params)`.

So a kernel body never sees `hw_device_t` or `hw_buffer_t` -- only plain
pointers. This keeps kernel authoring free of any emulator/HAL bookkeeping;
a kernel is just numerical code operating on memory it's handed.

## The contract

- **Buffer order is positional.** `inputs[0]`, `inputs[1]`, ... and
  `outputs[0]`, `outputs[1]`, ... are matched by convention between the
  caller building the `hw_buffer_t` arrays and the kernel -- there are no
  names or types attached to a slot. Each kernel's header comments document
  what belongs at each index (e.g. matmul: `inputs = {A, B}`, `outputs =
  {C}`).
- **`params` is an opaque blob.** `hw_execute()` passes it through
  unchanged; the kernel casts it to whatever struct it expects (e.g.
  `hw_matmul_params_t`). A kernel that needs no metadata (like
  `template_kernel`) simply ignores it -- callers may pass `NULL`.
- **No size/shape checking happens on the kernel's behalf.** `hw_execute()`
  only bounds-checks that each buffer lies within the *device's memory
  pool* (via `hw_buffer_map()`); it has no idea what shape a kernel expects,
  and a kernel does not re-derive buffer sizes from anything at runtime --
  it trusts the caller allocated buffers large enough for the shape
  described in `params`. This mirrors real accelerator dispatch: shape
  correctness is guaranteed upstream (by the compiler that generated the
  dispatch), not re-validated by the runtime or the kernel on every call.
  Passing undersized buffers is undefined behavior, exactly as it would be
  on real hardware.
- **Layout/dtype are whatever the kernel documents.** There is no generic
  tensor descriptor in this ABI -- e.g. `matmul_kernel` assumes row-major,
  contiguous, `float32` buffers with no batch dimension. A future kernel is
  free to define different assumptions as long as they're documented next
  to it.

## Kernels in this directory

### `template_kernel` (`template_kernel.h` / `.c`)

Ignores every argument and prints a message. Exists purely to demonstrate
the minimal shape a kernel must have to satisfy `hw_kernel_fn_t` and be
dispatched through `hw_execute()`.

### `matmul_kernel` (`matmul_kernel.h` / `.c`)

Computes `C[m,n] = A[m,k] * B[k,n]` over row-major `float32` buffers.

```c
typedef struct hw_matmul_params {
  int64_t m;
  int64_t n;
  int64_t k;
} hw_matmul_params_t;
```

- `inputs  = { A, B }` -- `A` is `m*k` floats, `B` is `k*n` floats.
- `outputs = { C }` -- `m*n` floats.
- `params` points to an `hw_matmul_params_t` describing `m`, `n`, `k`.

## Adding a new kernel

1. Add `include/<name>_kernel.h` declaring a function matching
   `hw_kernel_fn_t`, plus any `params` struct it needs.
2. Add `src/<name>_kernel.c` implementing it.
3. Document, next to the declaration, the meaning of each `inputs`/`outputs`
   slot, the `params` struct (if any), and any layout/dtype assumptions --
   exactly as done above for `matmul_kernel`.
4. Add it to this README's kernel list.
