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

rm -rf external
mkdir -p external third-party/include/io-boards third-party/build-libs/win-mingw/x64 third-party/runtime-libs/win-mingw/x64
cd external

prepare_dependency_source io-boards "${IO_BOARDS_SHA}" "https://github.com/PPUC/io-boards/archive/${IO_BOARDS_SHA}.zip" zip IO_BOARDS_SOURCE_DIR
cp -a io-boards/src/PPUCTimings.h ${PROJECT_SOURCE_ROOT}/third-party/include/io-boards/
cp -a io-boards/src/PPUCPlatforms.h ${PROJECT_SOURCE_ROOT}/third-party/include/io-boards/
cp -a io-boards/src/PPUCProtocolV2.h ${PROJECT_SOURCE_ROOT}/third-party/include/io-boards/
cp -a io-boards/src/EventDispatcher/Event.h ${PROJECT_SOURCE_ROOT}/third-party/include/io-boards/

# The protocol conformance suite travels with the header it tests, so both
# sides of the bus assert the same wire contract from identical code.
cp -a io-boards/test/conformance/ProtocolConformance.h ${PROJECT_SOURCE_ROOT}/third-party/include/io-boards/
cp -a io-boards/test/conformance/ProtocolConformance.cpp ${PROJECT_SOURCE_ROOT}/third-party/include/io-boards/

curl -sL https://github.com/sigrokproject/libserialport/archive/${LIBSERIALPORT_SHA}.zip -o libserialport.zip
unzip libserialport.zip
cd libserialport-${LIBSERIALPORT_SHA}
cp -a libserialport.h ${PROJECT_SOURCE_ROOT}/third-party/include/
./autogen.sh
./configure --enable-shared
make -j${NUM_PROCS}
cp -a .libs/libserialport.dll.a ${PROJECT_SOURCE_ROOT}/third-party/build-libs/win-mingw/x64/libserialport64.dll.a
cp -a .libs/libserialport-0.dll ${PROJECT_SOURCE_ROOT}/third-party/runtime-libs/win-mingw/x64/libserialport64-0.dll
cd ..

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
cp -a build/libyaml-cpp.dll.a ${PROJECT_SOURCE_ROOT}/third-party/build-libs/win-mingw/x64/
cp -a build/libyaml-cpp.dll ${PROJECT_SOURCE_ROOT}/third-party/runtime-libs/win-mingw/x64/
cd ..

UCRT64_BIN="${MINGW_PREFIX}/bin"
cp -a "${UCRT64_BIN}/libgcc_s_seh-1.dll" ${PROJECT_SOURCE_ROOT}/third-party/runtime-libs/win-mingw/x64/
cp -a "${UCRT64_BIN}/libstdc++-6.dll" ${PROJECT_SOURCE_ROOT}/third-party/runtime-libs/win-mingw/x64/
cp -a "${UCRT64_BIN}/libwinpthread-1.dll" ${PROJECT_SOURCE_ROOT}/third-party/runtime-libs/win-mingw/x64/

#
# doctest (unit test framework, header only)
#

ppuc_stage_doctest
