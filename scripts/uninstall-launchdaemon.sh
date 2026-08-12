#!/bin/bash

set -euo pipefail

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "This uninstaller only supports macOS." >&2
    exit 1
fi

if [[ "${EUID}" -ne 0 ]]; then
    echo "Administrator privileges are required." >&2
    echo "Run: sudo \"$0\"" >&2
    exit 1
fi

script_dir="$(cd "$(dirname "$0")" && pwd)"
manager="$script_dir/manage-launchdaemon.sh"
[[ -x "$manager" ]] || manager="$script_dir/../Resources/manage-launchdaemon.sh"
[[ -x "$manager" ]] || { echo "manage-launchdaemon.sh was not found." >&2; exit 1; }
result_temp="$(mktemp /private/tmp/runbay-result.XXXXXX)"
trap 'rm -f "$result_temp"' EXIT
"$manager" uninstall runbay-85c274be25bc "$result_temp"
cat "$result_temp"
echo "Task data and logs were retained under /Library/Application Support/RunBayd and /Library/Logs/RunBayd."
