#!/bin/bash

set -e

source ./platforms/config.sh

NUM_PROCS=$(sysctl -n hw.ncpu)

echo "Building libraries..."
echo "  IO_BOARDS_SHA: ${IO_BOARDS_SHA}"
print_dependency_source IO_BOARDS "${IO_BOARDS_SHA}" IO_BOARDS_SOURCE_DIR
echo "  LIBSERIALPORT_SHA: ${LIBSERIALPORT_SHA}"
echo "  YAML_CPP_SHA: ${YAML_CPP_SHA}"
echo "  NUM_PROCS: ${NUM_PROCS}"
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
   [ -f "${PROJECT_SOURCE_ROOT}/third-party/build-libs/macos/arm64/libserialport.a" ] &&
   [ -f "${PROJECT_SOURCE_ROOT}/third-party/runtime-libs/macos/arm64/libserialport.dylib" ]; then
   LIBSERIALPORT_ARTIFACTS_OK=1
fi

if [ "${LIBSERIALPORT_EXPECTED_SHA}" != "${LIBSERIALPORT_FOUND_SHA}" ] || [ "${LIBSERIALPORT_ARTIFACTS_OK}" -ne 1 ]; then
   echo "Building libserialport. Expected: ${LIBSERIALPORT_EXPECTED_SHA}, Found: ${LIBSERIALPORT_FOUND_SHA}"

   rm -rf libserialport
   mkdir libserialport
   cd libserialport

   curl -sL https://github.com/sigrokproject/libserialport/archive/${LIBSERIALPORT_SHA}.zip -o libserialport.zip
   unzip libserialport.zip
   cd libserialport-${LIBSERIALPORT_SHA}
   cp -a libserialport.h ${PROJECT_SOURCE_ROOT}/third-party/include/
   ./autogen.sh
   ./configure --host=aarch64-apple-darwin CFLAGS="-arch arm64" LDFLAGS="-Wl,-install_name,@rpath/libserialport.dylib"
   make -j${NUM_PROCS}
   # Plain cp, not "cp -a": libtool emits .libs/libserialport.dylib (and its
   # Linux equivalents) as symlinks to the versioned real file. "cp -a" implies
   # -P and would stage a dangling symlink, which fails at link time with
   # "cannot find -l:...".
   cp .libs/libserialport.a ${PROJECT_SOURCE_ROOT}/third-party/build-libs/macos/arm64/
   cp .libs/libserialport.dylib ${PROJECT_SOURCE_ROOT}/third-party/runtime-libs/macos/arm64/
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
   ls "${PROJECT_SOURCE_ROOT}"/third-party/runtime-libs/macos/arm64/libyaml-cpp*.dylib >/dev/null 2>&1; then
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
   cmake -DYAML_BUILD_SHARED_LIBS=ON \
     -DYAML_CPP_BUILD_CONTRIB=OFF \
     -DYAML_CPP_BUILD_TOOLS=OFF \
     -DYAML_CPP_FORMAT_SOURCE=OFF \
     -DYAML_CPP_INSTALL=OFF \
     -B build
   cmake --build build --config Release
   cp -P build/libyaml-cpp*.dylib ${PROJECT_SOURCE_ROOT}/third-party/runtime-libs/macos/arm64/
   cd ..

   echo "${YAML_CPP_EXPECTED_SHA}" > cache.txt

   cd ..
fi

#
# doctest (unit test framework, header only)
#

ppuc_stage_doctest
