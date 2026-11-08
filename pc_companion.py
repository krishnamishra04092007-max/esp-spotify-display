"""
Spotify PC Companion for ESP32 2.4" TFT Display
----------------------------------------------
- Real-time window scanner for Spotify Web Player (Chrome, Edge, Firefox, Brave) and Spotify Desktop
- Fetches high-res 300x300 album posters using iTunes & Deezer public catalogs (no API keys required)
- Pushes track name, artist, album, and poster URL to ESP32 over Wi-Fi
"""

import ctypes
import ctypes.wintypes
import json
import os
import re
import sys
import time
import urllib.parse
import urllib.request
from concurrent.futures import ThreadPoolExecutor

LAST_IP_FILE = os.path.join(os.path.dirname(__file__), ".esp_ip")

def load_env_file():
    env_path = os.path.join(os.path.dirname(__file__), ".env")
    if os.path.exists(env_path):
        try:
            with open(env_path, "r", encoding="utf-8") as f:
                for line in f:
                    line = line.strip()
                    if line and not line.startswith("#") and "=" in line:
                        k, v = line.split("=", 1)
                        os.environ.setdefault(k.strip(), v.strip().strip("\"'"))
        except Exception:
            pass

load_env_file()

def get_saved_ip():
    if "ESP32_IP" in os.environ and os.environ["ESP32_IP"]:
        return os.environ["ESP32_IP"]
    if os.path.exists(LAST_IP_FILE):
        try:
            with open(LAST_IP_FILE, "r") as f:
                ip = f.read().strip()
                if ip: return ip
        except Exception:
            pass
    return "192.168.10.37"

def save_ip(ip):
    try:
        with open(LAST_IP_FILE, "w") as f:
            f.write(ip.strip())
    except Exception:
        pass

# ---------------------------------------------------------------------------
# High-Res Poster Search (iTunes & Deezer APIs - 100% Free, No Keys)
# ---------------------------------------------------------------------------
def fetch_poster_and_details(artist, track):
    clean_track = re.sub(r'\(.*?\)|\[.*?\]', '', track)
    if " - " in clean_track:
        clean_track = clean_track.split(" - ")[0]
    clean_track = clean_track.strip()

    search_query = f"{artist} {clean_track}".strip() if artist else clean_track

    # 1. Try iTunes Search API
    try:
        query = urllib.parse.quote(search_query)
        url = f"https://itunes.apple.com/search?term={query}&entity=song&limit=1"
        req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
        with urllib.request.urlopen(req, timeout=4) as resp:
            data = json.loads(resp.read().decode())
            if data.get("resultCount", 0) > 0:
                item = data["results"][0]
                art = item.get("artworkUrl100", "").replace("100x100bb", "300x300bb")
                return {
                    "title": item.get("trackName", track),
                    "artist": item.get("artistName", artist or "Unknown Artist"),
                    "album": item.get("collectionName", ""),
                    "image": art,
                    "duration_ms": item.get("trackTimeMillis", 0)
                }
    except Exception:
        pass

    # 2. Fallback to Deezer Search API
    try:
        query = urllib.parse.quote(f'{clean_track} {artist}'.strip())
        url = f"https://api.deezer.com/search?q={query}&limit=1"
        req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
        with urllib.request.urlopen(req, timeout=4) as resp:
            data = json.loads(resp.read().decode())
            items = data.get("data", [])
            if items:
                item = items[0]
                return {
                    "title": item.get("title", track),
                    "artist": item.get("artist", {}).get("name", artist or "Unknown Artist"),
                    "album": item.get("album", {}).get("title", ""),
                    "image": item.get("album", {}).get("cover_medium", ""),
                    "duration_ms": item.get("duration", 0) * 1000
                }
    except Exception:
        pass

    return {
        "title": track,
        "artist": artist or "Unknown Artist",
        "album": "",
        "image": "",
        "duration_ms": 0
    }

# ---------------------------------------------------------------------------
# Communication with ESP32
# ---------------------------------------------------------------------------
def ping_esp32(esp_ip, timeout=1.5):
    try:
        req = urllib.request.Request(f"http://{esp_ip}/ping")
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return resp.status == 200
    except Exception:
        return False

def scan_network_for_esp32(base_ip="192.168.10."):
    print(f"Scanning {base_ip}1-254 for ESP32 display...")
    found = []

    def check_ip(ip):
        if ping_esp32(ip, timeout=0.4):
            found.append(ip)

    ips = [f"{base_ip}{i}" for i in range(1, 255)]
    with ThreadPoolExecutor(max_workers=50) as executor:
        executor.map(check_ip, ips)

    return found[0] if found else None

def send_to_esp32(esp_ip, payload):
    url = f"http://{esp_ip}/track"
    try:
        data = json.dumps(payload).encode("utf-8")
        req = urllib.request.Request(url, data=data, headers={"Content-Type": "application/json"})
        with urllib.request.urlopen(req, timeout=5) as resp:
            return resp.status == 200
    except Exception as e:
        print(f"[ESP32 Error] Could not reach {esp_ip}: {e}")
        return False

# ---------------------------------------------------------------------------
# Robust Window Title Parsing
# ---------------------------------------------------------------------------
IGNORE_LIST = [
    "Visual Studio Code", "cmd.exe", "PowerShell", "Command Prompt",
    "Settings", "Task Manager", "File Explorer", "Calculator"
]

