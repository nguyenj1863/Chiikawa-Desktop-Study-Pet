#!/usr/bin/env bash
set -euo pipefail

# Package ChiikawaStudyPet.app into a standalone drag-to-Applications DMG for macOS.
# Usage: ./scripts/package_macos.sh <arm64|x64> [build_dir]

ARCH_LABEL="${1:-arm64}"
BUILD_DIR="${2:-build}"
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
DIST_DIR="${ROOT_DIR}/dist"
STAGING_DIR="${DIST_DIR}/dmg-staging-${ARCH_LABEL}"
APP_NAME="ChiikawaStudyPet.app"
SRC_APP="${ROOT_DIR}/${BUILD_DIR}/${APP_NAME}"
DST_APP="${STAGING_DIR}/${APP_NAME}"
DMG_NAME="ChiikawaStudyPet-macOS-${ARCH_LABEL}.dmg"
DMG_PATH="${DIST_DIR}/${DMG_NAME}"

if [[ ! -d "${SRC_APP}" ]]; then
    echo "Error: ${SRC_APP} not found. Build Release first." >&2
    exit 1
fi

MACDEPLOYQT="${MACDEPLOYQT_EXECUTABLE:-$(command -v macdeployqt || true)}"
if [[ -z "${MACDEPLOYQT}" && -n "${CMAKE_PREFIX_PATH:-}" ]]; then
    CANDIDATE="${CMAKE_PREFIX_PATH}/bin/macdeployqt"
    if [[ -x "${CANDIDATE}" ]]; then
        MACDEPLOYQT="${CANDIDATE}"
    fi
fi
if [[ -z "${MACDEPLOYQT}" ]]; then
    echo "Error: macdeployqt not found. Add Qt bin/ to PATH or set CMAKE_PREFIX_PATH." >&2
    exit 1
fi

rm -rf "${STAGING_DIR}" "${DMG_PATH}"
mkdir -p "${STAGING_DIR}"
cp -R "${SRC_APP}" "${DST_APP}"

# Generate chiikawa.icns from bundled artwork using native macOS sips + iconutil
ICONSET_DIR="${DIST_DIR}/chiikawa.iconset"
rm -rf "${ICONSET_DIR}"
mkdir -p "${ICONSET_DIR}"
BASE_PNG="${DIST_DIR}/chiikawa_base.png"
if sips -s format png "${ROOT_DIR}/assets/chiikawa_twirl.gif" --out "${BASE_PNG}" >/dev/null 2>&1; then
    for sz in 16 32 128 256 512; do
        sips -z "${sz}" "${sz}" "${BASE_PNG}" --out "${ICONSET_DIR}/icon_${sz}x${sz}.png" >/dev/null 2>&1 || true
        sz2=$((sz * 2))
        sips -z "${sz2}" "${sz2}" "${BASE_PNG}" --out "${ICONSET_DIR}/icon_${sz}x${sz}@2x.png" >/dev/null 2>&1 || true
    done
    mkdir -p "${DST_APP}/Contents/Resources"
    if iconutil -c icns "${ICONSET_DIR}" -o "${DST_APP}/Contents/Resources/chiikawa.icns" >/dev/null 2>&1; then
        echo "Generated chiikawa.icns"
    fi
fi
rm -rf "${ICONSET_DIR}" "${BASE_PNG}"

# Bundle license and third-party notices inside the app and DMG root
mkdir -p "${DST_APP}/Contents/Resources"
for doc in LICENSE THIRD_PARTY_NOTICES.md; do
    if [[ -f "${ROOT_DIR}/${doc}" ]]; then
        cp "${ROOT_DIR}/${doc}" "${DST_APP}/Contents/Resources/${doc}"
        cp "${ROOT_DIR}/${doc}" "${STAGING_DIR}/${doc}"
    fi
done

echo "Running macdeployqt on ${DST_APP}..."
"${MACDEPLOYQT}" "${DST_APP}" -always-overwrite

# Verify binary architecture and dynamic library linkage
BIN_PATH="${DST_APP}/Contents/MacOS/ChiikawaStudyPet"
echo "=== Architecture verification ==="
file "${BIN_PATH}"
lipo -info "${BIN_PATH}"

EXPECTED_ARCH="arm64"
if [[ "${ARCH_LABEL}" == "x64" || "${ARCH_LABEL}" == "x86_64" ]]; then
    EXPECTED_ARCH="x86_64"
fi
if ! lipo -info "${BIN_PATH}" | grep -q "${EXPECTED_ARCH}"; then
    echo "Error: Binary does not contain expected architecture ${EXPECTED_ARCH}" >&2
    exit 1
fi

echo "=== Linked libraries ==="
otool -L "${BIN_PATH}"
if otool -L "${BIN_PATH}" | grep -E "(/Users/|/opt/homebrew/|/usr/local/)" >/dev/null 2>&1; then
    echo "Warning: Detected non-relocated library path in binary." >&2
fi

# Code signing (Developer ID if configured via secrets, otherwise ad-hoc so Apple Silicon runs locally)
if [[ -n "${APPLE_SIGNING_IDENTITY:-}" ]]; then
    echo "Signing ${DST_APP} with Developer ID identity..."
    codesign --force --deep --options runtime --timestamp --sign "${APPLE_SIGNING_IDENTITY}" "${DST_APP}"
    codesign --verify --deep --strict --verbose=2 "${DST_APP}"
else
    echo "No APPLE_SIGNING_IDENTITY configured; applying ad-hoc signature (unsigned distribution)."
    codesign --force --deep --sign - "${DST_APP}"
    codesign --verify --deep --strict "${DST_APP}"
fi

# Add drag-to-Applications symlink
ln -s /Applications "${STAGING_DIR}/Applications"

# Create compressed DMG
echo "Creating ${DMG_PATH}..."
hdiutil create \
    -volname "Chiikawa Study Pet" \
    -srcfolder "${STAGING_DIR}" \
    -ov \
    -format UDZO \
    "${DMG_PATH}"

# Optional notarization & stapling when Apple credentials are configured
if [[ -n "${APPLE_SIGNING_IDENTITY:-}" && -n "${APPLE_ID:-}" && -n "${APPLE_TEAM_ID:-}" && -n "${APPLE_APP_PASSWORD:-}" ]]; then
    echo "Submitting ${DMG_PATH} for notarization..."
    xcrun notarytool submit "${DMG_PATH}" \
        --apple-id "${APPLE_ID}" \
        --team-id "${APPLE_TEAM_ID}" \
        --password "${APPLE_APP_PASSWORD}" \
        --wait
    xcrun stapler staple "${DMG_PATH}"
    spctl --assess --type open --context context:primary-signature --verbose "${DMG_PATH}" || true
fi

rm -rf "${STAGING_DIR}"
ls -lh "${DMG_PATH}"
