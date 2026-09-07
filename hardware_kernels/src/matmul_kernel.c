#include "matmul_kernel.h"

hw_status_t matmul_kernel(void* const* inputs, size_t num_inputs,
                           void* const* outputs, size_t num_outputs,
                           void* params) {
  if (num_inputs != 2 || num_outputs != 1 || !params) {
    hw_status_t status;
    status.code = HW_STATUS_INVALID_ARGUMENT;
    status.message =
        "matmul_kernel expects 2 inputs, 1 output, and non-NULL params";
    return status;
  }

  const hw_matmul_params_t* shape = (const hw_matmul_params_t*)params;
  if (shape->m <= 0 || shape->n <= 0 || shape->k <= 0) {
    hw_status_t status;
    status.code = HW_STATUS_INVALID_ARGUMENT;
    status.message = "matmul dimensions must be positive";
    return status;
  }

  const float* a = (const float*)inputs[0];  // [m, k], row-major
  const float* b = (const float*)inputs[1];  // [k, n], row-major
  float* c = (float*)outputs[0];             // [m, n], row-major

  for (int64_t i = 0; i < shape->m; ++i) {
    for (int64_t j = 0; j < shape->n; ++j) {
      float acc = 0.0f;
      for (int64_t p = 0; p < shape->k; ++p) {
        acc += a[i * shape->k + p] * b[p * shape->n + j];
      }
      c[i * shape->n + j] = acc;
    }
  }

  return hw_status_ok();
}
