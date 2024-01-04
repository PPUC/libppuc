#!/bin/bash

set -e

LIBSERIALPORT_SHA=fd20b0fc5a34cd7f776e4af6c763f59041de223b
YAML_CPP_VERSION=0.8.0

NUM_PROCS=$(sysctl -n hw.ncpu)

echo "Building libraries..."
echo "  LIBSERIALPORT_SHA: ${LIBSERIALPORT_SHA}"
echo "  YAML_CPP_VERSION: ${YAML_CPP_VERSION}"
echo "  NUM_PROCS: ${NUM_PROCS}"
echo ""

rm -rf external
mkdir external
cd external

#
# build libserialport and copy to platform/arch
#

curl -sL https://github.com/sigrokproject/libserialport/archive/${LIBSERIALPORT_SHA}.zip -o libserialport.zip
unzip libserialport.zip
cd libserialport-${LIBSERIALPORT_SHA}
cp libserialport.h ../../third-party/include/
./autogen.sh
./configure --host=x86_64-apple-darwin CFLAGS="-arch x86_64" LDFLAGS="-Wl,-install_name,@rpath/libserialport.dylib"
make -j${NUM_PROCS}
cp .libs/libserialport.a ../../third-party/build-libs/macos/x64/
cp .libs/libserialport.dylib ../../third-party/runtime-libs/macos/x64/
cd ..

#
# build libyaml-cpp and copy to platform/arch
#

curl -sL https://github.com/jbeder/yaml-cpp/archive/refs/tags/${YAML_CPP_VERSION}.zip -o yaml-cpp.zip
unzip yaml-cpp.zip

cd yaml-cpp-${YAML_CPP_VERSION}
cp -r include/yaml-cpp ../../third-party/include/
cmake -DCMAKE_BUILD_TYPE=Release -DYAML_BUILD_SHARED_LIBS=off -DYAML_CPP_BUILD_CONTRIB=off -DYAML_CPP_BUILD_TOOLS=off -DYAML_CPP_FORMAT_SOURCE=off -B build
cmake --build build
cp build/libyaml-cpp.a ../../third-party/build-libs/macos/x64/
cmake -DCMAKE_BUILD_TYPE=Release -DYAML_BUILD_SHARED_LIBS=on -DYAML_CPP_BUILD_CONTRIB=off -DYAML_CPP_BUILD_TOOLS=off -DYAML_CPP_FORMAT_SOURCE=off -B build
cmake --build build
cp build/libyaml-cpp*.dylib ../../third-party/runtime-libs/macos/x64/
cd ..
