/*
 * Generates random A/B matrices, runs matmul_kernel through the hardware
 * emulator, and checks the result against a plain CPU reference matmul.
 *
 * Build:
 *   gcc -std=c11 -Wall -Wextra \
 *     -I../../hardware_emulator/include -I../../hardware_kernels/include \
 *     ../../hardware_emulator/src/hardwareEmulator.c \
 *     ../../hardware_kernels/src/matmul_kernel.c \
 *     matmul_random_test.c -o matmul_random_test -lm
 * Run:
 *   ./matmul_random_test
 */

#include "hardwareEmulator.h"
#include "matmul_kernel.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static float random_float(void) {
  // Uniform in [-1, 1].
  return ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
}

// Reference CPU implementation to check the emulator's kernel against.
static void reference_matmul(const float* a, const float* b, float* c,
                              int64_t m, int64_t n, int64_t k) {
  for (int64_t i = 0; i < m; ++i) {
    for (int64_t j = 0; j < n; ++j) {
      float acc = 0.0f;
      for (int64_t p = 0; p < k; ++p) {
        acc += a[i * k + p] * b[p * n + j];
      }
      c[i * n + j] = acc;
    }
  }
}

int main(void) {
  srand((unsigned int)time(NULL));

  const int64_t m = 8, n = 8, k = 8;
  const size_t a_bytes = (size_t)(m * k) * sizeof(float);
  const size_t b_bytes = (size_t)(k * n) * sizeof(float);
  const size_t c_bytes = (size_t)(m * n) * sizeof(float);

  hw_device_t* device = NULL;
  hw_status_t status =
      hw_init_hardware(/*device_id=*/0, /*memory_size=*/1 << 16, &device);
  if (!hw_status_is_ok(status)) {
    fprintf(stderr, "hw_init_hardware failed: %s\n",
            status.message ? status.message : "(no message)");
    return EXIT_FAILURE;
  }

  hw_buffer_t a_buf, b_buf, c_buf;
  status = hw_allocate(device, a_bytes, &a_buf);
  if (hw_status_is_ok(status)) status = hw_allocate(device, b_bytes, &b_buf);
  if (hw_status_is_ok(status)) status = hw_allocate(device, c_bytes, &c_buf);
  if (!hw_status_is_ok(status)) {
    fprintf(stderr, "hw_allocate failed: %s\n",
            status.message ? status.message : "(no message)");
    hw_destroy_hardware(device);
    return EXIT_FAILURE;
  }

  void *a_ptr, *b_ptr, *c_ptr;
  status = hw_buffer_map(device, a_buf, &a_ptr);
  if (hw_status_is_ok(status)) status = hw_buffer_map(device, b_buf, &b_ptr);
  if (hw_status_is_ok(status)) status = hw_buffer_map(device, c_buf, &c_ptr);
  if (!hw_status_is_ok(status)) {
    fprintf(stderr, "hw_buffer_map failed: %s\n",
            status.message ? status.message : "(no message)");
    hw_destroy_hardware(device);
    return EXIT_FAILURE;
  }

  float* a = (float*)a_ptr;
  float* b = (float*)b_ptr;
  for (int64_t i = 0; i < m * k; ++i) a[i] = random_float();
  for (int64_t i = 0; i < k * n; ++i) b[i] = random_float();

  hw_matmul_params_t shape;
  shape.m = m;
  shape.n = n;
  shape.k = k;
  hw_buffer_t inputs[2] = {a_buf, b_buf};
  hw_buffer_t outputs[1] = {c_buf};
  status = hw_execute(device, matmul_kernel, inputs, 2, outputs, 1, &shape);
  if (!hw_status_is_ok(status)) {
    fprintf(stderr, "hw_execute(matmul_kernel) failed: %s\n",
            status.message ? status.message : "(no message)");
    hw_destroy_hardware(device);
    return EXIT_FAILURE;
  }

  float* expected = (float*)malloc(c_bytes);
  reference_matmul(a, b, expected, m, n, k);

  const float* c = (const float*)c_ptr;
  float max_abs_diff = 0.0f;
  for (int64_t i = 0; i < m * n; ++i) {
    float diff = fabsf(c[i] - expected[i]);
    if (diff > max_abs_diff) max_abs_diff = diff;
  }
  free(expected);
  hw_destroy_hardware(device);

  const float kTolerance = 1e-4f;
  if (max_abs_diff > kTolerance) {
    fprintf(stderr,
            "[FAIL] matmul_random_test: max abs diff %e exceeds tolerance "
            "%e\n",
            max_abs_diff, kTolerance);
    return EXIT_FAILURE;
  }

  printf("[PASS] matmul_random_test (m=%lld n=%lld k=%lld, max abs diff=%e)\n",
         (long long)m, (long long)n, (long long)k, max_abs_diff);
  return EXIT_SUCCESS;
}
