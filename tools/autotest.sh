#!/bin/sh
# Build, install into testrun (the automated-test copy, always hidden) and run it
# headless. Never touches testgame (the player's copy).
# Usage: autotest.sh [seconds] [screenshot-every] [script]   e.g. autotest.sh 40 0 "4:esc,8:esc,12:shot"
#        RES=1920x1080 MAXFPS=60 autotest.sh   [Display] Resolution and MaxFPS (defaults auto, 60)
#        ARGS=/bwcursor autotest.sh    game command line
#        ONSCREEN=1 autotest.sh          on-screen test (full screen; only with the player's OK)
#        TESTDIR=testrun-x autotest.sh   another hidden test copy, so runs can go side by side
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
c["Debug"]["Patch"] = os.environ.get("PATCH", "")
c["Debug"]["DDTrace"] = os.environ.get("DDTRACE", "0")
with open(p, "w", newline="\r\n") as f: c.write(f)
EOF
python -P tools/watchvis.py "$SECS" "$T" > build/vis.txt &
powershell -NoProfile -File tools/smoke.ps1 -Seconds "$SECS" -Dir "$T" -GameArgs "${ARGS:-}"
wait
cat build/vis.txt
cat "$T/ThemeParkWorldHD.log"
[ -f "$T/enginedebug.txt" ] && { echo "--- enginedebug.txt"; tail -20 "$T/enginedebug.txt"; } || true
ls "$T"/Scr*.tga 2>/dev/null || true
