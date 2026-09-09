iree_setup_c_src_root()

# hardware_emulator is a plain C library living alongside this plugin in the
# iree-custom-plugin repo (not inside the IREE source tree, and built
# standalone by that repo's own top-level CMakeLists.txt too). It isn't an
# iree_cc_library, so wrap it with external_cc_library (IREE's helper for
# exactly this: third-party/out-of-tree sources) rather than add_subdirectory
# on its plain CMakeLists.txt - a plain target linked into an installed/
# exported iree_cc_library (like hw_runtime_plugin::hw_emulator below) trips
# CMake's install(EXPORT) validation because it isn't part of any export set.
set(HW_EMULATOR_ROOT "${CMAKE_CURRENT_LIST_DIR}/../hardware_emulator/")
external_cc_library(
  PACKAGE
    hardware_emulator
  NAME
    hardware_emulator
  ROOT
    ${HW_EMULATOR_ROOT}
  HDRS
    "include/hardwareEmulator.h"
  SRCS
    "src/hardwareEmulator.c"
  INCLUDES
    "${HW_EMULATOR_ROOT}/include"
  PUBLIC
)

# Same rationale as hardware_emulator above: a plain library from the parent
# repo, wrapped so it participates correctly in the export set. Used only by
# tests (real kernels to dispatch against, e.g. matmul_kernel) - the driver
# itself has no compile-time dependency on any particular kernel.
set(HW_KERNELS_ROOT "${CMAKE_CURRENT_LIST_DIR}/../hardware_kernels/")
external_cc_library(
  PACKAGE
    hardware_kernels
  NAME
    hardware_kernels
  ROOT
    ${HW_KERNELS_ROOT}
  HDRS
    "include/template_kernel.h"
    "include/matmul_kernel.h"
  SRCS
    "src/template_kernel.c"
    "src/matmul_kernel.c"
  INCLUDES
    "${HW_KERNELS_ROOT}/include"
  DEPS
    hardware_emulator::hardware_emulator
  PUBLIC
)

add_subdirectory(hw_runtime_plugin)
