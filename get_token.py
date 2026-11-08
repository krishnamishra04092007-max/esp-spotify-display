"""
Spotify Token Helper Script
This script helps you obtain your Spotify Refresh Token for the ESP32 project.
It starts a local web server, opens Spotify authorization in your browser,
and prints your REFRESH_TOKEN automatically.
"""

import http.server
import socketserver
import webbrowser
import urllib.parse
import urllib.request
import json
import base64
import sys

PORT = 8888
REDIRECT_URI = f"http://127.0.0.1:{PORT}/callback"
SCOPE = "user-read-currently-playing user-read-playback-state"

client_id = ""
client_secret = ""

class SpotifyAuthHandler(http.server.SimpleHTTPRequestHandler):
    def do_GET(self):
        global client_id, client_secret
        if self.path.startswith("/callback"):
            parsed = urllib.parse.urlparse(self.path)
            query = urllib.parse.parse_qs(parsed.query)

            if "code" in query:
                code = query["code"][0]
                self.send_response(200)
                self.send_header("Content-type", "text/html")
                self.end_headers()
                self.wfile.write(b"<h1>Authorization Successful!</h1><p>You can close this tab and return to your terminal.</p>")

                # Exchange auth code for refresh token
                token_url = "https://accounts.spotify.com/api/token"
                headers = {
                    "Authorization": "Basic " + base64.b64encode(f"{client_id}:{client_secret}".encode()).decode(),
                    "Content-Type": "application/x-www-form-urlencoded"
                }
                data = urllib.parse.urlencode({
                    "grant_type": "authorization_code",
                    "code": code,
                    "redirect_uri": REDIRECT_URI
                }).encode()

                req = urllib.request.Request(token_url, data=data, headers=headers)
                try:
                    with urllib.request.urlopen(req) as response:
                        tokens = json.loads(response.read().decode())
                        refresh_token = tokens.get("refresh_token")
                        print("\n" + "="*60)
                        print("SUCCESS! Here is your Spotify Refresh Token:")
                        print("="*60)
                        print(f"\nREFRESH_TOKEN:\n{refresh_token}\n")
                        print("="*60)
                        print("Copy this Refresh Token and paste it into config.h")
                        print("="*60)
                except Exception as e:
                    print(f"Error fetching tokens: {e}")
                
                # Exit after receiving token
                sys.exit(0)
            else:
                self.send_response(400)
                self.wfile.write(b"Failed to receive authorization code.")
        else:
            self.send_response(404)
            self.end_headers()

    def log_message(self, format, *args):
        # Silence standard HTTP logs
        return

def main():
    global client_id, client_secret
    print("=" * 60)
    print(" Spotify ESP32 Refresh Token Generator")
    print("=" * 60)
    print("\nMake sure you added the following Redirect URI in your Spotify App settings:")
    print(f"--> {REDIRECT_URI}\n")

    client_id = input("Enter your Spotify Client ID: ").strip()
    client_secret = input("Enter your Spotify Client Secret: ").strip()

    if not client_id or not client_secret:
        print("Error: Client ID and Secret are required.")
        return

    auth_url = (
        "https://accounts.spotify.com/authorize?"
        + urllib.parse.urlencode({
            "client_id": client_id,
            "response_type": "code",
            "redirect_uri": REDIRECT_URI,
            "scope": SCOPE
        })
    )

    print("\nOpening your browser for Spotify authorization...")
    webbrowser.open(auth_url)

    with socketserver.TCPServer(("", PORT), SpotifyAuthHandler) as httpd:
        print(f"Waiting for authorization callback on port {PORT}...")
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nCancelled by user.")

if __name__ == "__main__":
    main()
