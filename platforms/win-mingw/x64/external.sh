#!/bin/bash

set -e

source ./platforms/config.sh

echo "Building libraries..."
echo "  IO_BOARDS_SHA: ${IO_BOARDS_SHA}"
print_dependency_source IO_BOARDS "${IO_BOARDS_SHA}" IO_BOARDS_SOURCE_DIR
echo "  LIBSERIALPORT_SHA: ${LIBSERIALPORT_SHA}"
echo "  YAML_CPP_SHA: ${YAML_CPP_SHA}"
echo ""

NUM_PROCS=$(nproc)

# Dependencies are cached exactly the way ppuc caches its own: each one keeps a
# cache.txt marker holding the key it was built from, and its entire build
# branch - including the rm -rf - is skipped while that key still matches and
# the artifacts it stages are still in place.
#
# Note there is no "rm -rf external" here. Wiping the tree up front is what
# made every build rebuild everything, and it would defeat any CI cache.
mkdir -p external third-party/include/io-boards third-party/build-libs/win-mingw/x64 third-party/runtime-libs/win-mingw/x64
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
   [ -f "${PROJECT_SOURCE_ROOT}/third-party/build-libs/win-mingw/x64/libserialport64.dll.a" ] &&
   [ -f "${PROJECT_SOURCE_ROOT}/third-party/runtime-libs/win-mingw/x64/libserialport64-0.dll" ]; then
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
   ./configure --enable-shared
   make -j${NUM_PROCS}
   # Plain cp, not "cp -a": libtool emits .libs/libserialport.so.0 (and friends) as
   # symlinks to the versioned real file. "cp -a" implies -P and would stage a
   # dangling symlink, which fails at link time with "cannot find -l:...".
   cp .libs/libserialport.dll.a ${PROJECT_SOURCE_ROOT}/third-party/build-libs/win-mingw/x64/libserialport64.dll.a
   cp .libs/libserialport-0.dll ${PROJECT_SOURCE_ROOT}/third-party/runtime-libs/win-mingw/x64/libserialport64-0.dll
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
   [ -f "${PROJECT_SOURCE_ROOT}/third-party/build-libs/win-mingw/x64/libyaml-cpp.dll.a" ] &&
   [ -f "${PROJECT_SOURCE_ROOT}/third-party/runtime-libs/win-mingw/x64/libyaml-cpp.dll" ]; then
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
   cmake \
     -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
     -DYAML_BUILD_SHARED_LIBS=ON \
     -DYAML_CPP_BUILD_CONTRIB=OFF \
     -DYAML_CPP_BUILD_TOOLS=OFF \
     -DYAML_CPP_FORMAT_SOURCE=OFF \
     -DYAML_CPP_INSTALL=OFF \
     -B build
   cmake --build build -- -j${NUM_PROCS}
   cp build/libyaml-cpp.dll.a ${PROJECT_SOURCE_ROOT}/third-party/build-libs/win-mingw/x64/
   cp build/libyaml-cpp.dll ${PROJECT_SOURCE_ROOT}/third-party/runtime-libs/win-mingw/x64/
   cd ..

   echo "${YAML_CPP_EXPECTED_SHA}" > cache.txt

   cd ..
fi

# The MinGW runtime DLLs are copied out of the toolchain, not built here, so
# they are not cached behind a marker - the copies are trivial and the source
# is whatever toolchain this run happens to have.
UCRT64_BIN="${MINGW_PREFIX}/bin"
cp "${UCRT64_BIN}/libgcc_s_seh-1.dll" ${PROJECT_SOURCE_ROOT}/third-party/runtime-libs/win-mingw/x64/
cp "${UCRT64_BIN}/libstdc++-6.dll" ${PROJECT_SOURCE_ROOT}/third-party/runtime-libs/win-mingw/x64/
cp "${UCRT64_BIN}/libwinpthread-1.dll" ${PROJECT_SOURCE_ROOT}/third-party/runtime-libs/win-mingw/x64/

#
# doctest (unit test framework, header only)
#

ppuc_stage_doctest
