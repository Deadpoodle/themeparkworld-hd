#!/bin/sh
# Build, install into testrun (the automated-test copy, always hidden) and run it
# headless. Never touches testgame (the player's copy).
# Usage: autotest.sh [seconds] [screenshot-every] [script]   e.g. autotest.sh 40 0 "4:esc,8:esc,12:shot"
#        RES=1920x1080 MAXFPS=60 MODE=window autotest.sh   [Display] Resolution, MaxFPS, Mode (defaults auto, 60, borderless)
#        ARGS=/bwcursor autotest.sh    game command line
#        ONSCREEN=1 autotest.sh          on-screen test (full screen; only with the player's OK)
#        TESTDIR=testrun-x autotest.sh   another hidden test copy, so runs can go side by side
#        INI="Display.SharpUI=0" autotest.sh   any other ini values (Section.Key=value;...), this run only
set -e
cd "$(dirname "$0")/.."
SECS=${1:-30}
SHOT=${2:-0}
SCRIPT=${3:-}
T=${TESTDIR:-testrun}
PATH=/c/msys64/mingw32/bin:$PATH sh build.sh 2>&1 | grep -E "error|warning" || true
cp build/wsock32.dll "$T/"
rm -f "$T"/Scr*.tga "$T"/ThemeParkWorldHD.log
[ -f "$T/ThemeParkWorldHD.ini" ] || printf '[Debug]\r\n' > "$T/ThemeParkWorldHD.ini"
# INI= values are for this run only: the ini is put back afterwards.
[ -n "${INI:-}" ] && cp "$T/ThemeParkWorldHD.ini" "$T/ThemeParkWorldHD.ini.keep"
python -P - "$T/ThemeParkWorldHD.ini" "$SECS" "$SHOT" "$SCRIPT" <<'EOF'
import configparser, os, sys
p, secs, shot, script = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
c = configparser.ConfigParser(comment_prefixes=(";",), inline_comment_prefixes=None)
c.optionxform = str
c.read(p)
if "Debug" not in c: c["Debug"] = {}
c["Debug"]["Headless"] = os.environ.get("ONSCREEN") and "2" or "1"
c["Debug"]["ScreenshotEvery"] = str(shot)
c["Debug"]["QuitAfter"] = str(max(secs - 18, 5))
c["Debug"]["Script"] = script
if "Display" not in c: c["Display"] = {}
c["Display"]["Resolution"] = os.environ.get("RES", "auto")
c["Display"]["MaxFPS"] = os.environ.get("MAXFPS", "60")
c["Display"]["Mode"] = os.environ.get("MODE", "borderless")
c["Debug"]["Patch"] = os.environ.get("PATCH", "")
c["Debug"]["DDTrace"] = os.environ.get("DDTRACE", "0")
c["Debug"]["Profile"] = os.environ.get("PROFILE", "0")
c["Debug"]["Watch"] = os.environ.get("WATCH", "")
c["Debug"]["BlockActivateApp"] = os.environ.get("BLOCKACT", "0")
c["Debug"]["NoPresent"] = os.environ.get("NOPRESENT", "0")
for kv in filter(None, os.environ.get("INI", "").split(";")):  # INI="Display.SharpUI=0;Debug.DumpTextures=1"
    key, value = kv.split("=", 1)
    sec, name = key.split(".", 1)
    if sec not in c: c[sec] = {}
    c[sec][name] = value
with open(p, "w", newline="\r\n") as f: c.write(f)
EOF
python -P tools/watchvis.py "$SECS" "$T" > build/vis.txt &
powershell -NoProfile -File tools/smoke.ps1 -Seconds "$SECS" -Dir "$T" -GameArgs "${ARGS:-}" ${ONSCREEN:+-Explorer}
wait
[ -n "${INI:-}" ] && mv -f "$T/ThemeParkWorldHD.ini.keep" "$T/ThemeParkWorldHD.ini"
cat build/vis.txt
cat "$T/ThemeParkWorldHD.log"
[ -f "$T/enginedebug.txt" ] && { echo "--- enginedebug.txt"; tail -20 "$T/enginedebug.txt"; } || true
ls "$T"/Scr*.tga 2>/dev/null || true
