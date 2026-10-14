cmake_minimum_required(VERSION 3.20)

project(vpxhost VERSION 0.1.0 LANGUAGES CXX)

# The vpinball plugin SDK: plugins/plugins (MsgPlugin.h, MsgPluginManager.cpp
# and the other plugin API headers) and third-party/include (mINI, plog) of a
# vpinball checkout. Applications pass the checkout they build their plugins
# from, so the host and the plugins agree on the SDK.
set(VPXHOST_VPINBALL_DIR "" CACHE PATH "vpinball checkout providing the plugin SDK")
option(VPXHOST_BUILD_TESTS "Build the tests and their test plugin" ${PROJECT_IS_TOP_LEVEL})

if(VPXHOST_VPINBALL_DIR STREQUAL "")
   message(FATAL_ERROR "Set VPXHOST_VPINBALL_DIR to a vpinball checkout.")
endif()
get_filename_component(VPXHOST_VPINBALL_DIR "${VPXHOST_VPINBALL_DIR}" ABSOLUTE)
foreach(file
      plugins/plugins/MsgPlugin.h
      plugins/plugins/MsgPluginManager.h
      plugins/plugins/MsgPluginManager.cpp
      plugins/plugins/LoggingPlugin.h
      plugins/plugins/ScriptablePlugin.h
      third-party/include/mINI/ini.h
      third-party/include/unordered_dense.h)
   if(NOT EXISTS "${VPXHOST_VPINBALL_DIR}/${file}")
      message(FATAL_ERROR "VPXHOST_VPINBALL_DIR has no ${file}: ${VPXHOST_VPINBALL_DIR}")
   endif()
endforeach()

# The plugin API uses designated initializers.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static: MsgPluginManager holds the bus of one process, and each application
# links exactly one copy of it.
add_library(vpxhost STATIC
   src/PluginBus.cpp
   "${VPXHOST_VPINBALL_DIR}/plugins/plugins/MsgPluginManager.cpp"
)
add_library(vpxhost::vpxhost ALIAS vpxhost)
target_include_directories(vpxhost PUBLIC
   "${CMAKE_CURRENT_SOURCE_DIR}/include"
   "${VPXHOST_VPINBALL_DIR}/plugins"
   "${VPXHOST_VPINBALL_DIR}/third-party/include"
)
set_target_properties(vpxhost PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(WIN32)
   target_compile_definitions(vpxhost PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
else()
   target_link_libraries(vpxhost PUBLIC ${CMAKE_DL_LIBS})
endif()

if(VPXHOST_BUILD_TESTS)
   enable_testing()

   # The plugin goes into <build>/plugins/vpxhosttest with its plugin.cfg, where
   # the bus finds it.
   add_library(VpxHostTestPlugin MODULE tests/testplugin/TestPlugin.cpp)
   target_include_directories(VpxHostTestPlugin PRIVATE "${VPXHOST_VPINBALL_DIR}/plugins")
   set(VPXHOST_TEST_PLUGIN_NAME "plugin-vpxhosttest")
   if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
      set(VPXHOST_TEST_PLUGIN_NAME "plugin-vpxhosttest64")
   endif()
   set_target_properties(VpxHostTestPlugin PROPERTIES OUTPUT_NAME "${VPXHOST_TEST_PLUGIN_NAME}" PREFIX "")
   if(APPLE)
      # plugin.cfg names .dylib, as every other plugin does.
      set_target_properties(VpxHostTestPlugin PROPERTIES SUFFIX ".dylib")
   endif()
   set(VPXHOST_TEST_PLUGIN_DIR "${CMAKE_BINARY_DIR}/plugins/vpxhosttest")
   add_custom_command(TARGET VpxHostTestPlugin POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${VPXHOST_TEST_PLUGIN_DIR}"
      COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_SOURCE_DIR}/tests/testplugin/plugin.cfg"
              "${VPXHOST_TEST_PLUGIN_DIR}/plugin.cfg"
      COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:VpxHostTestPlugin>" "${VPXHOST_TEST_PLUGIN_DIR}"
   )

   add_executable(vpxhost_tests tests/plugin_bus_tests.cpp)
   target_link_libraries(vpxhost_tests PRIVATE vpxhost)
   add_dependencies(vpxhost_tests VpxHostTestPlugin)
   add_test(NAME vpxhost_tests COMMAND vpxhost_tests "${CMAKE_BINARY_DIR}/plugins")
endif()
