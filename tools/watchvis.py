"""Poll the test copy's TP.exe windows for N seconds and report whether any was
ever visible or foreground (headless runs must stay off the player's screen).
Only processes started from the given game folder are watched, never another copy.
Usage: watchvis.py <seconds> [dir]"""
import ctypes
import ctypes.wintypes as wt
import os
import sys
import time

u = ctypes.windll.user32
k = ctypes.windll.kernel32
P = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
end = time.time() + float(sys.argv[1])
root = os.path.normcase(os.path.abspath(sys.argv[2] if len(sys.argv) > 2 else "testrun"))


def exe_of(pid):
    h = k.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
    if not h:
        return ""
    buf = ctypes.create_unicode_buffer(1024)
    n = wt.DWORD(1024)
    ok = k.QueryFullProcessImageNameW(h, 0, buf, ctypes.byref(n))
    k.CloseHandle(h)
    return os.path.normcase(buf.value) if ok else ""


visible = foreground = windows = samples = 0
while time.time() < end:
    fg = u.GetForegroundWindow()

    def cb(h, _):
        global visible, foreground, windows
        p = wt.DWORD()
        u.GetWindowThreadProcessId(h, ctypes.byref(p))
        if exe_of(p.value) == os.path.join(root, "tp.exe"):
            windows += 1
            if u.IsWindowVisible(h):
                visible += 1
            if h == fg:
                foreground += 1
        return True

    u.EnumWindows(P(cb), 0)
    samples += 1
    time.sleep(0.25)
print(f"samples={samples} game_windows_seen={windows} visible_hits={visible} foreground_hits={foreground}")
