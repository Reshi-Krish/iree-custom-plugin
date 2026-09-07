#include "hardwareEmulator.h"

#include <stdlib.h>

struct hw_device {
  int device_id;
  uint8_t* memory;
  size_t memory_size;
  size_t next_free_offset;  // bump allocator watermark
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

  *out_device = device;
  return hw_status_ok();
}

void hw_destroy_hardware(hw_device_t* device) {
  if (!device) return;
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
