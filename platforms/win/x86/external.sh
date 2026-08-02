#!/bin/bash

set -e

source ./platforms/config.sh

echo "Building libraries..."
echo "  IO_BOARDS_SHA: ${IO_BOARDS_SHA}"
print_dependency_source IO_BOARDS "${IO_BOARDS_SHA}" IO_BOARDS_SOURCE_DIR
echo "  LIBSERIALPORT_SHA: ${LIBSERIALPORT_SHA}"
echo "  YAML_CPP_SHA: ${YAML_CPP_SHA}"
echo ""

rm -rf external
mkdir external
cd external

#
# get io-boards includes
#

prepare_dependency_source io-boards "${IO_BOARDS_SHA}" "https://github.com/PPUC/io-boards/archive/${IO_BOARDS_SHA}.zip" zip IO_BOARDS_SOURCE_DIR
cp -a io-boards/src/PPUCTimings.h ${PROJECT_SOURCE_ROOT}/third-party/include/io-boards/
cp -a io-boards/src/PPUCPlatforms.h ${PROJECT_SOURCE_ROOT}/third-party/include/io-boards/
cp -a io-boards/src/PPUCProtocolV2.h ${PROJECT_SOURCE_ROOT}/third-party/include/io-boards/
cp -a io-boards/src/EventDispatcher/Event.h ${PROJECT_SOURCE_ROOT}/third-party/include/io-boards/

# The protocol conformance suite travels with the header it tests, so both
# sides of the bus assert the same wire contract from identical code.
cp -a io-boards/test/conformance/ProtocolConformance.h ${PROJECT_SOURCE_ROOT}/third-party/include/io-boards/
cp -a io-boards/test/conformance/ProtocolConformance.cpp ${PROJECT_SOURCE_ROOT}/third-party/include/io-boards/

#
# build libserialport and copy to platform/arch
#

curl -sL https://github.com/sigrokproject/libserialport/archive/${LIBSERIALPORT_SHA}.zip -o libserialport.zip
unzip libserialport.zip
cd libserialport-$LIBSERIALPORT_SHA
cp -a libserialport.h ${PROJECT_SOURCE_ROOT}/third-party/include
msbuild.exe libserialport.sln \
   -p:Platform=x86 \
   -p:PlatformToolset=v143 \
   -p:Configuration=Release
cp Release/libserialport.lib ${PROJECT_SOURCE_ROOT}/third-party/build-libs/win/x86
cp Release/libserialport.dll ${PROJECT_SOURCE_ROOT}/third-party/runtime-libs/win/x86
cd ..

#
# build libyaml-cpp and copy to platform/arch
#

curl -sL https://github.com/jbeder/yaml-cpp/archive/${YAML_CPP_SHA}.zip -o yaml-cpp.zip
unzip yaml-cpp.zip

cd yaml-cpp-${YAML_CPP_SHA}
cp -a include/yaml-cpp ${PROJECT_SOURCE_ROOT}/third-party/include/
cmake -G "Visual Studio 17 2022" -A Win32 \
  -DYAML_BUILD_SHARED_LIBS=ON \
  -DYAML_CPP_BUILD_CONTRIB=OFF \
  -DYAML_CPP_BUILD_TOOLS=OFF \
  -DYAML_CPP_FORMAT_SOURCE=OFF \
  -DYAML_CPP_INSTALL=OFF \
  -B build
cmake --build build --config Release
cp build/Release/yaml-cpp.lib ${PROJECT_SOURCE_ROOT}/third-party/build-libs/win/x86/
cp build/Release/yaml-cpp*.dll ${PROJECT_SOURCE_ROOT}/third-party/runtime-libs/win/x86/
cd ..

#
# doctest (unit test framework, header only)
#

ppuc_stage_doctest
