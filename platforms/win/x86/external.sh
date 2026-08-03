#!/bin/bash

set -e

source ./platforms/config.sh

echo "Building libraries..."
echo "  IO_BOARDS_SHA: ${IO_BOARDS_SHA}"
print_dependency_source IO_BOARDS "${IO_BOARDS_SHA}" IO_BOARDS_SOURCE_DIR
echo "  LIBSERIALPORT_SHA: ${LIBSERIALPORT_SHA}"
echo "  YAML_CPP_SHA: ${YAML_CPP_SHA}"
echo ""

# Dependencies are cached exactly the way ppuc caches its own: each one keeps a
# cache.txt marker holding the key it was built from, and its entire build
# branch - including the rm -rf - is skipped while that key still matches and
# the artifacts it stages are still in place.
#
# Note there is no "rm -rf external" here. Wiping the tree up front is what
# made every build rebuild everything, and it would defeat any CI cache.
mkdir -p external
cd external

#
# get io-boards includes
#

IO_BOARDS_EXPECTED_SHA="$(dependency_cache_key "${IO_BOARDS_SHA}" IO_BOARDS_SOURCE_DIR)"
IO_BOARDS_FOUND_SHA="$([ -f io-boards/cache.txt ] && cat io-boards/cache.txt || echo "")"
IO_BOARDS_ARTIFACTS_OK=0
if [ -f "${PROJECT_SOURCE_ROOT}/third-party/include/io-boards/PPUCProtocolV2.h" ] &&
   [ -f "${PROJECT_SOURCE_ROOT}/third-party/include/io-boards/ProtocolConformance.cpp" ]; then
   IO_BOARDS_ARTIFACTS_OK=1
fi

if [ "${IO_BOARDS_EXPECTED_SHA}" != "${IO_BOARDS_FOUND_SHA}" ] || [ "${IO_BOARDS_ARTIFACTS_OK}" -ne 1 ]; then
   echo "Staging io-boards. Expected: ${IO_BOARDS_EXPECTED_SHA}, Found: ${IO_BOARDS_FOUND_SHA}"

   rm -rf io-boards
   mkdir io-boards
   cd io-boards

   prepare_dependency_source io-boards "${IO_BOARDS_SHA}" "https://github.com/PPUC/io-boards/archive/${IO_BOARDS_SHA}.zip" zip IO_BOARDS_SOURCE_DIR
   cp -a io-boards/src/PPUCTimings.h ${PROJECT_SOURCE_ROOT}/third-party/include/io-boards/
   cp -a io-boards/src/PPUCPlatforms.h ${PROJECT_SOURCE_ROOT}/third-party/include/io-boards/
   cp -a io-boards/src/PPUCProtocolV2.h ${PROJECT_SOURCE_ROOT}/third-party/include/io-boards/
   cp -a io-boards/src/EventDispatcher/Event.h ${PROJECT_SOURCE_ROOT}/third-party/include/io-boards/

   # The protocol conformance suite travels with the header it tests, so both
   # sides of the bus assert the same wire contract from identical code.
   cp -a io-boards/test/conformance/ProtocolConformance.h ${PROJECT_SOURCE_ROOT}/third-party/include/io-boards/
   cp -a io-boards/test/conformance/ProtocolConformance.cpp ${PROJECT_SOURCE_ROOT}/third-party/include/io-boards/

   echo "${IO_BOARDS_EXPECTED_SHA}" > cache.txt

   cd ..
fi

#
# build libserialport and copy to platform/arch
#

LIBSERIALPORT_EXPECTED_SHA="${LIBSERIALPORT_SHA}"
LIBSERIALPORT_FOUND_SHA="$([ -f libserialport/cache.txt ] && cat libserialport/cache.txt || echo "")"
LIBSERIALPORT_ARTIFACTS_OK=0
if [ -f "${PROJECT_SOURCE_ROOT}/third-party/include/libserialport.h" ] &&
   [ -f "${PROJECT_SOURCE_ROOT}/third-party/build-libs/win/x86/libserialport.lib" ] &&
   [ -f "${PROJECT_SOURCE_ROOT}/third-party/runtime-libs/win/x86/libserialport.dll" ]; then
   LIBSERIALPORT_ARTIFACTS_OK=1
