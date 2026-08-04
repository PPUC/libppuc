#!/bin/bash

set -e

IO_BOARDS_SHA=e1131ec21a6a12b21377d94e86595e8650ae03e4
LIBSERIALPORT_SHA=21b3dfe5f68c205be4086469335fd2fc2ce11ed2
YAML_CPP_SHA=28f93bdec6387d42332220afa9558060c8016795
DOCTEST_VERSION=2.4.11


PROJECT_SOURCE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
SOURCE_DIR_CACHE_BUSTER="${SOURCE_DIR_CACHE_BUSTER:-$(date +%s)}"

dependency_source_dir() {
   local var_name="$1"
   local source_dir="${!var_name:-}"

   if [ -z "${source_dir}" ]; then
      return 0
   fi

   (cd "${PROJECT_SOURCE_ROOT}" && cd "${source_dir}" && pwd -P)
}

ppuc_source_dir_fingerprint() {
   # A stable fingerprint of a local dependency checkout: its commit plus any
   # uncommitted work. Used so that building from a source directory rebuilds
   # when the sources actually changed, rather than on every invocation.
   #
   # Falls back to the timestamp for anything that is not a git checkout, which
   # restores the old always-rebuild behaviour for that case.
   #
   # Kept identical to ppuc/platforms/config.sh: both projects cache their
   # dependencies the same way, and a divergence here would mean a dependency
   # rebuilds in one project but not the other.
   local dir="$1"
   local head
   local dirty

   if ! head="$(git -C "${dir}" rev-parse HEAD 2>/dev/null)"; then
      echo "${SOURCE_DIR_CACHE_BUSTER}"
      return 0
   fi

   # Hash with git rather than shasum/sha1sum: git is definitionally available
   # here (the branch above already used it), whereas shasum is a Perl script
   # that is not guaranteed on every build host. A missing hasher would have
   # silently degraded the fingerprint to commit-only rather than failing.
   dirty="$( {
      # Content of tracked modifications.
      git -C "${dir}" diff HEAD
      # Content of untracked files, so a new or edited untracked source file
      # (a test suite, for instance) still triggers a rebuild.
      git -C "${dir}" ls-files --others --exclude-standard | while read -r f; do
         git hash-object "${dir}/${f}" 2>/dev/null
      done
   } 2>/dev/null | git hash-object --stdin | cut -c1-12 )"

   echo "${head:0:12}-${dirty}"
}

dependency_cache_key() {
   local sha="$1"
   local source_var="$2"
   local source_dir

   source_dir="$(dependency_source_dir "${source_var}")"
   if [ -n "${source_dir}" ]; then
      echo "source:${source_dir}:$(ppuc_source_dir_fingerprint "${source_dir}")"
   else
      echo "${sha}"
   fi
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
