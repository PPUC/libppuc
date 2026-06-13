#!/bin/bash

set -e

source ./platforms/config.sh

echo "Building libraries..."
echo "  IO_BOARDS_SHA: ${IO_BOARDS_SHA}"
echo "  LIBSERIALPORT_SHA: ${LIBSERIALPORT_SHA}"
echo "  YAML_CPP_SHA: ${YAML_CPP_SHA}"
echo ""

NUM_PROCS=$(nproc)

rm -rf external
mkdir -p external third-party/include/io-boards third-party/build-libs/win-mingw/x64 third-party/runtime-libs/win-mingw/x64
cd external

curl -sL https://github.com/PPUC/io-boards/archive/${IO_BOARDS_SHA}.zip -o io-boards.zip
unzip io-boards.zip
cd io-boards-${IO_BOARDS_SHA}
cp src/PPUCTimings.h ../../third-party/include/io-boards/
cp src/PPUCPlatforms.h ../../third-party/include/io-boards/
cp src/PPUCProtocolV2.h ../../third-party/include/io-boards/
cp src/EventDispatcher/Event.h ../../third-party/include/io-boards/
cd ..

curl -sL https://github.com/sigrokproject/libserialport/archive/${LIBSERIALPORT_SHA}.zip -o libserialport.zip
unzip libserialport.zip
cd libserialport-${LIBSERIALPORT_SHA}
cp libserialport.h ../../third-party/include/
./autogen.sh
./configure --enable-shared
make -j${NUM_PROCS}
cp .libs/libserialport.dll.a ../../third-party/build-libs/win-mingw/x64/libserialport64.dll.a
cp .libs/libserialport-0.dll ../../third-party/runtime-libs/win-mingw/x64/libserialport64-0.dll
cd ..

curl -sL https://github.com/jbeder/yaml-cpp/archive/${YAML_CPP_SHA}.zip -o yaml-cpp.zip
unzip yaml-cpp.zip
cd yaml-cpp-${YAML_CPP_SHA}
cp -r include/yaml-cpp ../../third-party/include/
cmake \
  -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
  -DYAML_BUILD_SHARED_LIBS=ON \
  -DYAML_CPP_BUILD_CONTRIB=OFF \
  -DYAML_CPP_BUILD_TOOLS=OFF \
  -DYAML_CPP_FORMAT_SOURCE=OFF \
  -DYAML_CPP_INSTALL=OFF \
  -B build
cmake --build build -- -j${NUM_PROCS}
cp build/libyaml-cpp.dll.a ../../third-party/build-libs/win-mingw/x64/
cp build/libyaml-cpp.dll ../../third-party/runtime-libs/win-mingw/x64/
cd ..

UCRT64_BIN="${MINGW_PREFIX}/bin"
cp "${UCRT64_BIN}/libgcc_s_seh-1.dll" ../third-party/runtime-libs/win-mingw/x64/
cp "${UCRT64_BIN}/libstdc++-6.dll" ../third-party/runtime-libs/win-mingw/x64/
cp "${UCRT64_BIN}/libwinpthread-1.dll" ../third-party/runtime-libs/win-mingw/x64/
