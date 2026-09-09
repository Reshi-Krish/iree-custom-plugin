// Copyright 2024 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// End-to-end smoke test for the executable + command_buffer path: prepares
// an executable wrapping hardware_kernels' matmul_kernel, records a single
// dispatch against it into a real (deferred) command buffer, applies that
// recording against the device's applier - standing in for what
// queue_execute will do once implemented - and checks the computed result.
// This is the closest thing to the project's stated milestone
// (linalg.matmul -> custom dispatch -> HAL -> emulator -> custom kernel)
// achievable before a compiler exists to emit the dispatch itself.

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "hardwareEmulator.h"
#include "hw_runtime_plugin/api.h"
#include "hw_runtime_plugin/device.h"
#include "hw_runtime_plugin/executable.h"
#include "iree/async/util/proactor_pool.h"
#include "iree/base/api.h"
#include "iree/hal/api.h"
#include "iree/hal/utils/deferred_command_buffer.h"
#include "matmul_kernel.h"

static const iree_hal_hw_emulator_executable_export_t kExports[] = {
    {
        .name = IREE_SVL("matmul"),
        .kernel = matmul_kernel,
        .num_inputs = 2,
        .num_outputs = 1,
    },
};

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

  // Prepare the executable through the real executable_cache path (exercises
  // can_prepare_format/prepare_executable, not just executable_create).
  iree_hal_executable_cache_t* executable_cache = NULL;
  IREE_CHECK_OK(iree_hal_executable_cache_create(
      device, IREE_SV("test_cache"), &executable_cache));
  iree_hal_executable_params_t executable_params;
  memset(&executable_params, 0, sizeof(executable_params));
  executable_params.executable_format =
      iree_make_cstring_view(IREE_HAL_HW_EMULATOR_EXECUTABLE_FORMAT);
  executable_params.executable_data =
      iree_make_const_byte_span(kExports, sizeof(kExports));
  bool can_prepare = iree_hal_executable_cache_can_prepare_format(
      executable_cache, /*caching_mode=*/0, executable_params.executable_format);
  IREE_CHECK_OK(can_prepare ? iree_ok_status()
                            : iree_make_status(IREE_STATUS_INTERNAL,
                                               "expected can_prepare_format "
                                               "to accept our own format"));
  iree_hal_executable_t* executable = NULL;
  IREE_CHECK_OK(iree_hal_executable_cache_prepare_executable(
      executable_cache, &executable_params, &executable));

  // A[2,2] * B[2,2] = C[2,2].
  const int64_t m = 2, n = 2, k = 2;
  const float a_data[4] = {1.0f, 2.0f, 3.0f, 4.0f};
  const float b_data[4] = {5.0f, 6.0f, 7.0f, 8.0f};
  const float expected_c[4] = {19.0f, 22.0f, 43.0f, 50.0f};

  iree_hal_allocator_t* allocator = iree_hal_device_allocator(device);
  iree_hal_buffer_params_t buffer_params;
  memset(&buffer_params, 0, sizeof(buffer_params));
  iree_hal_buffer_t* buffer_a = NULL;
  iree_hal_buffer_t* buffer_b = NULL;
  iree_hal_buffer_t* buffer_c = NULL;
  IREE_CHECK_OK(iree_hal_allocator_allocate_buffer(
      allocator, buffer_params, sizeof(a_data), &buffer_a));
  IREE_CHECK_OK(iree_hal_allocator_allocate_buffer(
      allocator, buffer_params, sizeof(b_data), &buffer_b));
  IREE_CHECK_OK(iree_hal_allocator_allocate_buffer(
      allocator, buffer_params, sizeof(expected_c), &buffer_c));
  IREE_CHECK_OK(
      iree_hal_buffer_map_write(buffer_a, 0, a_data, sizeof(a_data)));
  IREE_CHECK_OK(
      iree_hal_buffer_map_write(buffer_b, 0, b_data, sizeof(b_data)));

  // Record a single-dispatch command buffer.
  iree_hal_command_buffer_t* recorder = NULL;
  IREE_CHECK_OK(iree_hal_command_buffer_create(
      device, IREE_HAL_COMMAND_BUFFER_MODE_ONE_SHOT,
      IREE_HAL_COMMAND_CATEGORY_DISPATCH, IREE_HAL_QUEUE_AFFINITY_ANY,
      /*binding_capacity=*/0, &recorder));
  IREE_CHECK_OK(iree_hal_command_buffer_begin(recorder));
  iree_hal_buffer_ref_t bindings[3] = {
      {.buffer = buffer_a, .offset = 0, .length = IREE_HAL_WHOLE_BUFFER},
      {.buffer = buffer_b, .offset = 0, .length = IREE_HAL_WHOLE_BUFFER},
      {.buffer = buffer_c, .offset = 0, .length = IREE_HAL_WHOLE_BUFFER},
  };
  iree_hal_dispatch_config_t config;
  memset(&config, 0, sizeof(config));
  // Our kernel ABI has no real workgroup grid (a dispatch is always exactly
  // one kernel invocation; see command_buffer.c), but iree_hal_command_buffer
  // _dispatch treats an all-zero workgroup_count as an intentional no-op and
  // skips the dispatch entirely, so this must be non-zero.
  config.workgroup_count[0] = config.workgroup_count[1] =
      config.workgroup_count[2] = 1;
  const hw_matmul_params_t matmul_params = {.m = m, .n = n, .k = k};
  iree_const_byte_span_t constants =
      iree_make_const_byte_span(&matmul_params, sizeof(matmul_params));
  const iree_hal_buffer_ref_list_t binding_list = {
      .count = IREE_ARRAYSIZE(bindings),
      .values = bindings,
  };
  IREE_CHECK_OK(iree_hal_command_buffer_dispatch(
      recorder, executable, iree_hal_executable_function_from_value(0),
      config, constants, binding_list, IREE_HAL_DISPATCH_FLAG_NONE));
  IREE_CHECK_OK(iree_hal_command_buffer_end(recorder));

  // Apply the recording against the device's applier - standing in for
  // queue_execute, which isn't implemented yet (see device.h).
  iree_hal_command_buffer_t* applier =
      iree_hal_hw_emulator_device_debug_applier(device);
  IREE_CHECK_OK(iree_hal_deferred_command_buffer_apply(
      recorder, applier, iree_hal_buffer_binding_table_empty()));

  float actual_c[4];
  IREE_CHECK_OK(
      iree_hal_buffer_map_read(buffer_c, 0, actual_c, sizeof(actual_c)));
  bool matches = memcmp(actual_c, expected_c, sizeof(expected_c)) == 0;
  if (!matches) {
    fprintf(stderr,
            "matmul mismatch: got [%f %f %f %f], expected [%f %f %f %f]\n",
            actual_c[0], actual_c[1], actual_c[2], actual_c[3], expected_c[0],
            expected_c[1], expected_c[2], expected_c[3]);
  }
  IREE_CHECK_OK(matches ? iree_ok_status()
                        : iree_make_status(IREE_STATUS_INTERNAL,
                                           "matmul result mismatch"));

  iree_hal_command_buffer_release(recorder);
  iree_hal_buffer_release(buffer_a);
  iree_hal_buffer_release(buffer_b);
  iree_hal_buffer_release(buffer_c);
  iree_hal_executable_release(executable);
  iree_hal_executable_cache_release(executable_cache);
  iree_hal_device_release(device);
  iree_hal_driver_release(driver);

  fprintf(stdout,
          "hw_emulator command_buffer/executable matmul smoke test passed\n");
  return 0;
}
