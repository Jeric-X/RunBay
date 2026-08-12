#!/bin/bash

set -uo pipefail

readonly LABEL_PREFIX="com.runbay.daemon"

usage() {
    echo "Usage:" >&2
    echo "  $0 install DAEMON CONFIG RESULT_FILE" >&2
    echo "  $0 start|stop|uninstall SERVICE_ID RESULT_FILE" >&2
    exit 2
}

require_root() {
    if [[ "${EUID}" -ne 0 ]]; then
        echo "administrator privileges are required" >&2
        return 1
    fi
}

validate_service_id() {
    [[ "$1" =~ ^[a-z0-9][a-z0-9-]{0,63}$ ]] || {
        echo "invalid service id: $1" >&2
        return 1
    }
}

label_for_id() {
    printf '%s.%s' "$LABEL_PREFIX" "$1"
}

plist_for_id() {
    printf '/Library/LaunchDaemons/%s.plist' "$(label_for_id "$1")"
}

config_value() {
    /usr/bin/plutil -extract "$1" raw "$2"
}

validate_data_directory() {
    case "$1" in
        ""|/|/Library|/System|/Users|/Applications|/private|/var)
            echo "unsafe service data directory: $1" >&2
            return 1
            ;;
    esac
}

install_service() {
    local daemon_source="$1"
    local config_source="$2"
    [[ -x "$daemon_source" ]] || { echo "runbayd is not executable: $daemon_source" >&2; return 1; }
    [[ -f "$config_source" ]] || { echo "service config was not found: $config_source" >&2; return 1; }
    # plutil's lint mode only accepts plist syntax. Conversion mode also parses
    # JSON, so use it for validation without creating another persisted format.
    /usr/bin/plutil -convert xml1 -o /dev/null "$config_source" || return 1

    local version service_id user data_file log_directory data_directory config_destination label plist group plist_temp
    version="$(config_value version "$config_source")" || return 1
    [[ "$version" == "1" ]] || { echo "unsupported service config version: $version" >&2; return 1; }
    service_id="$(config_value service_id "$config_source")" || return 1
    user="$(config_value user "$config_source")" || return 1
    data_file="$(config_value data_file "$config_source")" || return 1
    log_directory="$(config_value log_directory "$config_source")" || return 1
    validate_service_id "$service_id" || return 1
    /usr/bin/id "$user" >/dev/null 2>&1 || { echo "local user does not exist: $user" >&2; return 1; }

    data_directory="$(/usr/bin/dirname "$data_file")"
    validate_data_directory "$data_directory" || return 1
    validate_data_directory "$log_directory" || return 1
    config_destination="$data_directory/service.json"
    label="$(label_for_id "$service_id")"
    plist="$(plist_for_id "$service_id")"
    group="$(/usr/bin/id -gn "$user")" || return 1

    if [[ -e "$plist" ]] || /bin/launchctl print "system/$label" >/dev/null 2>&1; then
        echo "service is already registered: $label" >&2
        return 1
    fi

    /usr/bin/install -d -o root -g wheel -m 755 /Library/LaunchDaemons || return 1
    /usr/bin/install -d -o "$user" -g "$group" -m 750 "$data_directory" "$log_directory" || return 1
    /usr/bin/install -o "$user" -g "$group" -m 640 "$config_source" "$config_destination" || return 1
    if [[ -f "$data_file" ]]; then
        /usr/sbin/chown "$user:$group" "$data_file" || return 1
    fi

    plist_temp="$(/usr/bin/mktemp /private/tmp/runbay-launchd.XXXXXX)" || return 1
    /usr/bin/plutil -create xml1 "$plist_temp" || return 1
    /usr/bin/plutil -insert Label -string "$label" "$plist_temp" || return 1
    /usr/bin/plutil -insert ProgramArguments -array "$plist_temp" || return 1
    /usr/bin/plutil -insert ProgramArguments.0 -string "$daemon_source" "$plist_temp" || return 1
    /usr/bin/plutil -insert ProgramArguments.1 -string "-service-config" "$plist_temp" || return 1
    /usr/bin/plutil -insert ProgramArguments.2 -string "$config_destination" "$plist_temp" || return 1
    /usr/bin/plutil -insert EnvironmentVariables -dictionary "$plist_temp" || return 1
    /usr/bin/plutil -insert EnvironmentVariables.PATH -string "/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin" "$plist_temp" || return 1
    /usr/bin/plutil -insert KeepAlive -bool true "$plist_temp" || return 1
    /usr/bin/plutil -insert RunAtLoad -bool true "$plist_temp" || return 1
    /usr/bin/plutil -insert ProcessType -string Background "$plist_temp" || return 1
    /usr/bin/plutil -insert UserName -string "$user" "$plist_temp" || return 1
    /usr/bin/plutil -insert WorkingDirectory -string "$data_directory" "$plist_temp" || return 1
    /usr/bin/plutil -lint "$plist_temp" >/dev/null || return 1
    /usr/bin/install -o root -g wheel -m 644 "$plist_temp" "$plist" || return 1
    /bin/rm -f "$plist_temp"

    if ! /bin/launchctl bootstrap system "$plist"; then
        /bin/rm -f "$plist"
        return 1
    fi
    /bin/launchctl enable "system/$label" || return 1
    /bin/launchctl kickstart -k "system/$label" || return 1
}

