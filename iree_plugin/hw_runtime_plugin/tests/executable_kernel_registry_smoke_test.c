// Copyright 2024 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Exercises the name-based executable format's validation: an export naming
// an unregistered kernel, or one whose kernel_name isn't NUL-terminated
// within its fixed capacity, must be rejected at executable-creation time
// rather than surfacing later (or, worse, reading out of bounds) at
// dispatch time.

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "hw_runtime_plugin/api.h"
#include "hw_runtime_plugin/executable.h"
#include "iree/async/util/proactor_pool.h"
#include "iree/base/api.h"
#include "iree/hal/api.h"

static iree_status_t create_executable(
    iree_hal_executable_cache_t* executable_cache,
    const iree_hal_hw_emulator_executable_export_t* exports,
    iree_host_size_t export_count, iree_hal_executable_t** out_executable) {
  iree_hal_executable_params_t params;
  memset(&params, 0, sizeof(params));
  params.executable_format =
      iree_make_cstring_view(IREE_HAL_HW_EMULATOR_EXECUTABLE_FORMAT);
  params.executable_data =
      iree_make_const_byte_span(exports, export_count * sizeof(*exports));
  return iree_hal_executable_cache_prepare_executable(executable_cache, &params,
                                                       out_executable);
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  iree_allocator_t host_allocator = iree_allocator_system();

  iree_async_proactor_pool_t* proactor_pool = NULL;
  IREE_CHECK_OK(iree_async_proactor_pool_create(
      /*node_count=*/1, /*node_ids=*/NULL,
      iree_async_proactor_pool_options_default(), host_allocator,
      &proactor_pool));
  iree_hal_hw_emulator_driver_options_t driver_options;
  iree_hal_hw_emulator_driver_options_initialize(&driver_options);
  iree_hal_driver_t* driver = NULL;
  IREE_CHECK_OK(iree_hal_hw_emulator_driver_create(
      IREE_SV("hw_emulator"), &driver_options, host_allocator, &driver));
  iree_hal_device_create_params_t create_params =
      iree_hal_device_create_params_default();
  create_params.proactor_pool = proactor_pool;
  iree_hal_device_t* device = NULL;
  IREE_CHECK_OK(iree_hal_driver_create_default_device(
      driver, &create_params, host_allocator, &device));
  iree_async_proactor_pool_release(proactor_pool);

  iree_hal_executable_cache_t* executable_cache = NULL;
  IREE_CHECK_OK(iree_hal_executable_cache_create(
      device, IREE_SV("test_cache"), &executable_cache));

  // A real, registered kernel name must succeed.
  {
    const iree_hal_hw_emulator_executable_export_t exports[] = {
        {.kernel_name = "matmul"},
    };
    iree_hal_executable_t* executable = NULL;
    IREE_CHECK_OK(create_executable(executable_cache, exports,
                                    IREE_ARRAYSIZE(exports), &executable));
    iree_hal_executable_release(executable);
  }

  // An unregistered kernel name must be rejected at creation, not later.
  {
    const iree_hal_hw_emulator_executable_export_t exports[] = {
        {.kernel_name = "does_not_exist"},
    };
    iree_hal_executable_t* executable = NULL;
    iree_status_t status = create_executable(
        executable_cache, exports, IREE_ARRAYSIZE(exports), &executable);
    bool rejected = !iree_status_is_ok(status);
    iree_status_ignore(status);
    IREE_CHECK_OK(rejected ? iree_ok_status()
                          : iree_make_status(IREE_STATUS_INTERNAL,
                                             "expected an unregistered "
                                             "kernel name to be rejected"));
  }

  // A kernel_name with no NUL terminator anywhere in its fixed capacity must
  // also be rejected, not read out of bounds.
  {
    iree_hal_hw_emulator_executable_export_t exports[1];
    memset(exports[0].kernel_name, 'x',
           sizeof(exports[0].kernel_name));  // no NUL anywhere
    iree_hal_executable_t* executable = NULL;
    iree_status_t status = create_executable(executable_cache, exports,
                                             IREE_ARRAYSIZE(exports),
                                             &executable);
    bool rejected = !iree_status_is_ok(status);
    iree_status_ignore(status);
    IREE_CHECK_OK(rejected
                      ? iree_ok_status()
                      : iree_make_status(IREE_STATUS_INTERNAL,
                                         "expected a non-NUL-terminated "
                                         "kernel_name to be rejected"));
  }

  iree_hal_executable_cache_release(executable_cache);
  iree_hal_device_release(device);
  iree_hal_driver_release(driver);

  fprintf(stdout, "hw_emulator executable kernel registry smoke test passed\n");
  return 0;
}
