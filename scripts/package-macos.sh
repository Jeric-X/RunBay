#!/bin/bash

set -euo pipefail

configuration="Release"
qt_prefix=""
output_dir=""
build_dir=""
architectures=""

usage() {
    echo "Usage: $0 [-c Debug|Release|RelWithDebInfo|MinSizeRel] [--qt-prefix PATH]"
    echo "          [--output-dir PATH] [--build-dir PATH] [--architectures ARCHS]"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -c|--configuration)
            if [[ $# -lt 2 ]]; then
                echo "Missing value for $1" >&2
                exit 2
            fi
            configuration="$2"
            shift 2
            ;;
        --qt-prefix)
            if [[ $# -lt 2 ]]; then
                echo "Missing value for $1" >&2
                exit 2
            fi
            qt_prefix="$2"
            shift 2
            ;;
        --output-dir)
            if [[ $# -lt 2 ]]; then
                echo "Missing value for $1" >&2
                exit 2
            fi
            output_dir="$2"
            shift 2
            ;;
        --build-dir)
            if [[ $# -lt 2 ]]; then
                echo "Missing value for $1" >&2
                exit 2
            fi
            build_dir="$2"
            shift 2
            ;;
        --architectures)
            if [[ $# -lt 2 ]]; then
                echo "Missing value for $1" >&2
                exit 2
            fi
            architectures="$2"
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

case "$configuration" in
    Debug|Release|RelWithDebInfo|MinSizeRel) ;;
    *)
        echo "Unsupported configuration: $configuration" >&2
        exit 2
        ;;
esac

case "$architectures" in
    ""|arm64|x86_64|"arm64;x86_64"|"x86_64;arm64") ;;
    *)
        echo "Unsupported architecture list: $architectures" >&2
        exit 2
        ;;
esac

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "macOS packaging must run on macOS." >&2
    exit 1
fi

script_dir="$(cd "$(dirname "$0")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
output_dir="${output_dir:-$repo_root/dist/$configuration}"
build_dir="${build_dir:-$repo_root/qt-client/build/$configuration}"

if ! command -v go >/dev/null 2>&1; then
    echo "Go was not found in PATH." >&2
    exit 1
fi

cmake_bin="$(command -v cmake || true)"
if [[ -z "$cmake_bin" && -x "/Applications/CMake.app/Contents/bin/cmake" ]]; then
    cmake_bin="/Applications/CMake.app/Contents/bin/cmake"
fi
if [[ -z "$cmake_bin" ]]; then
    echo "CMake was not found in PATH or /Applications/CMake.app." >&2
    exit 1
fi

if [[ -z "$qt_prefix" ]]; then
    for candidate in \
        "/opt/homebrew/opt/qtbase" \
        "/opt/homebrew/opt/qt" \
        "/usr/local/opt/qtbase" \
        "/usr/local/opt/qt"; do
        if [[ -f "$candidate/lib/cmake/Qt6/Qt6Config.cmake" ]]; then
            qt_prefix="$candidate"
            break
        fi
    done
fi

macdeployqt_bin=""
if [[ -n "$qt_prefix" && -x "$qt_prefix/bin/macdeployqt" ]]; then
    macdeployqt_bin="$qt_prefix/bin/macdeployqt"
else
    macdeployqt_bin="$(command -v macdeployqt || true)"
fi
if [[ -z "$macdeployqt_bin" ]]; then
    echo "macdeployqt was not found. Add Qt bin to PATH or pass --qt-prefix." >&2
    exit 1
fi

daemon_output="$repo_root/daemon/bin/$configuration/runbayd"
/bin/mkdir -p "$(dirname "$daemon_output")"

build_daemon_for_arch() {
    local arch="$1"
    local destination="$2"
    (
        cd "$repo_root/daemon"
        CGO_ENABLED=0 GOOS=darwin GOARCH="$arch" go build -trimpath -o "$destination" ./cmd/runbayd
    )
}

if [[ "$architectures" == "arm64;x86_64" || "$architectures" == "x86_64;arm64" ]]; then
    daemon_arm64="$daemon_output.arm64"
    daemon_amd64="$daemon_output.x86_64"
    build_daemon_for_arch arm64 "$daemon_arm64"
    build_daemon_for_arch amd64 "$daemon_amd64"
    /usr/bin/lipo -create -output "$daemon_output" "$daemon_arm64" "$daemon_amd64"
    /bin/rm -f "$daemon_arm64" "$daemon_amd64"
else
    native_arch="${architectures:-$(uname -m)}"
    if [[ "$native_arch" == "x86_64" ]]; then
        go_arch="amd64"
    else
        go_arch="arm64"
    fi
    build_daemon_for_arch "$go_arch" "$daemon_output"
fi

configure_args=(
    -S "$repo_root/qt-client"
    -B "$build_dir"
    "-DCMAKE_BUILD_TYPE=$configuration"
)
if [[ -n "$qt_prefix" ]]; then
    configure_args+=("-DCMAKE_PREFIX_PATH=$qt_prefix")
fi
if [[ -n "$architectures" ]]; then
    configure_args+=("-DCMAKE_OSX_ARCHITECTURES=$architectures")
fi

"$cmake_bin" "${configure_args[@]}"
"$cmake_bin" --build "$build_dir" --config "$configuration"

app_source="$build_dir/bin/RunBay.app"
if [[ ! -d "$app_source" ]]; then
    echo "App bundle was not produced at $app_source" >&2
    exit 1
fi

/bin/mkdir -p "$output_dir"
/bin/rm -rf "$output_dir/RunBay.app"
/bin/rm -f "$output_dir/install-launchdaemon.sh" "$output_dir/uninstall-launchdaemon.sh" \
    "$output_dir/manage-launchdaemon.sh"
/usr/bin/ditto "$app_source" "$output_dir/RunBay.app"
/usr/bin/install -m 755 "$daemon_output" "$output_dir/RunBay.app/Contents/MacOS/runbayd"
/usr/bin/install -m 755 "$script_dir/install-launchdaemon.sh" "$output_dir/install-launchdaemon.sh"
/usr/bin/install -m 755 "$script_dir/uninstall-launchdaemon.sh" "$output_dir/uninstall-launchdaemon.sh"
/usr/bin/install -m 755 "$script_dir/manage-launchdaemon.sh" "$output_dir/manage-launchdaemon.sh"
/usr/bin/install -m 755 "$script_dir/install-launchdaemon.sh" \
    "$output_dir/RunBay.app/Contents/Resources/install-launchdaemon.sh"
/usr/bin/install -m 755 "$script_dir/uninstall-launchdaemon.sh" \
    "$output_dir/RunBay.app/Contents/Resources/uninstall-launchdaemon.sh"
/usr/bin/install -m 755 "$script_dir/manage-launchdaemon.sh" \
    "$output_dir/RunBay.app/Contents/Resources/manage-launchdaemon.sh"

"$macdeployqt_bin" "$output_dir/RunBay.app" -always-overwrite -no-codesign

# Ad-hoc signing makes the nested Qt frameworks and helper binary internally consistent.
/usr/bin/codesign --force --deep --sign - "$output_dir/RunBay.app"
/usr/bin/codesign --verify --deep --strict "$output_dir/RunBay.app"

echo "Packaged RunBay for macOS: $output_dir/RunBay.app"
echo "Install the boot service with: sudo \"$output_dir/install-launchdaemon.sh\""
