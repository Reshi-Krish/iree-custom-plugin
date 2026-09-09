#ifndef HARDWARE_EMULATOR_H_
#define HARDWARE_EMULATOR_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//===----------------------------------------------------------------------===
// Status
//===----------------------------------------------------------------------===

typedef enum hw_status_code {
  HW_STATUS_OK = 0,
  HW_STATUS_INVALID_ARGUMENT,
  HW_STATUS_OUT_OF_MEMORY,
  HW_STATUS_OUT_OF_RANGE,
  HW_STATUS_INTERNAL_ERROR,
} hw_status_code_t;

typedef struct hw_status {
  hw_status_code_t code;
  // Static/string-literal message, may be NULL. Not owned.
  const char* message;
} hw_status_t;

static inline hw_status_t hw_status_ok(void) {
  hw_status_t status;
  status.code = HW_STATUS_OK;
  status.message = NULL;
  return status;
}

static inline int hw_status_is_ok(hw_status_t status) {
  return status.code == HW_STATUS_OK;
}

//===----------------------------------------------------------------------===
// Device
//===----------------------------------------------------------------------===
// Opaque handle representing one emulated hardware instance and the single
// memory pool backing it. Definition lives in hardwareEmulator.c only.

typedef struct hw_device hw_device_t;

// Allocates the emulated hardware's full memory pool (|memory_size| bytes)
// and returns a handle to it in |out_device|. The caller owns the returned
// device and must release it with hw_destroy_hardware().
hw_status_t hw_init_hardware(int device_id, size_t memory_size,
                              hw_device_t** out_device);

// Releases a device created by hw_init_hardware(), freeing its memory pool.
// No-op if |device| is NULL.
void hw_destroy_hardware(hw_device_t* device);

//===----------------------------------------------------------------------===
// Buffers
//===----------------------------------------------------------------------===
// A buffer handle identifies a region of the device's memory pool by offset
// and size. It is not a host pointer: call hw_buffer_map() to obtain one.

typedef struct hw_buffer {
  uint64_t offset;
  uint64_t size;
} hw_buffer_t;

// Sub-allocates |size| bytes out of |device|'s memory pool and returns a
// handle to the region in |out_buffer|. Bump-allocated: there is no
// corresponding free(); the whole pool is released by hw_destroy_hardware().
hw_status_t hw_allocate(hw_device_t* device, size_t size,
                         hw_buffer_t* out_buffer);

// Resolves |buffer| to a host-accessible pointer within |device|'s memory
// pool. The returned pointer is valid until the device is destroyed.
hw_status_t hw_buffer_map(hw_device_t* device, hw_buffer_t buffer,
                           void** out_ptr);

//===----------------------------------------------------------------------===
// Kernel execution
//===----------------------------------------------------------------------===
// A kernel is a plain function that operates on host pointers already
// resolved from device buffers. hw_execute() performs that resolution and
// invokes the kernel; kernel authors never touch hw_buffer_t or hw_device_t
// directly.

typedef hw_status_t (*hw_kernel_fn_t)(void* const* inputs, size_t num_inputs,
                                       void* const* outputs,
                                       size_t num_outputs, void* params);

// Resolves each of |inputs|/|outputs| to a host pointer via hw_buffer_map()
// and invokes |kernel| with the resolved pointer arrays. |params| is passed
// through to the kernel unchanged (e.g. a shape descriptor); may be NULL.
hw_status_t hw_execute(hw_device_t* device, hw_kernel_fn_t kernel,
                        hw_buffer_t* inputs, size_t num_inputs,
                        hw_buffer_t* outputs, size_t num_outputs,
                        void* params);

//===----------------------------------------------------------------------===
// Asynchronous execution
//===----------------------------------------------------------------------===
// Models how real hardware behaves: the host submits work and gets control
// back immediately, while the "device" (here, a background thread) runs the
// kernel and later flips a status flag that the host is expected to poll via
// hw_query_execution_status() -- standing in for a real device's completion/
// doorbell register. Only one asynchronous execution may be in flight per
// device at a time.

typedef enum hw_execution_status {
  HW_EXECUTION_IDLE = 0,  // no asynchronous execution has been submitted yet
  HW_EXECUTION_RUNNING,   // kernel is running on the background thread
  HW_EXECUTION_COMPLETE,  // kernel finished and returned HW_STATUS_OK
  HW_EXECUTION_FAILED,    // kernel or buffer resolution returned an error
} hw_execution_status_t;

// Copies |inputs|/|outputs| and invokes |kernel| on a new background thread,
// returning immediately. Fails with HW_STATUS_INTERNAL_ERROR if a previous
// asynchronous execution on this device is still running.
hw_status_t hw_execute_async(hw_device_t* device, hw_kernel_fn_t kernel,
                              hw_buffer_t* inputs, size_t num_inputs,
                              hw_buffer_t* outputs, size_t num_outputs,
                              void* params);

// Non-blocking query of the status of the most recent hw_execute_async()
// call. If the execution has finished (successfully or not), |out_result|
// (if non-NULL) is set to the kernel's/buffer-resolution's returned status;
// left untouched otherwise (i.e. while HW_EXECUTION_IDLE or _RUNNING).
hw_execution_status_t hw_query_execution_status(hw_device_t* device,
                                                 hw_status_t* out_result);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // HARDWARE_EMULATOR_H_