start_service() {
    local service_id="$1" label plist
    validate_service_id "$service_id" || return 1
    label="$(label_for_id "$service_id")"
    plist="$(plist_for_id "$service_id")"
    [[ -f "$plist" ]] || { echo "service is not registered: $label" >&2; return 1; }
    if ! /bin/launchctl print "system/$label" >/dev/null 2>&1; then
        /bin/launchctl bootstrap system "$plist" || return 1
    fi
    /bin/launchctl enable "system/$label" || return 1
    /bin/launchctl kickstart -k "system/$label" || return 1
}

stop_service() {
    local service_id="$1" label
    validate_service_id "$service_id" || return 1
    label="$(label_for_id "$service_id")"
    if /bin/launchctl print "system/$label" >/dev/null 2>&1; then
        /bin/launchctl bootout "system/$label" || return 1
    fi
}

uninstall_service() {
    local service_id="$1" label plist
    validate_service_id "$service_id" || return 1
    label="$(label_for_id "$service_id")"
    plist="$(plist_for_id "$service_id")"
    if /bin/launchctl print "system/$label" >/dev/null 2>&1; then
        /bin/launchctl bootout "system/$label" || return 1
    fi
    /bin/rm -f "$plist"
}

[[ "$(uname -s)" == "Darwin" ]] || { echo "this manager only supports macOS" >&2; exit 1; }
require_root || exit 1
[[ $# -ge 3 ]] || usage

operation="$1"
shift
result_file="${!#}"
umask 022

case "$operation" in
    install)
        [[ $# -eq 3 ]] || usage
        if output="$(install_service "$1" "$2" 2>&1)"; then
            printf 'SUCCESS\n' >"$result_file"
        else
            status=$?
            printf 'ERROR: %s\n' "${output:-installation failed}" >"$result_file"
            exit "$status"
        fi
        ;;
    start)
        [[ $# -eq 2 ]] || usage
        if output="$(start_service "$1" 2>&1)"; then
            printf 'SUCCESS\n' >"$result_file"
        else
            status=$?
            printf 'ERROR: %s\n' "${output:-start failed}" >"$result_file"
            exit "$status"
        fi
        ;;
    stop)
        [[ $# -eq 2 ]] || usage
        if output="$(stop_service "$1" 2>&1)"; then
            printf 'SUCCESS\n' >"$result_file"
        else
            status=$?
            printf 'ERROR: %s\n' "${output:-stop failed}" >"$result_file"
            exit "$status"
        fi
        ;;
    uninstall)
        [[ $# -eq 2 ]] || usage
        if output="$(uninstall_service "$1" 2>&1)"; then
            printf 'SUCCESS\n' >"$result_file"
        else
            status=$?
            printf 'ERROR: %s\n' "${output:-uninstall failed}" >"$result_file"
            exit "$status"
        fi
        ;;
    *) usage ;;
esac
