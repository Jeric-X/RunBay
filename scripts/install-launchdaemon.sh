#!/bin/bash

set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
daemon_source="${1:-}"

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "This installer only supports macOS." >&2
    exit 1
fi

if [[ -z "$daemon_source" ]]; then
    for candidate in \
        "$script_dir/RunBay.app/Contents/MacOS/runbayd" \
        "$script_dir/../MacOS/runbayd" \
        "$script_dir/runbayd"; do
        if [[ -x "$candidate" ]]; then
            daemon_source="$candidate"
            break
        fi
    done
fi

if [[ -z "$daemon_source" || ! -x "$daemon_source" ]]; then
    echo "runbayd was not found. Pass its path as the first argument." >&2
    exit 1
fi
daemon_source="$(cd "$(dirname "$daemon_source")" && pwd)/$(basename "$daemon_source")"

manager="$script_dir/manage-launchdaemon.sh"
[[ -x "$manager" ]] || manager="$script_dir/../Resources/manage-launchdaemon.sh"
[[ -x "$manager" ]] || { echo "manage-launchdaemon.sh was not found." >&2; exit 1; }

config_temp="$(mktemp /private/tmp/runbay-service.XXXXXX)"
result_temp="$(mktemp /private/tmp/runbay-result.XXXXXX)"
trap 'rm -f "$config_temp" "$result_temp"' EXIT
/usr/bin/printf '%s\n' \
    '{' \
    '  "version": 1,' \
    '  "service_id": "runbay-85c274be25bc",' \
    '  "name": "RunBay",' \
    '  "listen_address": "127.0.0.1:8732",' \
    '  "data_file": "/Library/Application Support/RunBayd/tasks.json",' \
    '  "log_directory": "/Library/Logs/RunBayd",' \
    '  "user": "root"' \
    '}' >"$config_temp"

"$manager" install "$daemon_source" "$config_temp" "$result_temp"
cat "$result_temp"
