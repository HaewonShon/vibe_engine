"""
verify_render.py — Visual verification for VibeEngine Sandbox

Launches Sandbox.exe, waits for the window to appear, moves it to the
specified monitor, captures a screenshot, then asks Claude Vision API
whether the 3D scene is rendering correctly.

Requirements:
    pip install anthropic pillow pywin32

Usage:
    python tools/verify_render.py [options]

    --exe       Path to Sandbox.exe (default: bin/Debug-x64/Sandbox.exe)
    --api-key   Anthropic API key (default: reads ANTHROPIC_API_KEY env var)
    --no-launch Skip launching the exe (use if already running)
    --timeout   Seconds to wait for window (default: 15)
    --monitor   Which monitor to move the window to before capturing.
                  right   — rightmost monitor by X position (default)
                  left    — leftmost monitor (primary in most setups)
                  0,1,2…  — zero-based index sorted left→right
    --save-screenshot PATH  Save the captured screenshot to this path
"""

import argparse
import base64
import io
import os
import subprocess
import sys
import time

try:
    import anthropic
except ImportError:
    sys.exit("anthropic package not found. Run: pip install anthropic")

try:
    from PIL import ImageGrab
except ImportError:
    sys.exit("Pillow not found. Run: pip install pillow")

try:
    import win32gui
    import win32con
    import win32api
except ImportError:
    sys.exit("pywin32 not found. Run: pip install pywin32")


WINDOW_TITLE_KEYWORDS = ["VibeEngine", "Sandbox"]
PROMPT = (
    "이 스크린샷은 DirectX 12 게임 엔진의 렌더링 결과입니다. "
    "다음 사항을 확인하고 간결하게 답해 주세요:\n"
    "1. 화면에 3D 오브젝트(큐브, 바닥, 모델 등)가 보이는가? (예/아니오)\n"
    "2. 조명·그림자가 적용되어 있는가? (예/아니오)\n"
    "3. 배경이 짙은 단색(거의 검정에 가까운)인가? (예/아니오)\n"
    "4. ImGui 패널(좌측 Hierarchy, 우측 Inspector 등)이 보이는가? (예/아니오)\n"
    "5. 렌더링 문제(아티팩트, 깨짐, 빈 화면 등)가 있는가? (있다면 설명)\n"
    "최종 판정: 렌더링이 정상적으로 동작하고 있는가?"
)

# ---------------------------------------------------------------------------
# Monitor helpers
# ---------------------------------------------------------------------------

def get_monitors() -> list[tuple[int, int, int, int]]:
    """Return monitor rects (left, top, right, bottom) sorted left→right by X."""
    rects: list[tuple[int, int, int, int]] = []
    for hmon, _hdc, _rect in win32api.EnumDisplayMonitors():
        info = win32api.GetMonitorInfo(hmon)
        m = info["Monitor"]            # (left, top, right, bottom)
        rects.append((m[0], m[1], m[2], m[3]))
    return sorted(rects, key=lambda r: r[0])   # ascending by left edge


def resolve_monitor(spec: str) -> tuple[int, int, int, int]:
    """
    Convert --monitor spec to a (left, top, right, bottom) rect.
      'right'  → rightmost monitor
      'left'   → leftmost monitor (usually primary)
      '0','1'… → index in left-to-right order
    Raises SystemExit if spec is invalid or index out of range.
    """
    monitors = get_monitors()
    if not monitors:
        sys.exit("No monitors detected.")

    if spec == "right":
        return monitors[-1]
    if spec == "left":
        return monitors[0]

    try:
        idx = int(spec)
    except ValueError:
        sys.exit(f"Unknown --monitor value '{spec}'. Use 'right', 'left', or an integer index.")

    if idx < 0 or idx >= len(monitors):
        sys.exit(f"Monitor index {idx} out of range (0–{len(monitors) - 1}).")
    return monitors[idx]


