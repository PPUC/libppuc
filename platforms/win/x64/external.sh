#!/bin/bash

set -e

IO_BOARDS_SHA=main
LIBSERIALPORT_SHA=fd20b0fc5a34cd7f776e4af6c763f59041de223b
YAML_CPP_VERSION=0.8.0

echo "Building libraries..."
echo "  IO_BOARDS_SHA: ${IO_BOARDS_SHA}"
echo "  LIBSERIALPORT_SHA: ${LIBSERIALPORT_SHA}"
echo "  YAML_CPP_VERSION: ${YAML_CPP_VERSION}"
echo ""

rm -rf external
mkdir external
cd external

#
# get io-boards includes
#

curl -sL https://github.com/PPUC/io-boards/archive/${IO_BOARDS_SHA}.zip -o io-boards.zip
unzip io-boards.zip
cd io-boards-${IO_BOARDS_SHA}
cp src/PPUCPlatforms.h ../../third-party/include/io-boards/
cp src/EventDispatcher/Event.h ../../third-party/include/io-boards/
cd ..

#
# build libserialport and copy to platform/arch
#

curl -sL https://github.com/sigrokproject/libserialport/archive/${LIBSERIALPORT_SHA}.zip -o libserialport.zip
unzip libserialport.zip
cd libserialport-$LIBSERIALPORT_SHA
cp libserialport.h ../../third-party/include
msbuild.exe libserialport.sln -p:Configuration=Release -p:Platform=x64
cp x64/Release/libserialport.lib ../../third-party/build-libs/win/x64/
cp x64/Release/libserialport.dll ../../third-party/runtime-libs/win/x64/
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
cp build/yaml-cpp.lib ../../third-party/build-libs/win/x64/
cmake -DCMAKE_BUILD_TYPE=Release -DYAML_BUILD_SHARED_LIBS=on -DYAML_CPP_BUILD_CONTRIB=off -DYAML_CPP_BUILD_TOOLS=off -DYAML_CPP_FORMAT_SOURCE=off -B build
cmake --build build
cp build/yaml-cpp*.dll ../../third-party/runtime-libs/win/x64/
cd ..
