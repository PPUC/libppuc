#!/bin/bash

set -e

IO_BOARDS_SHA=7da3b17d730e546214f5b5fbc9aefe0eb9ff0a9e
LIBSERIALPORT_SHA=21b3dfe5f68c205be4086469335fd2fc2ce11ed2
YAML_CPP_SHA=28f93bdec6387d42332220afa9558060c8016795
DOCTEST_VERSION=2.4.11


PROJECT_SOURCE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"

dependency_source_dir() {
   local var_name="$1"
   local source_dir="${!var_name:-}"

   if [ -z "${source_dir}" ]; then
      return 0
   fi

   (cd "${PROJECT_SOURCE_ROOT}" && cd "${source_dir}" && pwd -P)
}

print_dependency_source() {
   local label="$1"
   local sha="$2"
   local source_var="$3"
   local source_dir

   source_dir="$(dependency_source_dir "${source_var}")"
   if [ -n "${source_dir}" ]; then
      echo "  ${label}_SOURCE_DIR: ${source_dir}"
   else
      echo "  ${label}_SOURCE: archive ${sha}"
   fi
}

prepare_dependency_source() {
   local name="$1"
   local sha="$2"
   local url="$3"
   local archive_type="${4:-tar}"
   local source_var="$5"
   local source_dir

   source_dir="$(dependency_source_dir "${source_var}")"
   if [ -n "${source_dir}" ]; then
      echo "Using ${source_var}: ${source_dir}"
      ln -s "${source_dir}" "${name}"
   elif [ "${archive_type}" = "zip" ]; then
      curl -sL "${url}" -o "${name}.zip"
      unzip "${name}.zip"
      mv "${name}-${sha}" "${name}"
   else
      curl -sL "${url}" -o "${name}-${sha}.tar.gz"
      tar xzf "${name}-${sha}.tar.gz"
      mv "${name}-${sha}" "${name}"
   fi
}



ppuc_stage_doctest() {
   # doctest is a single header used only by the unit tests. It is staged like
   # every other dependency instead of being committed, because
   # third-party/include is generated and gitignored.
   #
   # Cached against the pinned version so repeated builds do not re-fetch it.
   local include_dir="${PROJECT_SOURCE_ROOT}/third-party/include"
   local header="${include_dir}/doctest.h"
   local marker="${include_dir}/doctest.cache.txt"
   local expected="${DOCTEST_VERSION}"
   local found

   found="$([ -f "${marker}" ] && cat "${marker}" || echo "")"

   if [ "${expected}" = "${found}" ] && [ -f "${header}" ]; then
      return 0
   fi

   echo "Staging doctest. Expected: ${expected}, Found: ${found}"
   mkdir -p "${include_dir}"
   curl -sL \
      "https://raw.githubusercontent.com/doctest/doctest/v${DOCTEST_VERSION}/doctest/doctest.h" \
      -o "${header}"
   echo "${expected}" > "${marker}"
}


if [ -z "${BUILD_TYPE}" ]; then
   BUILD_TYPE="Release"
fi

echo "Build type: ${BUILD_TYPE}"
echo ""
