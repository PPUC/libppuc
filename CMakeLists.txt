cmake_minimum_required(VERSION 3.25)

set(PLATFORM "win" CACHE STRING "Platform")
set(ARCH "x64" CACHE STRING "Arch")

option(BUILD_SHARED "Option to build shared library" ON)
option(BUILD_STATIC "Option to build static library" ON)

message(STATUS "PLATFORM: ${PLATFORM}")
message(STATUS "ARCH: ${ARCH}")

message(STATUS "BUILD_SHARED: ${BUILD_SHARED}")
message(STATUS "BUILD_STATIC: ${BUILD_STATIC}")

file(READ src/PPUC.h version)
string(REGEX MATCH "PPUC_VERSION_MAJOR[ ]+([0-9]+)" _tmp ${version})
set(VERSION_MAJOR "${CMAKE_MATCH_1}")
string(REGEX MATCH "PPUC_VERSION_MINOR[ ]+([0-9]+)" _tmp ${version})
set(VERSION_MINOR "${CMAKE_MATCH_1}")
string(REGEX MATCH "PPUC_VERSION_PATCH[ ]+([0-9]+)" _tmp ${version})
set(VERSION_PATCH "${CMAKE_MATCH_1}")

project(ppuc VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}"
   DESCRIPTION "Cross-platform library for communicating with PPUC boards.")

if(PLATFORM STREQUAL "win")
   if(ARCH STREQUAL "x86")
      add_compile_definitions(WIN32)
   endif()
elseif(PLATFORM STREQUAL "macos")
   set(CMAKE_OSX_DEPLOYMENT_TARGET 13.0)
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

set(PPUC_SOURCES
   src/RS485Comm.h
   src/RS485Comm.cpp
   src/PPUC.h
   src/PPUC.cpp
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
      target_link_libraries(ppuc_shared PUBLIC libserialport yaml-cpp)
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

   if(PLATFORM STREQUAL "win" AND ARCH STREQUAL "x64")
      set(PPUC_OUTPUT_NAME "ppuc64")
   else()
      set(PPUC_OUTPUT_NAME "ppuc")
   endif()

   set_target_properties(ppuc_shared PROPERTIES
      OUTPUT_NAME ${PPUC_OUTPUT_NAME}
      VERSION ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}
   )

   install(TARGETS ppuc_shared
      LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/lib
   )
   install(FILES src/PPUC.h DESTINATION ${CMAKE_INSTALL_PREFIX}/include)

   if(PLATFORM STREQUAL "win" OR PLATFORM STREQUAL "macos" OR PLATFORM STREQUAL "linux")
      add_executable(ppuc_test
         src/test.cpp
      )

      target_link_libraries(ppuc_test PUBLIC ppuc_shared)

      if(PLATFORM STREQUAL "win")
         add_custom_command(TARGET ppuc_test POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/build-libs/${PLATFORM}/${ARCH}/*.lib" "$<TARGET_FILE_DIR:ppuc_test>"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/*.dll" "$<TARGET_FILE_DIR:ppuc_test>"
         )
      elseif(PLATFORM STREQUAL "macos")
         add_custom_command(TARGET ppuc_test POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/*.dylib" "$<TARGET_FILE_DIR:ppuc_test>"
         )
      elseif(PLATFORM STREQUAL "linux")
         add_custom_command(TARGET ppuc_test POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/*.so.*" "$<TARGET_FILE_DIR:ppuc_test>"
         )
      endif()
   endif()
endif()

if(BUILD_STATIC)
   add_library(ppuc_static STATIC ${PPUC_SOURCES})

   target_include_directories(ppuc_static PUBLIC ${PPUC_INCLUDE_DIRS})

   if(PLATFORM STREQUAL "win")
      set_target_properties(ppuc_static PROPERTIES
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

   if(PLATFORM STREQUAL "win" OR PLATFORM STREQUAL "macos" OR PLATFORM STREQUAL "linux")
      add_executable(ppuc_test_s
         src/test.cpp
      )

      if(PLATFORM STREQUAL "win")
         target_link_directories(ppuc_test_s PUBLIC
            third-party/build-libs/${PLATFORM}/${ARCH}
            third-party/runtime-libs/${PLATFORM}/${ARCH}
         )
         target_link_libraries(ppuc_test_s PUBLIC ppuc_static libserialport yaml-cpp)

         add_custom_command(TARGET ppuc_test_s POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/build-libs/${PLATFORM}/${ARCH}/*.lib" "$<TARGET_FILE_DIR:ppuc_test_s>"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/*.dll" "$<TARGET_FILE_DIR:ppuc_test_s>"
         )
      elseif(PLATFORM STREQUAL "macos")
         target_link_directories(ppuc_test_s PUBLIC
            third-party/runtime-libs/${PLATFORM}/${ARCH}
         )
         target_link_libraries(ppuc_test_s PUBLIC ppuc_static serialport yaml-cpp)

         add_custom_command(TARGET ppuc_test_s POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/*.dylib" "$<TARGET_FILE_DIR:ppuc_test_s>"
         )
      elseif(PLATFORM STREQUAL "linux")
         target_link_directories(ppuc_test_s PUBLIC
            third-party/runtime-libs/${PLATFORM}/${ARCH}
         )
         target_link_libraries(ppuc_test_s PUBLIC ppuc_static -l:libserialport.so.0 -l:libyaml-cpp.so.0.8.0)

         add_custom_command(TARGET ppuc_test_s POST_BUILD
           COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/${PLATFORM}/${ARCH}/*.so.*" "$<TARGET_FILE_DIR:ppuc_test_s>"
         )
      endif()
   endif()
endif()
