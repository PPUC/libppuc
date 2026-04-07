#!/bin/bash

set -e

IO_BOARDS_SHA=4af2cf60fccc0ae82e37f006a8f5bd67ddb58f3c
LIBSERIALPORT_SHA=21b3dfe5f68c205be4086469335fd2fc2ce11ed2
YAML_CPP_SHA=28f93bdec6387d42332220afa9558060c8016795


if [ -z "${BUILD_TYPE}" ]; then
   BUILD_TYPE="Release"
fi

echo "Build type: ${BUILD_TYPE}"
echo ""
