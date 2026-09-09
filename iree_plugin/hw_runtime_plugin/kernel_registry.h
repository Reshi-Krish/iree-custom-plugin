// Copyright 2024 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef IREE_HAL_DRIVERS_HW_EMULATOR_KERNEL_REGISTRY_H_
#define IREE_HAL_DRIVERS_HW_EMULATOR_KERNEL_REGISTRY_H_

#include "hardwareEmulator.h"
#include "iree/base/api.h"

//===----------------------------------------------------------------------===//
// iree_hal_hw_emulator_kernel_registry
//===----------------------------------------------------------------------===//

// A statically-known kernel this runtime build can execute, keyed by name.
// hw_emulator has no dynamic code loading (no ELF/shared-library story like
// llvm-cpu's executable plugins) - the set of kernels a build supports is
// fixed at the driver's own compile time, linked in directly from
// hardware_kernels. An executable's export (see executable.h) only ever
// carries a *name*; this registry is what resolves that name back to an
// actual function pointer and its input/output arity, both at runtime and,
// eventually, at compile time (the compiler plugin will need to agree on the
// same set of names).
typedef struct iree_hal_hw_emulator_kernel_registration_t {
  iree_string_view_t name;
  hw_kernel_fn_t kernel;
  iree_host_size_t num_inputs;
  iree_host_size_t num_outputs;
} iree_hal_hw_emulator_kernel_registration_t;

// Looks up a statically-registered kernel by |name|. Returns
// IREE_STATUS_NOT_FOUND if no kernel with that name is registered in this
// build. The returned pointer has static storage duration.
iree_status_t iree_hal_hw_emulator_kernel_registry_lookup(
    iree_string_view_t name,
    const iree_hal_hw_emulator_kernel_registration_t** out_registration);

#endif  // IREE_HAL_DRIVERS_HW_EMULATOR_KERNEL_REGISTRY_H_