def parse_window_title(title):
    if not title:
        return None, None

    for ig in IGNORE_LIST:
        if ig in title:
            return None, None

    if "playlist by" in title.lower() or " - playlist" in title.lower():
        return None, None

    # Strip browser name
    t = re.sub(r'\s*-\s*(Google Chrome|Microsoft\s*Edge|Mozilla Firefox|Brave)$', '', title, flags=re.IGNORECASE).strip()
    # Strip '| Spotify'
    t = re.sub(r'\s*\|\s*Spotify$', '', t, flags=re.IGNORECASE).strip()

    if t in ("Spotify", "Spotify Free", "Spotify Premium", "Spotify - Web Player", "Spotify - Web Player: Music for everyone"):
        return "PAUSED", "PAUSED"

    # Pattern: "Track - song and lyrics by Artist"
    m = re.match(r'^(.+?)\s+-\s+song (?:and lyrics )?by\s+(.+)$', t, flags=re.IGNORECASE)
    if m:
        return m.group(2).strip(), m.group(1).strip() # (artist, track)

    # Pattern: "Spotify - Track" or "Spotify - Track - Artist"
    if t.startswith("Spotify - "):
        rest = t[10:].strip()
        if " - " in rest:
            p = rest.split(" - ", 1)
            return p[1].strip(), p[0].strip()
        return "", rest

    # Pattern: "Track • Artist"
    if " • " in t:
        p = t.split(" • ", 1)
        return p[1].strip(), p[0].strip()

    # Pattern: "Track - Artist"
    if " - " in t:
        p = t.split(" - ", 1)
        return p[1].strip(), p[0].strip()

    return None, None

user32 = ctypes.windll.user32

def scan_windows():
    found_candidates = []

    def enum_proc(hwnd, lParam):
        try:
            if not user32.IsWindowVisible(hwnd):
                return True
            length = user32.GetWindowTextLengthW(hwnd)
            if length > 0:
                buff = ctypes.create_unicode_buffer(length + 1)
                user32.GetWindowTextW(hwnd, buff, length + 1)
                title = buff.value

                artist, track = parse_window_title(title)
                if artist and track:
                    # Give preference to Spotify-related windows
                    score = 10 if ("spotify" in title.lower() or "chrome" in title.lower() or "edge" in title.lower()) else 1
                    found_candidates.append((score, artist, track, title))
        except Exception:
            return True
        return True

    WNDENUMPROC = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.wintypes.HWND, ctypes.wintypes.LPARAM)
    user32.EnumWindows(WNDENUMPROC(enum_proc), 0)

    if found_candidates:
        # Sort by score (highest first)
        found_candidates.sort(key=lambda x: x[0], reverse=True)
        best = found_candidates[0]
        return best[1], best[2], best[3] # artist, track, raw_title

    return None, None, None

# ---------------------------------------------------------------------------
# Main Loop
# ---------------------------------------------------------------------------
def main():
    print("=" * 65)
    print(" Spotify PC Companion for ESP32 Display")
    print("=" * 65)

    default_ip = get_saved_ip()
    user_input = input(f"Enter ESP32 IP address from screen [{default_ip}]: ").strip()
    esp_ip = user_input if user_input else default_ip

    print(f"\nChecking connection to ESP32 at http://{esp_ip}...")
    if not ping_esp32(esp_ip):
        print(f"[Warning] Could not ping {esp_ip}. Let me auto-scan your network...")
        base = ".".join(esp_ip.split(".")[:3]) + "."
        discovered_ip = scan_network_for_esp32(base)
        if not discovered_ip and base != "192.168.1.":
            discovered_ip = scan_network_for_esp32("192.168.1.")

        if discovered_ip:
            print(f"[FOUND!] Discovered ESP32 display at: {discovered_ip}")
            esp_ip = discovered_ip
        else:
            print(f"[Notice] Auto-scan could not locate it. Check IP on 2.4\" TFT screen.")

    if ping_esp32(esp_ip):
        print(f"[SUCCESS] Connected to ESP32 at {esp_ip}!")
        save_ip(esp_ip)
    else:
        print(f"[Notice] Will still attempt to send updates to {esp_ip}...")

    print("\n[Ready] Listening to Chrome, Edge, and Desktop Spotify!")
    print("=" * 65)

    last_track_key = ""

    while True:
        try:
            artist, track, raw_title = scan_windows()

            if artist and track:
                key = f"{artist} - {track}"

                if key != last_track_key:
                    last_track_key = key

                    if artist == "PAUSED":
                        print("\n[Status] Spotify is Paused / Idle.")
                        send_to_esp32(esp_ip, {"playing": False})
                    else:
                        print(f"\n[Detected Window] {raw_title}")
                        print(f"[Now Playing] {track} by {artist}")

                        info = fetch_poster_and_details(artist, track)
                        info["playing"] = True

                        print(f"  Album:  {info.get('album', 'N/A')}")
                        print(f"  Poster: {info.get('image', 'N/A')}")

                        if send_to_esp32(esp_ip, info):
                            print("  -> Sent to ESP32 successfully!")

            time.sleep(1.0)
        except KeyboardInterrupt:
            print("\nExiting companion.")
            break
        except Exception as e:
            print(f"[Error] {e}")
            time.sleep(1.0)

if __name__ == "__main__":
    main()
