cmake_minimum_required(VERSION 3.25)

set(PLATFORM "win" CACHE STRING "Platform")
set(ARCH "x64" CACHE STRING "Arch")

option(BUILD_SHARED "Option to build shared library" ON)
option(BUILD_STATIC "Option to build static library" ON)
option(ENABLE_SANITIZERS "Enable address/undefined sanitizers for supported Debug builds" OFF)
option(BUILD_TESTS "Option to build unit tests" OFF)

message(STATUS "PLATFORM: ${PLATFORM}")
message(STATUS "ARCH: ${ARCH}")

message(STATUS "BUILD_SHARED: ${BUILD_SHARED}")
message(STATUS "BUILD_STATIC: ${BUILD_STATIC}")
message(STATUS "ENABLE_SANITIZERS: ${ENABLE_SANITIZERS}")
message(STATUS "BUILD_TESTS: ${BUILD_TESTS}")

file(READ src/PPUC.h version)
string(REGEX MATCH "#[ \t]*define[ \t]+PPUC_VERSION_MAJOR[ \t]+([0-9]+)" _tmp "${version}")
if("${CMAKE_MATCH_1}" STREQUAL "")
   message(FATAL_ERROR "Failed to parse PPUC_VERSION_MAJOR from src/PPUC.h")
endif()
set(VERSION_MAJOR "${CMAKE_MATCH_1}")
string(REGEX MATCH "#[ \t]*define[ \t]+PPUC_VERSION_MINOR[ \t]+([0-9]+)" _tmp "${version}")
if("${CMAKE_MATCH_1}" STREQUAL "")
   message(FATAL_ERROR "Failed to parse PPUC_VERSION_MINOR from src/PPUC.h")
endif()
set(VERSION_MINOR "${CMAKE_MATCH_1}")
string(REGEX MATCH "#[ \t]*define[ \t]+PPUC_VERSION_PATCH[ \t]+([0-9]+)" _tmp "${version}")
if("${CMAKE_MATCH_1}" STREQUAL "")
   message(FATAL_ERROR "Failed to parse PPUC_VERSION_PATCH from src/PPUC.h")
endif()
set(VERSION_PATCH "${CMAKE_MATCH_1}")

project(ppuc VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}"
   DESCRIPTION "Cross-platform library for communicating with PPUC boards.")

if(PLATFORM STREQUAL "win")
   if(ARCH STREQUAL "x86")
      add_compile_definitions(WIN32)
   endif()
elseif(PLATFORM STREQUAL "win-mingw")
   add_compile_options(-Wa,-mbig-obj)
elseif(PLATFORM STREQUAL "macos")
   if (ARCH STREQUAL "arm64")
      set(CMAKE_OSX_ARCHITECTURES arm64)
   elseif(ARCH STREQUAL "x64")
      set(CMAKE_OSX_ARCHITECTURES x86_64)
   endif()
   set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
   set(CMAKE_INSTALL_RPATH "@executable_path")
elseif(PLATFORM STREQUAL "linux" OR PLATFORM STREQUAL "android")
   set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
   set(CMAKE_INSTALL_RPATH "$ORIGIN")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD 99)

set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_C_VISIBILITY_PRESET hidden)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
   if(ENABLE_SANITIZERS AND (PLATFORM STREQUAL "macos" OR PLATFORM STREQUAL "linux"))
      set(SANITIZER_FLAGS -fsanitize=address,undefined)
      add_compile_options(${SANITIZER_FLAGS} -fno-omit-frame-pointer -g)
      add_link_options(${SANITIZER_FLAGS})
   endif()
endif()

set(PPUC_SOURCES
   src/RS485Comm.h
   src/RS485Comm.cpp
   src/PPUC.h
   src/PPUC.cpp
   src/PPUC_structs.h
)

set(PPUC_INCLUDE_DIRS
   src
   third-party/include
)

