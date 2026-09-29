#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

current="$(tr -d '\r\n' < VERSION)"
if [[ ! "$current" =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)$ ]]; then
    echo "Unsupported current version: $current" >&2
    exit 1
fi

major="${BASH_REMATCH[1]}"
minor="${BASH_REMATCH[2]}"
patch="${BASH_REMATCH[3]}"
requested="${1:-patch}"

case "$requested" in
    patch) next_version="$major.$minor.$((10#$patch + 1))" ;;
    minor) next_version="$major.$((10#$minor + 1)).0" ;;
    major) next_version="$((10#$major + 1)).0.0" ;;
    *)
        if [[ ! "$requested" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
            echo "Usage: just up-version [patch|minor|major|X.Y.Z]" >&2
            exit 1
        fi
        next_version="$requested"
        ;;
esac

if [[ "$next_version" == "$current" ]]; then
    echo "Version is already $current" >&2
    exit 1
fi

version_tmp="$(mktemp "${TMPDIR:-/tmp}/steamvr-gesture-exit-version.XXXXXX")"
package_tmp="$(mktemp "${TMPDIR:-/tmp}/steamvr-gesture-exit-package.XXXXXX")"
version_backup="$(mktemp "${TMPDIR:-/tmp}/steamvr-gesture-exit-version-backup.XXXXXX")"
package_backup="$(mktemp "${TMPDIR:-/tmp}/steamvr-gesture-exit-package-backup.XXXXXX")"
trap 'rm -f "$version_tmp" "$package_tmp" "$version_backup" "$package_backup"' EXIT

printf '%s\n' "$next_version" > "$version_tmp"
awk -v next_version="$next_version" '
    !changed && /^  "version": "[^"]+",$/ {
        print "  \"version\": \"" next_version "\",";
        changed = 1;
        next;
    }
    { print }
    END { if (!changed) exit 1 }
' package.json > "$package_tmp"

cp VERSION "$version_backup"
cp package.json "$package_backup"
chmod 0644 "$version_tmp" "$package_tmp"
if ! mv "$version_tmp" VERSION || ! mv "$package_tmp" package.json; then
    cp "$version_backup" VERSION
    cp "$package_backup" package.json
    echo "Version update failed; original files were restored." >&2
    exit 1
fi

echo "Version bumped: $current -> $next_version"
