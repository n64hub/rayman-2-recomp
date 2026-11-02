#!/usr/bin/env bash
# Records src/rt64_shaders.txt, the shaders the page builds before the game and as each level starts, by code rather
# than by playing: N64Recomp-browser's tools/record_shaders.mjs serves the page built in <build dir> (tools/build_web.sh's
# build/web), drives it in headless Chrome through tools/shader_plan.json, and writes the list. Needs node 22 or later
# and Chrome. Then build the page again, so that it prebuilds the new list.
#
#   bash tools/record_shaders.sh <build dir> <ROM> [<first load>]
#
# Each section is recorded by a record_shaders.mjs of its own (--only, and --from to go on with the list), so in a
# Chrome and a profile of its own: a second load in one Chrome, after a load that reached the intro story, aborts as
# the next one's intro starts (Aborted(OOM), RT64's display-list stack growing without end), so record_shaders.mjs's
# own loop of loads in one page records only the first. About an hour for the 65 loads; a section that fails is said
# and the next goes on. <first load> goes on with a list stopped partway, from that load (the log numbers them).
set -uo pipefail
[ $# -ge 2 ] || { sed -n '7p' "$0"; exit 1; }
cd "$(dirname "$0")/.."
build="$1" rom="$2" first="${3:-1}"
mapfile -t names < <(node -e 'for (const s of require("./tools/shader_plan.json").sections) console.log(s.name)')
for i in "${!names[@]}"; do
    load=$((i + 1))
    [ "$load" -lt "$first" ] && continue
    from=()
    [ "$load" -gt 1 ] && from=(--from "$load")
    node lib/N64Recomp-browser/tools/record_shaders.mjs tools/shader_plan.json "$build" "$rom" src/rt64_shaders.txt \
        --only "${names[$i]}" "${from[@]}" || echo "record_shaders.sh: load $load (${names[$i]}) failed"
done
