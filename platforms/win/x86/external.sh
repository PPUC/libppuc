#!/bin/bash

set -e

source ./platforms/config.sh

NUM_PROCS=$(sysctl -n hw.ncpu)

echo "Building libraries..."
echo "  IO_BOARDS_SHA: ${IO_BOARDS_SHA}"
echo "  LIBSERIALPORT_SHA: ${LIBSERIALPORT_SHA}"
echo "  YAML_CPP_SHA: ${YAML_CPP_SHA}"
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
cp src/PPUCTimings.h ../../third-party/include/io-boards/
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
msbuild.exe libserialport.sln \
   -p:Platform=x86 \
   -p:PlatformToolset=v143 \
   -p:Configuration=Release
cp Release/libserialport.lib ../../third-party/build-libs/win/x86
cp Release/libserialport.dll ../../third-party/runtime-libs/win/x86
cd ..

#
# build libyaml-cpp and copy to platform/arch
#

curl -sL https://github.com/jbeder/yaml-cpp/archive/${YAML_CPP_SHA}.zip -o yaml-cpp.zip
unzip yaml-cpp.zip

cd yaml-cpp-${YAML_CPP_SHA}
cp -r include/yaml-cpp ../../third-party/include/
cmake -G "Visual Studio 17 2022" -A Win32 \
  -DYAML_BUILD_SHARED_LIBS=ON \
  -DYAML_CPP_BUILD_CONTRIB=OFF \
  -DYAML_CPP_BUILD_TOOLS=OFF \
  -DYAML_CPP_FORMAT_SOURCE=OFF \
  -DYAML_CPP_INSTALL=OFF \
  -B build
cmake --build build --config Release
cp build/Release/yaml-cpp.lib ../../third-party/build-libs/win/x86/
cp build/Release/yaml-cpp*.dll ../../third-party/runtime-libs/win/x86/
cd ..
