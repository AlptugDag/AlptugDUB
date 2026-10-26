#!/bin/sh
# AlptugDUB installer: copies the plug-ins into your user plug-in folders and clears the
# download quarantine (the release is signed ad hoc, not notarised).
#   sh install.sh           install for this user
#   sudo sh install.sh -a   install for every user (/Library)
set -e
here="$(cd "$(dirname "$0")" && pwd)"
base="$HOME/Library/Audio/Plug-Ins"
[ "$1" = "-a" ] && base="/Library/Audio/Plug-Ins"
mkdir -p "$base/VST3" "$base/Components"
for p in AlptugDUB.vst3:VST3 AlptugDUB.component:Components; do
    name="${p%%:*}"; dir="${p##*:}"
    [ -d "$here/$name" ] || continue
    rm -rf "$base/$dir/$name"
    cp -R "$here/$name" "$base/$dir/"
    xattr -dr com.apple.quarantine "$base/$dir/$name" 2>/dev/null || true
    echo "installed $base/$dir/$name"
done
# make Logic and other AU hosts notice the new component
killall -9 AudioComponentRegistrar 2>/dev/null || true
echo "Done. Rescan plug-ins in your DAW."
