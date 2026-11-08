#ifndef CONFIG_H
#define CONFIG_H

// =========================================================================
// Wi-Fi Configuration
// NOTE: ESP32 only supports 2.4 GHz Wi-Fi networks (not 5 GHz).
// =========================================================================
const char* WIFI_SSID     = "YOUR_2.4GHZ_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// =========================================================================
// Spotify Developer Credentials (Optional for Spotify Free / PC Companion)
// Needed if you have Spotify Premium and wish to use Standalone Cloud Mode.
// Get these at: https://developer.spotify.com/dashboard
// =========================================================================
const char* SPOTIFY_CLIENT_ID     = "YOUR_SPOTIFY_CLIENT_ID";
const char* SPOTIFY_CLIENT_SECRET = "YOUR_SPOTIFY_CLIENT_SECRET";
const char* SPOTIFY_REFRESH_TOKEN = "YOUR_SPOTIFY_REFRESH_TOKEN";

// Polling interval in milliseconds
const unsigned long POLL_INTERVAL_MS = 2000;

#endif // CONFIG_H
