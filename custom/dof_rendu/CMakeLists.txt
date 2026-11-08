# Minimal standalone port of Rendu's depth-of-field stack.
# No Rendu engine, Bistro, LightweightVK, ImGui, MSAA, or window.

add_executable(dof_rendu main.cpp)
target_include_directories(dof_rendu PRIVATE ${CMAKE_SOURCE_DIR}/base)
target_link_libraries(dof_rendu PRIVATE base)
target_compile_definitions(dof_rendu PRIVATE
  DOF_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders"
  DOF_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
set_target_properties(dof_rendu PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
  VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