def move_window_to_monitor(hwnd: int,
                            monitor_rect: tuple[int, int, int, int],
                            win_w: int = 1280,
                            win_h: int = 720) -> None:
    """Restore + move the window to be centred on the given monitor."""
    ml, mt, mr, mb = monitor_rect
    mw, mh = mr - ml, mb - mt
    wx = ml + max(0, (mw - win_w) // 2)
    wy = mt + max(0, (mh - win_h) // 2)
    win32gui.ShowWindow(hwnd, win32con.SW_RESTORE)
    win32gui.MoveWindow(hwnd, wx, wy, win_w, win_h, True)
    win32gui.SetForegroundWindow(hwnd)
    time.sleep(0.5)   # allow the window to repaint at new position


# ---------------------------------------------------------------------------
# Window discovery
# ---------------------------------------------------------------------------

def find_window(keywords: list[str], timeout: float) -> int | None:
    """Poll until a window whose title contains one of the keywords appears."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        found: list[int] = []

        def _cb(hwnd, _):
            if win32gui.IsWindowVisible(hwnd):
                title = win32gui.GetWindowText(hwnd)
                if any(k.lower() in title.lower() for k in keywords):
                    found.append(hwnd)
            return True

        win32gui.EnumWindows(_cb, None)
        if found:
            return found[0]
        time.sleep(0.5)
    return None


# ---------------------------------------------------------------------------
# Screenshot capture
# ---------------------------------------------------------------------------

def capture_window(hwnd: int) -> bytes:
    """Capture the bounding rect of a window and return PNG bytes."""
    win32gui.SetForegroundWindow(hwnd)
    time.sleep(0.3)

    left, top, right, bottom = win32gui.GetWindowRect(hwnd)
    img = ImageGrab.grab(bbox=(left, top, right, bottom), all_screens=True)

    buf = io.BytesIO()
    img.save(buf, format="PNG")
    return buf.getvalue()


# ---------------------------------------------------------------------------
# Claude Vision
# ---------------------------------------------------------------------------

def ask_claude(api_key: str, png_bytes: bytes) -> str:
    client = anthropic.Anthropic(api_key=api_key)
    b64 = base64.standard_b64encode(png_bytes).decode("utf-8")

    message = client.messages.create(
        model="claude-sonnet-4-6",
        max_tokens=512,
        messages=[
            {
                "role": "user",
                "content": [
                    {
                        "type": "image",
                        "source": {
                            "type": "base64",
                            "media_type": "image/png",
                            "data": b64,
                        },
                    },
                    {"type": "text", "text": PROMPT},
                ],
            }
        ],
    )
    return message.content[0].text


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> None:
    script_dir = os.path.dirname(os.path.abspath(__file__))
    repo_root  = os.path.dirname(script_dir)
    default_exe = os.path.join(repo_root, "bin", "Debug-x64", "Sandbox.exe")

    parser = argparse.ArgumentParser(description="Visual render verification for VibeEngine")
    parser.add_argument("--exe",     default=default_exe,
                        help="Path to Sandbox.exe")
    parser.add_argument("--api-key", default=os.environ.get("ANTHROPIC_API_KEY"),
                        help="Anthropic API key (or set ANTHROPIC_API_KEY env var)")
    parser.add_argument("--no-launch", action="store_true",
                        help="Skip launching the exe (use if already running)")
    parser.add_argument("--timeout", type=float, default=15.0,
                        help="Seconds to wait for the window (default: 15)")
    parser.add_argument("--monitor", default="right",
                        help="Monitor to use: 'right' (default), 'left', or index 0/1/…")
    parser.add_argument("--save-screenshot", metavar="PATH",
                        help="Save the captured screenshot to this path")
    args = parser.parse_args()

    if not args.api_key:
        sys.exit("No API key. Set ANTHROPIC_API_KEY or pass --api-key.")

    # Resolve target monitor early so we fail fast on bad input
    monitor_rect = resolve_monitor(args.monitor)
    ml, mt, mr, mb = monitor_rect
    print(f"[0/4] Target monitor: {args.monitor!r}  →  "
          f"({ml},{mt})–({mr},{mb})  {mr-ml}×{mb-mt}px")

    proc = None
    if not args.no_launch:
        if not os.path.isfile(args.exe):
            sys.exit(f"Exe not found: {args.exe}\nBuild the project first (Debug|x64).")
        print(f"[1/4] Launching {args.exe} ...")
        proc = subprocess.Popen([args.exe], cwd=os.path.dirname(args.exe))
    else:
        print("[1/4] Skipping launch (--no-launch).")

    print(f"[2/4] Waiting up to {args.timeout:.0f}s for window ...")
    hwnd = find_window(WINDOW_TITLE_KEYWORDS, args.timeout)
    if hwnd is None:
        if proc:
            proc.terminate()
        sys.exit("Window not found within timeout. Is the exe running?")
    print(f"      Found window (hwnd={hwnd:#010x}).")

    print(f"      Moving window to {args.monitor!r} monitor ...")
    move_window_to_monitor(hwnd, monitor_rect)

    print("[3/4] Capturing screenshot ...")
    png_bytes = capture_window(hwnd)
    print(f"      Captured {len(png_bytes):,} bytes.")

    if args.save_screenshot:
        with open(args.save_screenshot, "wb") as f:
            f.write(png_bytes)
        print(f"      Saved to {args.save_screenshot}")

    print("[4/4] Asking Claude Vision API ...")
    result = ask_claude(args.api_key, png_bytes)

    print()
    print("=" * 60)
    print("CLAUDE VERDICT")
    print("=" * 60)
    print(result)
    print("=" * 60)

    if proc:
        print("\nClosing Sandbox.exe ...")
        proc.terminate()


if __name__ == "__main__":
    main()
