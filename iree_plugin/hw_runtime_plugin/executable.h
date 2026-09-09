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
#define IREE_HAL_HW_EMULATOR_KERNEL_NAME_CAPACITY 32
typedef struct iree_hal_hw_emulator_executable_export_t {
  // NUL-terminated name of a kernel_registry.h entry. Bytes beyond the NUL
  // are ignored (and should be zero, though this is not required).
  char kernel_name[IREE_HAL_HW_EMULATOR_KERNEL_NAME_CAPACITY];
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

#endif  // IREE_HAL_DRIVERS_HW_EMULATOR_EXECUTABLE_H_
