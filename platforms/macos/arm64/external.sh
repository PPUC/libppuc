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

rm -rf external
mkdir external
cd external

#
# get io-boards includes
#

prepare_dependency_source io-boards "${IO_BOARDS_SHA}" "https://github.com/PPUC/io-boards/archive/${IO_BOARDS_SHA}.zip" zip IO_BOARDS_SOURCE_DIR
cp io-boards/src/PPUCTimings.h ${PROJECT_SOURCE_ROOT}/third-party/include/io-boards/
cp io-boards/src/PPUCPlatforms.h ${PROJECT_SOURCE_ROOT}/third-party/include/io-boards/
cp io-boards/src/PPUCProtocolV2.h ${PROJECT_SOURCE_ROOT}/third-party/include/io-boards/
cp io-boards/src/EventDispatcher/Event.h ${PROJECT_SOURCE_ROOT}/third-party/include/io-boards/

#
# build libserialport and copy to platform/arch
#

curl -sL https://github.com/sigrokproject/libserialport/archive/${LIBSERIALPORT_SHA}.zip -o libserialport.zip
unzip libserialport.zip
cd libserialport-${LIBSERIALPORT_SHA}
cp libserialport.h ${PROJECT_SOURCE_ROOT}/third-party/include/
./autogen.sh
./configure --host=aarch64-apple-darwin CFLAGS="-arch arm64" LDFLAGS="-Wl,-install_name,@rpath/libserialport.dylib"
make -j${NUM_PROCS}
cp .libs/libserialport.a ${PROJECT_SOURCE_ROOT}/third-party/build-libs/macos/arm64/
cp .libs/libserialport.dylib ${PROJECT_SOURCE_ROOT}/third-party/runtime-libs/macos/arm64/
cd ..

#
# build libyaml-cpp and copy to platform/arch
#

curl -sL https://github.com/jbeder/yaml-cpp/archive/${YAML_CPP_SHA}.zip -o yaml-cpp.zip
unzip yaml-cpp.zip

cd yaml-cpp-${YAML_CPP_SHA}
cp -r include/yaml-cpp ${PROJECT_SOURCE_ROOT}/third-party/include/
cmake -DYAML_BUILD_SHARED_LIBS=ON \
  -DYAML_CPP_BUILD_CONTRIB=OFF \
  -DYAML_CPP_BUILD_TOOLS=OFF \
  -DYAML_CPP_FORMAT_SOURCE=OFF \
  -DYAML_CPP_INSTALL=OFF \
  -B build
cmake --build build --config Release
cp -P build/libyaml-cpp*.dylib ${PROJECT_SOURCE_ROOT}/third-party/runtime-libs/macos/arm64/
cd ..
