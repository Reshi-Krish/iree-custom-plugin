iree_add_all_subdirs()

iree_cc_library(
  NAME
    defs
  INCLUDES
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/src>"
  PUBLIC
)

iree_cc_library(
  NAME
    registration
  SRCS
    "src/PluginRegistration.cpp"
  DEPS
    ::defs
    LLVMSupport
    MLIRIR
    MLIRPass
    MLIRSupport
    iree::compiler::Dialect::HAL::Target
    iree::compiler::PluginAPI
  PUBLIC
)

iree_compiler_register_plugin(
  PLUGIN_ID
    hw_emulator
  TARGET
    ::registration
)
