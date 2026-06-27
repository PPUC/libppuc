#!/bin/bash

set -e

source ./platforms/config.sh

echo "Building libraries..."
echo "  IO_BOARDS_SHA: ${IO_BOARDS_SHA}"
ppuc_print_dependency_source IO_BOARDS io-boards "${IO_BOARDS_SHA}"
echo "  LIBSERIALPORT_SHA: ${LIBSERIALPORT_SHA}"
echo "  YAML_CPP_SHA: ${YAML_CPP_SHA}"
echo ""

rm -rf external
mkdir external
cd external

#
# get io-boards includes
#

ppuc_prepare_dependency_source io-boards "${IO_BOARDS_SHA}" "https://github.com/PPUC/io-boards/archive/${IO_BOARDS_SHA}.zip" zip
cp io-boards/src/PPUCTimings.h ${PPUC_SOURCE_ROOT}/third-party/include/io-boards/
cp io-boards/src/PPUCPlatforms.h ${PPUC_SOURCE_ROOT}/third-party/include/io-boards/
cp io-boards/src/PPUCProtocolV2.h ${PPUC_SOURCE_ROOT}/third-party/include/io-boards/
cp io-boards/src/EventDispatcher/Event.h ${PPUC_SOURCE_ROOT}/third-party/include/io-boards/

#
# build libserialport and copy to platform/arch
#

curl -sL https://github.com/sigrokproject/libserialport/archive/${LIBSERIALPORT_SHA}.zip -o libserialport.zip
unzip libserialport.zip
cd libserialport-$LIBSERIALPORT_SHA
cp libserialport.h ${PPUC_SOURCE_ROOT}/third-party/include
patch libserialport.vcxproj < ../../platforms/win/x64/libserialport/001.patch
msbuild.exe libserialport.sln -p:Configuration=Release -p:Platform=x64
cp x64/Release/libserialport64.lib ${PPUC_SOURCE_ROOT}/third-party/build-libs/win/x64
cp x64/Release/libserialport64.dll ${PPUC_SOURCE_ROOT}/third-party/runtime-libs/win/x64
cd ..


#
# build libyaml-cpp and copy to platform/arch
#

curl -sL https://github.com/jbeder/yaml-cpp/archive/${YAML_CPP_SHA}.zip -o yaml-cpp.zip
unzip yaml-cpp.zip

cd yaml-cpp-${YAML_CPP_SHA}
cp -r include/yaml-cpp ${PPUC_SOURCE_ROOT}/third-party/include/
cmake -G "Visual Studio 17 2022" \
  -DYAML_BUILD_SHARED_LIBS=ON \
  -DYAML_CPP_BUILD_CONTRIB=OFF \
  -DYAML_CPP_BUILD_TOOLS=OFF \
  -DYAML_CPP_FORMAT_SOURCE=OFF \
  -DYAML_CPP_INSTALL=OFF \
  -B build
cmake --build build --config Release
cp build/Release/yaml-cpp.lib ${PPUC_SOURCE_ROOT}/third-party/build-libs/win/x64/
cp build/Release/yaml-cpp*.dll ${PPUC_SOURCE_ROOT}/third-party/runtime-libs/win/x64/
cd ..
