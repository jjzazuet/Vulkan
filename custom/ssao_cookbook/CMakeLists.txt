# Minimal standalone port of the Vulkan 3D Graphics Rendering Cookbook SSAO.
# No Bistro, no LightweightVK, no ImGui, no window, no MSAA.

add_executable(ssao_cookbook main.cpp)
target_include_directories(ssao_cookbook PRIVATE ${CMAKE_SOURCE_DIR}/base)
target_link_libraries(ssao_cookbook PRIVATE base)
target_compile_definitions(ssao_cookbook PRIVATE
  SSAO_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders"
  SSAO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
set_target_properties(ssao_cookbook PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
  VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