if(BUILD_SHARED)
   add_library(ppuc_shared SHARED ${PPUC_SOURCES})

   target_include_directories(ppuc_shared PUBLIC ${PPUC_INCLUDE_DIRS})

   if(PLATFORM STREQUAL "win")
      target_link_directories(ppuc_shared PUBLIC
         third-party/build-libs/${PLATFORM}/${ARCH}
         third-party/runtime-libs/${PLATFORM}/${ARCH}
      )
      if(ARCH STREQUAL "x64")
         target_link_libraries(ppuc_shared PUBLIC libserialport64 yaml-cpp)
      else()
         target_link_libraries(ppuc_shared PUBLIC libserialport yaml-cpp)
      endif()
   elseif(PLATFORM STREQUAL "win-mingw")
      target_link_directories(ppuc_shared PUBLIC
         third-party/build-libs/${PLATFORM}/${ARCH}
         third-party/runtime-libs/${PLATFORM}/${ARCH}
      )
      target_link_libraries(ppuc_shared PUBLIC serialport64 yaml-cpp)
   elseif(PLATFORM STREQUAL "macos")
      target_link_directories(ppuc_shared PUBLIC
         third-party/runtime-libs/${PLATFORM}/${ARCH}
      )
      target_link_libraries(ppuc_shared PUBLIC serialport yaml-cpp)
   elseif(PLATFORM STREQUAL "linux")
      target_link_directories(ppuc_shared PUBLIC
         third-party/runtime-libs/${PLATFORM}/${ARCH}
      )
      target_link_libraries(ppuc_shared PUBLIC -l:libserialport.so.0 -l:libyaml-cpp.so.0.8.0)
   endif()

   if((PLATFORM STREQUAL "win" OR PLATFORM STREQUAL "win-mingw") AND ARCH STREQUAL "x64")
      set(PPUC_OUTPUT_NAME "ppuc64")
   else()
      set(PPUC_OUTPUT_NAME "ppuc")
   endif()

   if(PLATFORM STREQUAL "win-mingw")
      set_target_properties(ppuc_shared PROPERTIES
         PREFIX ""
         IMPORT_PREFIX ""
         OUTPUT_NAME ${PPUC_OUTPUT_NAME}
         VERSION ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}
      )
   else()
      set_target_properties(ppuc_shared PROPERTIES
         OUTPUT_NAME ${PPUC_OUTPUT_NAME}
         VERSION ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}
      )
   endif()

   install(TARGETS ppuc_shared
      LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/lib
   )
   install(FILES src/PPUC.h DESTINATION ${CMAKE_INSTALL_PREFIX}/include)
endif()

if(BUILD_STATIC)
   add_library(ppuc_static STATIC ${PPUC_SOURCES})

   target_include_directories(ppuc_static PUBLIC ${PPUC_INCLUDE_DIRS})

   if(PLATFORM STREQUAL "win" OR PLATFORM STREQUAL "win-mingw")
      set_target_properties(ppuc_static PROPERTIES
         PREFIX ""
         OUTPUT_NAME "ppuc_static"
      )
   else()
      set_target_properties(ppuc_static PROPERTIES
         OUTPUT_NAME "ppuc"
      )
   endif()

   install(TARGETS ppuc_static
      LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/lib
   )
   install(FILES src/PPUC.h DESTINATION ${CMAKE_INSTALL_PREFIX}/include)
endif()

if(BUILD_TESTS)
   enable_testing()

   # The unit tests compile the library sources directly rather than linking
   # ppuc_static, so they do not depend on BUILD_STATIC and can later reach
   # internal helpers if needed.
   add_executable(ppuc_tests
      ${PPUC_SOURCES}
      tests/main.cpp
      tests/ConfigFixture.h
      tests/test_config_validation.cpp
      tests/test_switch_groups.cpp
      tests/test_coil_gi_mappings.cpp
      tests/test_pwm_output.cpp
   )

   target_include_directories(ppuc_tests PRIVATE ${PPUC_INCLUDE_DIRS} tests)

   if(PLATFORM STREQUAL "win")
      target_link_directories(ppuc_tests PRIVATE
         third-party/build-libs/${PLATFORM}/${ARCH}
         third-party/runtime-libs/${PLATFORM}/${ARCH}
      )
      if(ARCH STREQUAL "x64")
         target_link_libraries(ppuc_tests PRIVATE libserialport64 yaml-cpp)
      else()
         target_link_libraries(ppuc_tests PRIVATE libserialport yaml-cpp)
      endif()
   elseif(PLATFORM STREQUAL "win-mingw")
      target_link_directories(ppuc_tests PRIVATE
         third-party/build-libs/${PLATFORM}/${ARCH}
         third-party/runtime-libs/${PLATFORM}/${ARCH}
      )
      target_link_libraries(ppuc_tests PRIVATE serialport64 yaml-cpp)
   elseif(PLATFORM STREQUAL "macos")
      target_link_directories(ppuc_tests PRIVATE
         third-party/runtime-libs/${PLATFORM}/${ARCH}
      )
      target_link_libraries(ppuc_tests PRIVATE serialport yaml-cpp)
   elseif(PLATFORM STREQUAL "linux")
      target_link_directories(ppuc_tests PRIVATE
         third-party/runtime-libs/${PLATFORM}/${ARCH}
      )
      target_link_libraries(ppuc_tests PRIVATE -l:libserialport.so.0 -l:libyaml-cpp.so.0.8.0)
   endif()

   # The library targets are linked with an @executable_path / $ORIGIN rpath so
   # they can be shipped next to a host application. The test binary is never
   # installed and runs from the build tree, so point it straight at the staged
   # runtime libraries instead.
   if(PLATFORM STREQUAL "macos" OR PLATFORM STREQUAL "linux")
      set_target_properties(ppuc_tests PROPERTIES
         INSTALL_RPATH "${CMAKE_CURRENT_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}"
      )
   elseif(PLATFORM STREQUAL "win" OR PLATFORM STREQUAL "win-mingw")
      # Windows has no rpath; copy the runtime DLLs next to the test binary.
      add_custom_command(TARGET ppuc_tests POST_BUILD
         COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}"
            "$<TARGET_FILE_DIR:ppuc_tests>"
      )
   endif()

   add_test(NAME ppuc_tests COMMAND ppuc_tests)
endif()
