# Fragment-only McGuire reconstruction filter fixture for zt-gfx.
add_executable(motion_blur_mcguire main.cpp)
target_include_directories(motion_blur_mcguire PRIVATE ${CMAKE_SOURCE_DIR}/base)
target_link_libraries(motion_blur_mcguire PRIVATE base)
target_compile_definitions(motion_blur_mcguire PRIVATE
  MOTION_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set_target_properties(motion_blur_mcguire PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
  VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
