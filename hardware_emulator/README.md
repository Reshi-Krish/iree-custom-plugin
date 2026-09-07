# hardware_emulator

A minimal, in-process C model of a low-power AI accelerator: a single
device-memory pool, opaque handles for the device and for buffers within
that pool, and one generic dispatch entry point that invokes a kernel
function pointer. This is the thing IREE's custom HAL driver
(`iree_plugin/hal`) will eventually talk to instead of real hardware, and
the thing `hardware_kernels/` (see its own README for the kernel ABI)
provides kernels for.

It is deliberately not tied to any particular kernel -- it knows nothing
about matmul, tensors, or shapes. It only knows how to hand out memory and
call a function pointer with resolved pointers into that memory.

## API overview

All types and functions are declared in `include/hardwareEmulator.h`.

- **`hw_status_t`** -- every function returns one of these. It's a
  `{ hw_status_code_t code; const char* message; }` pair (`message` is a
  static string, may be `NULL`). Check success with `hw_status_is_ok()`.
- **`hw_device_t`** -- opaque handle for one emulated hardware instance.
  `hw_init_hardware(device_id, memory_size, &device)` allocates the
  device's entire memory pool up front; `hw_destroy_hardware(device)` frees
  it. The struct definition lives only in `hardwareEmulator.c` -- callers
  never see its fields, mirroring how `iree_hal_device_t` works in IREE
  itself.
- **`hw_buffer_t`** -- an opaque `{ offset, size }` handle into a device's
  memory pool, returned by `hw_allocate(device, size, &buffer)`. It is not
  a host pointer. `hw_allocate` is a bump allocator: there is no per-buffer
  free, only whole-device teardown via `hw_destroy_hardware`.
- **`hw_buffer_map(device, buffer, &ptr)`** -- resolves a buffer handle to
  a host-accessible pointer, bounds-checked against the device's pool.
- **`hw_execute(device, kernel, inputs, num_inputs, outputs, num_outputs, params)`**
  -- the dispatch entry point. Maps every input/output buffer to a host
  pointer, then calls `kernel(resolved_inputs, num_inputs, resolved_outputs,
  num_outputs, params)`. `kernel` is a `hw_kernel_fn_t` function pointer;
  see `hardware_kernels/README.md` for the ABI kernels implement and for
  what `params` means.

## Why it's shaped this way

- **Opaque handles, not a global singleton.** `hw_init_hardware` returns a
  handle rather than initializing a single static instance, so multiple
  independent emulated devices can exist at once (useful for tests, and it
  mirrors IREE's own device model where a driver can enumerate/create more
  than one device).
- **Buffer handles are offsets, not raw pointers.** Real accelerators have
  a memory space distinct from the host's; modeling buffers as opaque
  `{offset, size}` handles that must be explicitly mapped keeps that
  distinction visible, and matches how `iree_hal_buffer_t` is used in
  IREE's HAL.
- **Kernels receive resolved host pointers, not handles.** Resolving
  `hw_buffer_t -> void*` happens once, at dispatch time, inside
  `hw_execute` -- not inside the kernel. This mirrors how a real
  accelerator's launch sequence translates buffer handles to device
  addresses before the kernel body runs, and it keeps kernel code free of
  any emulator/HAL bookkeeping (see `hardware_kernels/README.md`).
- **No semantic validation beyond pool bounds.** `hw_buffer_map` checks
  that a handle lies within the device's memory pool, but neither
  `hw_execute` nor any kernel checks that a buffer is the right *size* for
  what the kernel expects. That correctness is the compiler/caller's
  responsibility -- exactly as on real hardware, where the runtime trusts
  the dispatch it was given rather than re-deriving shapes at every call.

## Example

```c
hw_device_t* device = NULL;
hw_init_hardware(/*device_id=*/0, /*memory_size=*/1 << 16, &device);

hw_buffer_t buf;
hw_allocate(device, 64, &buf);

void* ptr = NULL;
hw_buffer_map(device, buf, &ptr);
memset(ptr, 0, 64);

hw_execute(device, some_kernel, &buf, 1, NULL, 0, NULL);

hw_destroy_hardware(device);
```

See `tests/emulator_tests/` for complete, runnable examples (a bare
`template_kernel` dispatch and a full random-input `matmul_kernel` run
checked against a CPU reference).

## Building

See the top-level `CMakeLists.txt` -- this directory builds as the
`hardware_emulator` static library target.

## Future direction

Per the project proposal, this in-process C model is meant to be the
first of several backends behind the same ABI: it can later be swapped for
a QEMU-based simulator or real hardware without `hardware_kernels/` or
`iree_plugin/hal` needing to change, as long as the replacement honors the
same `hw_status_t` / `hw_device_t` / `hw_buffer_t` / `hw_execute` contract.
