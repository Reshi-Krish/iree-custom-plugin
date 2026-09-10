// Copyright 2024 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef IREE_HAL_DRIVERS_HW_EMULATOR_EXECUTABLE_H_
#define IREE_HAL_DRIVERS_HW_EMULATOR_EXECUTABLE_H_

#include "hardwareEmulator.h"
#include "hw_runtime_plugin/kernel_registry.h"
#include "iree/base/api.h"
#include "iree/hal/api.h"

//===----------------------------------------------------------------------===//
// iree_hal_hw_emulator_executable_t
//===----------------------------------------------------------------------===//

// A single named export in a hw_emulator executable: which statically-known
// kernel (see kernel_registry.h) this entry point runs, identified by name
// rather than a function pointer. A pointer would only be meaningful within
// the process that created it; executable_data must remain valid to load in
// a *different* process (a real compiled artifact is emitted by iree-compile
// and loaded later by a separate runtime process), so executable_data is a
// raw array of this fixed-size, pointer-free struct - safe to memcpy,
// serialize to disk, or embed in a compiled module. A kernel's arity
// (num_inputs/num_outputs) is deliberately NOT part of this wire format: it
// is a fixed property of the kernel implementation itself, resolved from the
// registry by name, not something a compiler should redundantly restate.
//
// export_name and kernel_name serve two different purposes and must not be
// conflated (a real bug: an earlier version only had kernel_name and
// compared it against `hal.executable.lookup.function`'s name argument,
// which fails - see below):
//  - export_name is the *compiler-assigned* `hal.executable.export` symbol
//    (e.g. "on_hw_dispatch_0_matmul_2x2x2_f32") - unique per dispatch within
//    a compiled module, used only so the generic HAL/VM dispatch path
//    (`hal.executable.lookup.function`, which always resolves indirectly by
//    this name - confirmed empirically 2026-09-10, it is never resolved by
//    ordinal even when statically knowable) can find the right ordinal.
//  - kernel_name is the fixed, small set of hw_emulator kernel identities
//    (see kernel_registry.h, e.g. "matmul") - used once an ordinal is known,
//    to resolve which actual hardware kernel function to run.
//
// kernel_params carries the shape/metadata a kernel needs as its `params`
// argument (see kernel_registry.h's hw_kernel_fn_t and e.g.
// hardware_kernels/include/matmul_kernel.h's hw_matmul_params_t). This can
// only be a compile-time constant because hw_emulator has no codegen story:
// real backends embed a dispatch's static shape directly into generated
// code, but we just resolve to one of a handful of fixed named kernels, so
// there is no code generation step in which to embed shape info - it must
// instead travel through this wire format. The compiler bakes it in during
// serializeExecutable by reading the matmul's static tensor shapes directly
// out of the executable's translated IR (PluginRegistration.cpp); this only
// works because hw_emulator requires statically-shaped dispatches (see the
// AnnotateMatmulAffinity function-granularity limitation writeup) - a
// dynamically-shaped matmul has no compile-time m/n/k to bake in and is out
// of scope. 24 bytes = sizeof(hw_matmul_params_t) (3x int64_t); grow this if
// a future kernel needs a larger params struct. Byte layout matches the C
// struct's native layout directly (no explicit endianness/packing) since the
// compiler and runtime always run on the same machine for this project.
#define IREE_HAL_HW_EMULATOR_EXPORT_NAME_CAPACITY 64
#define IREE_HAL_HW_EMULATOR_KERNEL_NAME_CAPACITY 32
#define IREE_HAL_HW_EMULATOR_KERNEL_PARAMS_CAPACITY 24
typedef struct iree_hal_hw_emulator_executable_export_t {
  // NUL-terminated `hal.executable.export` symbol name, as assigned by the
  // compiler. Bytes beyond the NUL are ignored (and should be zero, though
  // this is not required).
  char export_name[IREE_HAL_HW_EMULATOR_EXPORT_NAME_CAPACITY];
  // NUL-terminated name of a kernel_registry.h entry. Bytes beyond the NUL
  // are ignored (and should be zero, though this is not required).
  char kernel_name[IREE_HAL_HW_EMULATOR_KERNEL_NAME_CAPACITY];
  // Opaque, kernel-specific parameter blob passed as-is to the kernel fn as
  // its `params` argument. See comment above.
  uint8_t kernel_params[IREE_HAL_HW_EMULATOR_KERNEL_PARAMS_CAPACITY];
} iree_hal_hw_emulator_executable_export_t;

#define IREE_HAL_HW_EMULATOR_EXECUTABLE_FORMAT "{hw_emulator-executable}"

// Creates a hw_emulator executable from a binary in memory. Each executable may
// contain multiple entry points and be composed of several modules presented to
// the HAL as a single instance. See iree_hal_executable_params_t for more
// information about the lifetime of the resources referenced within.
iree_status_t iree_hal_hw_emulator_executable_create(
    const iree_hal_executable_params_t* executable_params,
    iree_allocator_t host_allocator, iree_hal_executable_t** out_executable);

// Resolves |export_ordinal| to the kernel it names (via kernel_registry.h),
// or IREE_STATUS_OUT_OF_RANGE/IREE_STATUS_NOT_FOUND if it does not exist or
// names an unregistered kernel. Used by the applier command buffer
// (command_buffer.c) to resolve a dispatch to a kernel to run.
iree_status_t iree_hal_hw_emulator_executable_lookup_export(
    iree_hal_executable_t* executable,
    iree_hal_executable_function_t export_ordinal,
    const iree_hal_hw_emulator_kernel_registration_t** out_registration);

// Returns a pointer to |export_ordinal|'s kernel_params blob (see above),
// valid for as long as |executable| is. Used by the applier command buffer
// (command_buffer.c) to pass a dispatch's compile-time-baked shape info to
// its kernel as `params`.
iree_status_t iree_hal_hw_emulator_executable_lookup_export_params(
    iree_hal_executable_t* executable,
    iree_hal_executable_function_t export_ordinal,
    const uint8_t** out_kernel_params);

#endif  // IREE_HAL_DRIVERS_HW_EMULATOR_EXECUTABLE_H_
