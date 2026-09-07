/*
 * Initializes the hardware emulator and dispatches template_kernel through
 * hw_execute() -- the smallest possible round trip through the emulator ABI.
 *
 * Build:
 *   gcc -std=c11 -Wall -Wextra \
 *     -I../../hardware_emulator/include -I../../hardware_kernels/include \
 *     ../../hardware_emulator/src/hardwareEmulator.c \
 *     ../../hardware_kernels/src/template_kernel.c \
 *     init_template_test.c -o init_template_test
 * Run:
 *   ./init_template_test
 */

#include "hardwareEmulator.h"
#include "template_kernel.h"

#include <stdio.h>
#include <stdlib.h>

int main(void) {
  hw_device_t* device = NULL;
  hw_status_t status =
      hw_init_hardware(/*device_id=*/0, /*memory_size=*/4096, &device);
  if (!hw_status_is_ok(status)) {
    fprintf(stderr, "hw_init_hardware failed: %s\n",
            status.message ? status.message : "(no message)");
    return EXIT_FAILURE;
  }

  status = hw_execute(device, template_kernel, /*inputs=*/NULL,
                       /*num_inputs=*/0, /*outputs=*/NULL, /*num_outputs=*/0,
                       /*params=*/NULL);
  if (!hw_status_is_ok(status)) {
    fprintf(stderr, "hw_execute(template_kernel) failed: %s\n",
            status.message ? status.message : "(no message)");
    hw_destroy_hardware(device);
    return EXIT_FAILURE;
  }

  hw_destroy_hardware(device);
  printf("[PASS] init_template_test\n");
  return EXIT_SUCCESS;
}
