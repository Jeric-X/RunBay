#!/bin/bash

set -euo pipefail

app_path=""
output_path=""
volume_name="RunBay"
background_path=""
volume_icon_path=""

usage() {
    echo "Usage: $0 --app PATH [--output PATH] [--volume-name NAME]"
    echo "          [--background PATH] [--volume-icon PATH]"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --app)
            [[ $# -ge 2 ]] || { echo "Missing value for $1" >&2; exit 2; }
            app_path="$2"
            shift 2
            ;;
        --output)
            [[ $# -ge 2 ]] || { echo "Missing value for $1" >&2; exit 2; }
            output_path="$2"
            shift 2
            ;;
        --volume-name)
            [[ $# -ge 2 ]] || { echo "Missing value for $1" >&2; exit 2; }
            volume_name="$2"
            shift 2
            ;;
        --background)
            [[ $# -ge 2 ]] || { echo "Missing value for $1" >&2; exit 2; }
            background_path="$2"
            shift 2
            ;;
        --volume-icon)
            [[ $# -ge 2 ]] || { echo "Missing value for $1" >&2; exit 2; }
            volume_icon_path="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "DMG creation must run on macOS." >&2
    exit 1
fi

[[ -n "$app_path" ]] || { echo "--app is required." >&2; usage >&2; exit 2; }
[[ -d "$app_path" ]] || { echo "App bundle was not found: $app_path" >&2; exit 1; }
[[ -f "$app_path/Contents/Info.plist" ]] || { echo "Invalid app bundle: $app_path" >&2; exit 1; }

script_dir="$(cd "$(dirname "$0")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
output_path="${output_path:-$repo_root/dist/RunBay-macos-Release.dmg}"
output_dir="$(/usr/bin/dirname "$output_path")"
background_path="${background_path:-$repo_root/packaging/macos/dmg-background.png}"
volume_icon_path="${volume_icon_path:-$repo_root/qt-client/resources/icons/runbay-app-icon.icns}"
app_name="$(/usr/bin/basename "$app_path")"

create_dmg_bin="$(command -v create-dmg || true)"
if [[ -z "$create_dmg_bin" ]]; then
    echo "create-dmg was not found. Install it with: brew install create-dmg" >&2
    exit 1
fi
[[ -f "$background_path" ]] || { echo "DMG background was not found: $background_path" >&2; exit 1; }
[[ -f "$volume_icon_path" ]] || { echo "DMG volume icon was not found: $volume_icon_path" >&2; exit 1; }

/bin/mkdir -p "$output_dir"
/bin/rm -f "$output_path"

"$create_dmg_bin" \
    --volname "$volume_name" \
    --volicon "$volume_icon_path" \
    --background "$background_path" \
    --window-size 660 400 \
    --icon-size 120 \
    --icon "$app_name" 165 175 \
    --hide-extension "$app_name" \
    --app-drop-link 495 175 \
    --filesystem HFS+ \
    --format UDZO \
    --no-internet-enable \
    "$output_path" \
    "$app_path"

/usr/bin/hdiutil verify "$output_path"
echo "Created RunBay DMG: $output_path"
