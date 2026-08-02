#!/bin/bash

set -e

source ./platforms/config.sh

NUM_PROCS=$(nproc)

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
./autogen.sh
./configure
make -j${NUM_PROCS}
# Plain cp, not "cp -a": libtool emits .libs/libserialport.so.0 (and friends) as
# symlinks to the versioned real file. "cp -a" implies -P and would stage a
# dangling symlink, which fails at link time with "cannot find -l:...".
cp .libs/libserialport.a ${PROJECT_SOURCE_ROOT}/third-party/build-libs/linux/aarch64/
cp .libs/libserialport.so.0 ${PROJECT_SOURCE_ROOT}/third-party/runtime-libs/linux/aarch64/
cd ..

#
# build libyaml-cpp and copy to platform/arch
#

curl -sL https://github.com/jbeder/yaml-cpp/archive/${YAML_CPP_SHA}.zip -o yaml-cpp.zip
unzip yaml-cpp.zip

cd yaml-cpp-${YAML_CPP_SHA}
cp -a include/yaml-cpp ${PROJECT_SOURCE_ROOT}/third-party/include/
cmake -DYAML_BUILD_SHARED_LIBS=ON \
  -DYAML_CPP_BUILD_CONTRIB=OFF \
  -DYAML_CPP_BUILD_TOOLS=OFF \
  -DYAML_CPP_FORMAT_SOURCE=OFF \
  -DYAML_CPP_INSTALL=OFF \
  -B build
cmake --build build --config Release
cp -P build/libyaml-cpp.so.* ${PROJECT_SOURCE_ROOT}/third-party/runtime-libs/linux/aarch64/
cd ..

#
# doctest (unit test framework, header only)
#

ppuc_stage_doctest
