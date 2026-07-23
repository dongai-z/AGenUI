#!/usr/bin/env bash
# Pack AGenUI Studio into a distributable zip archive.
#
# Usage:
#   ./scripts/pack-studio.sh [output_dir]
#
# Output: <output_dir>/agenui-studio.zip (default: dist/)
#
# Contents of the zip:
#   agenui-studio/
#   ├── playground/studio/  Python server + static frontend + web source
#   ├── samples/            Preset A2UI protocol examples
#   └── skills/             a2ui-generation skill (prompt + validator)

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUTPUT_DIR="${1:-${REPO_ROOT}/dist}"
mkdir -p "${OUTPUT_DIR}"
OUTPUT_DIR="$(cd "${OUTPUT_DIR}" && pwd)"
STAGE_DIR="${OUTPUT_DIR}/agenui-studio"
ZIP_FILE="${OUTPUT_DIR}/agenui-studio.zip"

echo "[pack-studio] Repo root: ${REPO_ROOT}"
echo "[pack-studio] Output:    ${ZIP_FILE}"

# --- clean previous build ---
rm -rf "${STAGE_DIR}" "${ZIP_FILE}"
mkdir -p "${STAGE_DIR}"

# --- copy studio server (exclude __pycache__, node_modules) ---
echo "[pack-studio] Copying playground/studio/ ..."
mkdir -p "${STAGE_DIR}/playground"
rsync -a \
  --exclude='__pycache__' \
  --exclude='node_modules' \
  --exclude='*.pyc' \
  "${REPO_ROOT}/playground/studio/" "${STAGE_DIR}/playground/studio/"

# --- copy playground/__init__.py (package marker) ---
cp "${REPO_ROOT}/playground/__init__.py" "${STAGE_DIR}/playground/"

# --- copy samples ---
echo "[pack-studio] Copying samples/ ..."
rsync -a "${REPO_ROOT}/samples/" "${STAGE_DIR}/samples/"

# --- copy required skill directory (prompt builder + validator) ---
echo "[pack-studio] Copying skills/a2ui-generation/ ..."
mkdir -p "${STAGE_DIR}/skills/a2ui-generation"
rsync -a \
  --exclude='__pycache__' \
  --exclude='*.pyc' \
  "${REPO_ROOT}/skills/a2ui-generation/" "${STAGE_DIR}/skills/a2ui-generation/"

# --- create zip ---
echo "[pack-studio] Creating zip ..."
cd "${OUTPUT_DIR}"
rm -f agenui-studio.zip
zip -qr agenui-studio.zip agenui-studio/

# --- report ---
SIZE=$(du -h "${ZIP_FILE}" | cut -f1)
echo "[pack-studio] Done: ${ZIP_FILE} (${SIZE})"
