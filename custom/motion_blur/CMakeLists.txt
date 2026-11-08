# Minimal standalone baseline for the zt-gfx camera-only motion blur (B6).
add_executable(motion_blur main.cpp)
target_include_directories(motion_blur PRIVATE ${CMAKE_SOURCE_DIR}/base)
target_link_libraries(motion_blur PRIVATE base)
target_compile_definitions(motion_blur PRIVATE
  MOTION_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set_target_properties(motion_blur PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
  VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
