#!/bin/bash

set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
icon_dir="$repo_root/qt-client/resources/icons"
source_png="$icon_dir/runbay-app-icon.png"
macos_png="$icon_dir/runbay-app-icon-macos.png"
output_icns="$icon_dir/runbay-app-icon.icns"

makeicns_bin="$(command -v makeicns || true)"
if [[ -z "$makeicns_bin" ]]; then
    echo "makeicns was not found. Install it with: brew install makeicns" >&2
    exit 1
fi

/usr/bin/swift "$script_dir/flatten-png-background.swift" "$source_png" "$macos_png"
"$makeicns_bin" -in "$macos_png" -out "$output_icns"
echo "Generated white-background macOS icon: $output_icns"
