#!/bin/bash

set -e

source ./platforms/config.sh

echo "Building libraries..."
echo "  IO_BOARDS_SHA: ${IO_BOARDS_SHA}"
ppuc_print_dependency_source IO_BOARDS io-boards "${IO_BOARDS_SHA}"
echo "  LIBSERIALPORT_SHA: ${LIBSERIALPORT_SHA}"
echo "  YAML_CPP_SHA: ${YAML_CPP_SHA}"
echo ""

NUM_PROCS=$(nproc)

rm -rf external
mkdir -p external third-party/include/io-boards third-party/build-libs/win-mingw/x64 third-party/runtime-libs/win-mingw/x64
cd external

ppuc_prepare_dependency_source io-boards "${IO_BOARDS_SHA}" "https://github.com/PPUC/io-boards/archive/${IO_BOARDS_SHA}.zip" zip
cp io-boards/src/PPUCTimings.h ${PPUC_SOURCE_ROOT}/third-party/include/io-boards/
cp io-boards/src/PPUCPlatforms.h ${PPUC_SOURCE_ROOT}/third-party/include/io-boards/
cp io-boards/src/PPUCProtocolV2.h ${PPUC_SOURCE_ROOT}/third-party/include/io-boards/
cp io-boards/src/EventDispatcher/Event.h ${PPUC_SOURCE_ROOT}/third-party/include/io-boards/

curl -sL https://github.com/sigrokproject/libserialport/archive/${LIBSERIALPORT_SHA}.zip -o libserialport.zip
unzip libserialport.zip
cd libserialport-${LIBSERIALPORT_SHA}
cp libserialport.h ${PPUC_SOURCE_ROOT}/third-party/include/
./autogen.sh
./configure --enable-shared
make -j${NUM_PROCS}
cp .libs/libserialport.dll.a ${PPUC_SOURCE_ROOT}/third-party/build-libs/win-mingw/x64/libserialport64.dll.a
cp .libs/libserialport-0.dll ${PPUC_SOURCE_ROOT}/third-party/runtime-libs/win-mingw/x64/libserialport64-0.dll
cd ..

curl -sL https://github.com/jbeder/yaml-cpp/archive/${YAML_CPP_SHA}.zip -o yaml-cpp.zip
unzip yaml-cpp.zip
cd yaml-cpp-${YAML_CPP_SHA}
cp -r include/yaml-cpp ${PPUC_SOURCE_ROOT}/third-party/include/
cmake \
  -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
  -DYAML_BUILD_SHARED_LIBS=ON \
  -DYAML_CPP_BUILD_CONTRIB=OFF \
  -DYAML_CPP_BUILD_TOOLS=OFF \
  -DYAML_CPP_FORMAT_SOURCE=OFF \
  -DYAML_CPP_INSTALL=OFF \
  -B build
cmake --build build -- -j${NUM_PROCS}
cp build/libyaml-cpp.dll.a ${PPUC_SOURCE_ROOT}/third-party/build-libs/win-mingw/x64/
cp build/libyaml-cpp.dll ${PPUC_SOURCE_ROOT}/third-party/runtime-libs/win-mingw/x64/
cd ..

UCRT64_BIN="${MINGW_PREFIX}/bin"
cp "${UCRT64_BIN}/libgcc_s_seh-1.dll" ${PPUC_SOURCE_ROOT}/third-party/runtime-libs/win-mingw/x64/
cp "${UCRT64_BIN}/libstdc++-6.dll" ${PPUC_SOURCE_ROOT}/third-party/runtime-libs/win-mingw/x64/
cp "${UCRT64_BIN}/libwinpthread-1.dll" ${PPUC_SOURCE_ROOT}/third-party/runtime-libs/win-mingw/x64/
