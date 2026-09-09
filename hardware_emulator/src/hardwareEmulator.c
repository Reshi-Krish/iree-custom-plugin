#include "hardwareEmulator.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

struct hw_device {
  int device_id;
  uint8_t* memory;
  size_t memory_size;
  size_t next_free_offset;  // bump allocator watermark

  // Asynchronous execution state; see hw_execute_async(). Guarded by the
  // release/acquire pairing on execution_status: execution_result is written
  // before the RUNNING->COMPLETE/FAILED release store and only read after
  // observing that store via an acquire load, so no separate lock is needed.
  atomic_int execution_status;  // hw_execution_status_t
  hw_status_t execution_result;
  pthread_t execution_thread;
  bool execution_thread_valid;
};

static hw_status_t hw_status_make(hw_status_code_t code, const char* message) {
  hw_status_t status;
  status.code = code;
  status.message = message;
  return status;
}

hw_status_t hw_init_hardware(int device_id, size_t memory_size,
                              hw_device_t** out_device) {
  if (!out_device) {
    return hw_status_make(HW_STATUS_INVALID_ARGUMENT,
                           "out_device must not be NULL");
  }
  *out_device = NULL;
  if (memory_size == 0) {
    return hw_status_make(HW_STATUS_INVALID_ARGUMENT,
                           "memory_size must be greater than zero");
  }

  hw_device_t* device = (hw_device_t*)malloc(sizeof(hw_device_t));
  if (!device) {
    return hw_status_make(HW_STATUS_OUT_OF_MEMORY,
                           "failed to allocate hw_device_t");
  }

  device->memory = (uint8_t*)malloc(memory_size);
  if (!device->memory) {
    free(device);
    return hw_status_make(HW_STATUS_OUT_OF_MEMORY,
                           "failed to allocate hardware memory pool");
  }

  device->device_id = device_id;
  device->memory_size = memory_size;
  device->next_free_offset = 0;
  atomic_init(&device->execution_status, HW_EXECUTION_IDLE);
  device->execution_thread_valid = false;

  *out_device = device;
  return hw_status_ok();
}

void hw_destroy_hardware(hw_device_t* device) {
  if (!device) return;
  // Join any still-running background execution before freeing the device
  // out from under it.
  if (device->execution_thread_valid) {
    pthread_join(device->execution_thread, NULL);
  }
  free(device->memory);
  free(device);
}

hw_status_t hw_allocate(hw_device_t* device, size_t size,
                         hw_buffer_t* out_buffer) {
  if (!device || !out_buffer) {
    return hw_status_make(HW_STATUS_INVALID_ARGUMENT,
                           "device and out_buffer must not be NULL");
  }
  if (size == 0) {
    return hw_status_make(HW_STATUS_INVALID_ARGUMENT,
                           "size must be greater than zero");
  }
  if (size > device->memory_size - device->next_free_offset) {
    return hw_status_make(HW_STATUS_OUT_OF_MEMORY,
                           "hardware memory pool exhausted");
  }

  out_buffer->offset = device->next_free_offset;
  out_buffer->size = size;
  device->next_free_offset += size;
  return hw_status_ok();
}

hw_status_t hw_buffer_map(hw_device_t* device, hw_buffer_t buffer,
                           void** out_ptr) {
  if (!device || !out_ptr) {
    return hw_status_make(HW_STATUS_INVALID_ARGUMENT,
                           "device and out_ptr must not be NULL");
  }
  if (buffer.offset > device->memory_size ||
      buffer.size > device->memory_size - buffer.offset) {
    return hw_status_make(HW_STATUS_OUT_OF_RANGE,
                           "buffer lies outside the device memory pool");
  }

  *out_ptr = device->memory + buffer.offset;
  return hw_status_ok();
}

hw_status_t hw_execute(hw_device_t* device, hw_kernel_fn_t kernel,
                        hw_buffer_t* inputs, size_t num_inputs,
                        hw_buffer_t* outputs, size_t num_outputs,
                        void* params) {
  if (!device || !kernel) {
    return hw_status_make(HW_STATUS_INVALID_ARGUMENT,
                           "device and kernel must not be NULL");
  }
  if ((num_inputs > 0 && !inputs) || (num_outputs > 0 && !outputs)) {
    return hw_status_make(HW_STATUS_INVALID_ARGUMENT,
                           "inputs/outputs must not be NULL when count > 0");
  }

  // Stack-allocate small resolved-pointer arrays; kernels with more buffers
  // than this can be supported later if needed.
  enum { kMaxBuffers = 16 };
  if (num_inputs > kMaxBuffers || num_outputs > kMaxBuffers) {
    return hw_status_make(HW_STATUS_INVALID_ARGUMENT,
                           "too many input/output buffers for hw_execute");
  }

  void* resolved_inputs[kMaxBuffers];
  void* resolved_outputs[kMaxBuffers];

  for (size_t i = 0; i < num_inputs; ++i) {
    hw_status_t status = hw_buffer_map(device, inputs[i], &resolved_inputs[i]);
    if (!hw_status_is_ok(status)) return status;
  }
  for (size_t i = 0; i < num_outputs; ++i) {
    hw_status_t status =
        hw_buffer_map(device, outputs[i], &resolved_outputs[i]);
    if (!hw_status_is_ok(status)) return status;
  }

  return kernel(resolved_inputs, num_inputs, resolved_outputs, num_outputs,
                params);
}

