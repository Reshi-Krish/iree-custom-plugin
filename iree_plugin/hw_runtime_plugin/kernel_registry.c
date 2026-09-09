// Copyright 2024 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "hw_runtime_plugin/kernel_registry.h"

#include "matmul_kernel.h"
#include "template_kernel.h"

// The fixed set of kernels this runtime build knows how to execute. Adding a
// kernel to hardware_kernels does not make it usable until it is registered
// here by name - this list is the single source of truth for both a
// kernel's arity (num_inputs/num_outputs) and how a compiled executable
// names it (see executable.h).
static const iree_hal_hw_emulator_kernel_registration_t kKernelRegistry[] = {
    {
        .name = IREE_SVL("template"),
        .kernel = template_kernel,
        .num_inputs = 0,
        .num_outputs = 0,
    },
    {
        .name = IREE_SVL("matmul"),
        .kernel = matmul_kernel,
        .num_inputs = 2,
        .num_outputs = 1,
    },
};

iree_status_t iree_hal_hw_emulator_kernel_registry_lookup(
    iree_string_view_t name,
    const iree_hal_hw_emulator_kernel_registration_t** out_registration) {
  *out_registration = NULL;
  for (iree_host_size_t i = 0; i < IREE_ARRAYSIZE(kKernelRegistry); ++i) {
    if (iree_string_view_equal(kKernelRegistry[i].name, name)) {
      *out_registration = &kKernelRegistry[i];
      return iree_ok_status();
    }
  }
  return iree_make_status(IREE_STATUS_NOT_FOUND,
                          "no hw_emulator kernel registered with name '%.*s'",
                          (int)name.size, name.data);
}
