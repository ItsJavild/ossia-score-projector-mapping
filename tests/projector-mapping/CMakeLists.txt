cmake_minimum_required(VERSION 3.24)
project(ProjectorMappingChecks LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
find_package(Qt6 REQUIRED COMPONENTS Widgets Test)
set(GFX "${CMAKE_CURRENT_LIST_DIR}/../../src/plugins/score-plugin-gfx")
add_executable(projector-mapping-checks
  MappingChecks.cpp
  ${GFX}/Gfx/Window/OutputMappingCanvas.cpp
  ${GFX}/Gfx/Window/OutputPreview.cpp
  ${GFX}/Gfx/Window/TestCard.cpp)
target_include_directories(projector-mapping-checks PRIVATE ${GFX})
target_link_libraries(projector-mapping-checks PRIVATE Qt6::Widgets Qt6::Test)
enable_testing()
add_test(NAME projector-mapping-checks COMMAND projector-mapping-checks)
set_tests_properties(projector-mapping-checks PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

if(APPLE OR WIN32)
  find_package(Qt6 REQUIRED COMPONENTS ShaderTools)
  if(NOT TARGET Qt6::GuiPrivate)
    find_package(Qt6 REQUIRED COMPONENTS GuiPrivate)
  endif()
  if(NOT TARGET Qt6::ShaderToolsPrivate)
    find_package(Qt6 REQUIRED COMPONENTS ShaderToolsPrivate)
  endif()
  add_executable(projector-shader-pixel-checks ShaderPixelChecks.cpp)
  target_link_libraries(projector-shader-pixel-checks PRIVATE Qt6::GuiPrivate Qt6::ShaderToolsPrivate)
  target_compile_definitions(projector-shader-pixel-checks PRIVATE PROJECTOR_GFX_ROOT="${GFX}")
  add_test(NAME projector-shader-pixel-checks COMMAND projector-shader-pixel-checks)
  set_tests_properties(projector-shader-pixel-checks PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" SKIP_RETURN_CODE 77 TIMEOUT 90)
endif()