//===----------------------------------------------------------------------===
// Asynchronous execution
//===----------------------------------------------------------------------===

typedef struct hw_async_execution_args {
  hw_device_t* device;
  hw_kernel_fn_t kernel;
  hw_buffer_t* inputs;   // owned copy, freed by the background thread
  size_t num_inputs;
  hw_buffer_t* outputs;  // owned copy, freed by the background thread
  size_t num_outputs;
  void* params;
} hw_async_execution_args_t;

static void* hw_execute_async_thread_main(void* raw_args) {
  hw_async_execution_args_t* args = (hw_async_execution_args_t*)raw_args;

  hw_status_t result =
      hw_execute(args->device, args->kernel, args->inputs, args->num_inputs,
                args->outputs, args->num_outputs, args->params);

  // Written before the release store below, which is what makes it safe for
  // hw_query_execution_status() to read after its matching acquire load.
  args->device->execution_result = result;
  atomic_store_explicit(
      &args->device->execution_status,
      hw_status_is_ok(result) ? HW_EXECUTION_COMPLETE : HW_EXECUTION_FAILED,
      memory_order_release);

  free(args->inputs);
  free(args->outputs);
  free(args);
  return NULL;
}

hw_status_t hw_execute_async(hw_device_t* device, hw_kernel_fn_t kernel,
                              hw_buffer_t* inputs, size_t num_inputs,
                              hw_buffer_t* outputs, size_t num_outputs,
                              void* params) {
  if (!device || !kernel) {
    return hw_status_make(HW_STATUS_INVALID_ARGUMENT,
                           "device and kernel must not be NULL");
  }
  if ((num_inputs > 0 && !inputs) || (num_outputs > 0 && !outputs)) {
    return hw_status_make(HW_STATUS_INVALID_ARGUMENT,
                           "inputs/outputs must not be NULL when count > 0");
  }

  hw_execution_status_t current_status =
      (hw_execution_status_t)atomic_load_explicit(&device->execution_status,
                                                  memory_order_acquire);
  if (current_status == HW_EXECUTION_RUNNING) {
    return hw_status_make(
        HW_STATUS_INTERNAL_ERROR,
        "a previous asynchronous execution on this device is still running");
  }

  // The previous execution (if any) has finished; reclaim its thread handle
  // before starting a new one.
  if (device->execution_thread_valid) {
    pthread_join(device->execution_thread, NULL);
    device->execution_thread_valid = false;
  }

  hw_async_execution_args_t* args =
      (hw_async_execution_args_t*)malloc(sizeof(*args));
  if (!args) {
    return hw_status_make(HW_STATUS_OUT_OF_MEMORY,
                           "failed to allocate async execution args");
  }
  *args = (hw_async_execution_args_t){
      .device = device,
      .kernel = kernel,
      .num_inputs = num_inputs,
      .num_outputs = num_outputs,
      .params = params,
  };
  if (num_inputs > 0) {
    args->inputs = (hw_buffer_t*)malloc(num_inputs * sizeof(hw_buffer_t));
    if (!args->inputs) {
      free(args);
      return hw_status_make(HW_STATUS_OUT_OF_MEMORY,
                             "failed to allocate async input buffer copy");
    }
    memcpy(args->inputs, inputs, num_inputs * sizeof(hw_buffer_t));
  }
  if (num_outputs > 0) {
    args->outputs = (hw_buffer_t*)malloc(num_outputs * sizeof(hw_buffer_t));
    if (!args->outputs) {
      free(args->inputs);
      free(args);
      return hw_status_make(HW_STATUS_OUT_OF_MEMORY,
                             "failed to allocate async output buffer copy");
    }
    memcpy(args->outputs, outputs, num_outputs * sizeof(hw_buffer_t));
  }

  atomic_store_explicit(&device->execution_status, HW_EXECUTION_RUNNING,
                        memory_order_release);
  int rc = pthread_create(&device->execution_thread, NULL,
                          hw_execute_async_thread_main, args);
  if (rc != 0) {
    atomic_store_explicit(&device->execution_status, HW_EXECUTION_FAILED,
                          memory_order_release);
    free(args->inputs);
    free(args->outputs);
    free(args);
    return hw_status_make(HW_STATUS_INTERNAL_ERROR,
                           "failed to spawn asynchronous execution thread");
  }
  device->execution_thread_valid = true;
  return hw_status_ok();
}

hw_execution_status_t hw_query_execution_status(hw_device_t* device,
                                                 hw_status_t* out_result) {
  if (!device) return HW_EXECUTION_FAILED;
  hw_execution_status_t status = (hw_execution_status_t)atomic_load_explicit(
      &device->execution_status, memory_order_acquire);
  if (out_result &&
      (status == HW_EXECUTION_COMPLETE || status == HW_EXECUTION_FAILED)) {
    *out_result = device->execution_result;
  }
  return status;
}