fi

if [ "${LIBSERIALPORT_EXPECTED_SHA}" != "${LIBSERIALPORT_FOUND_SHA}" ] || [ "${LIBSERIALPORT_ARTIFACTS_OK}" -ne 1 ]; then
   echo "Building libserialport. Expected: ${LIBSERIALPORT_EXPECTED_SHA}, Found: ${LIBSERIALPORT_FOUND_SHA}"

   rm -rf libserialport
   mkdir libserialport
   cd libserialport

   curl -sL https://github.com/sigrokproject/libserialport/archive/${LIBSERIALPORT_SHA}.zip -o libserialport.zip
   unzip libserialport.zip
   cd libserialport-$LIBSERIALPORT_SHA
   cp -a libserialport.h ${PROJECT_SOURCE_ROOT}/third-party/include
   msbuild.exe libserialport.sln \
      -p:Platform=x86 \
      -p:PlatformToolset=v143 \
      -p:Configuration=Release
   cp Release/libserialport.lib ${PROJECT_SOURCE_ROOT}/third-party/build-libs/win/x86
   cp Release/libserialport.dll ${PROJECT_SOURCE_ROOT}/third-party/runtime-libs/win/x86
   cd ..

   echo "${LIBSERIALPORT_EXPECTED_SHA}" > cache.txt

   cd ..
fi

#
# build libyaml-cpp and copy to platform/arch
#

YAML_CPP_EXPECTED_SHA="${YAML_CPP_SHA}"
YAML_CPP_FOUND_SHA="$([ -f yaml-cpp/cache.txt ] && cat yaml-cpp/cache.txt || echo "")"
YAML_CPP_ARTIFACTS_OK=0
if [ -d "${PROJECT_SOURCE_ROOT}/third-party/include/yaml-cpp" ] &&
   [ -f "${PROJECT_SOURCE_ROOT}/third-party/build-libs/win/x86/yaml-cpp.lib" ] &&
   ls "${PROJECT_SOURCE_ROOT}"/third-party/runtime-libs/win/x86/yaml-cpp*.dll >/dev/null 2>&1; then
   YAML_CPP_ARTIFACTS_OK=1
fi

if [ "${YAML_CPP_EXPECTED_SHA}" != "${YAML_CPP_FOUND_SHA}" ] || [ "${YAML_CPP_ARTIFACTS_OK}" -ne 1 ]; then
   echo "Building libyaml-cpp. Expected: ${YAML_CPP_EXPECTED_SHA}, Found: ${YAML_CPP_FOUND_SHA}"

   rm -rf yaml-cpp
   mkdir yaml-cpp
   cd yaml-cpp

   curl -sL https://github.com/jbeder/yaml-cpp/archive/${YAML_CPP_SHA}.zip -o yaml-cpp.zip
   unzip yaml-cpp.zip

   cd yaml-cpp-${YAML_CPP_SHA}
   cp -a include/yaml-cpp ${PROJECT_SOURCE_ROOT}/third-party/include/
   cmake -G "Visual Studio 17 2022" -A Win32 \
     -DYAML_BUILD_SHARED_LIBS=ON \
     -DYAML_CPP_BUILD_CONTRIB=OFF \
     -DYAML_CPP_BUILD_TOOLS=OFF \
     -DYAML_CPP_FORMAT_SOURCE=OFF \
     -DYAML_CPP_INSTALL=OFF \
     -B build
   cmake --build build --config Release
   cp build/Release/yaml-cpp.lib ${PROJECT_SOURCE_ROOT}/third-party/build-libs/win/x86/
   cp build/Release/yaml-cpp*.dll ${PROJECT_SOURCE_ROOT}/third-party/runtime-libs/win/x86/
   cd ..

   echo "${YAML_CPP_EXPECTED_SHA}" > cache.txt

   cd ..
fi

#
# doctest (unit test framework, header only)
#

ppuc_stage_doctest
