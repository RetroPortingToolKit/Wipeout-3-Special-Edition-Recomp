#!/usr/bin/env bash
# Player ("lean") package: the game, ready to run — no source tree, no SDK,
# no rebuild wizard. Companion to package_setup_release.sh, which builds the
# full setup-host zip.
#
# Usage:
#   scripts/package_lean_release.sh <build-dir> <artifact-tag> [recompiler-build-dir]
#
# Writes: dist/wse-<VERSION>-<artifact-tag>-lean.zip
#         (the -lean suffix keeps it from overwriting the setup zip)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${1:-}"
ARTIFACT_TAG="${2:-}"
RECOMPILER_BUILD="${3:-build-recompiler}"

if [[ -z "${BUILD_DIR}" || -z "${ARTIFACT_TAG}" ]]; then
  echo "usage: $0 <build-dir> <artifact-tag> [recompiler-build-dir]" >&2
  exit 2
fi

PACKAGER="${ROOT}/psxrecomp/tools/package_setup_host.sh"
if [[ ! -f "${PACKAGER}" ]]; then
  echo "error: missing ${PACKAGER} (psxrecomp submodule)" >&2
  exit 1
fi
chmod +x "${PACKAGER}" 2>/dev/null || true

cd "${ROOT}"
exec bash "${PACKAGER}" \
  --lean \
  --root "${ROOT}" \
  --build-dir "${BUILD_DIR}" \
  --artifact "${ARTIFACT_TAG}" \
  --zip-prefix wse \
  --exe-name Wipeout_3_Special_Edition__Recompiled \
  --display-name "Wipeout 3 Special Edition  Recompiled" \
  --recompiler-build "${RECOMPILER_BUILD}" \
  --version-env RELEASE_VERSION \
  --disc-hint "your legally owned Wipeout 3 Special Edition disc" \
  --project-file game.toml \
  --project-file VERSION \
  `# CMakeLists.txt is shipped as a project-root MARKER, not to build from:` \
  `# find_project_root() (config_loader.cpp) walks up to 8 levels looking for` \
  `# .git/.gitignore/CMakeLists.txt, so without one here an install placed` \
  `# inside any git checkout would anchor its project root somewhere upstream` \
  `# of itself. The setup zip ships it too, so both shapes resolve alike.` \
  --project-file CMakeLists.txt \
  --project-dir translations \
  --project-dir assets \
  --project-dir hd_textures \
  --project-dir mods \
  --runtime-dir-optional mods \
  --runtime-dir-optional bezels \
  `# Precompiled overlay shards — the whole point of the lean bundle. A player` \
  `# install cannot regenerate these from scratch cheaply, and anything not in` \
  `# here runs on the dirty-RAM interpreter until autocompile catches up (and` \
  `# only if python3 + a C compiler are present). Play the content before` \
  `# packaging so the cache actually covers it.` \
  --runtime-dir-optional cache
