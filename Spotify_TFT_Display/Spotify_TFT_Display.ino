/*
 * =========================================================================
 * Spotify Album Poster & Track Display for ESP32 (2.4" TFT)
 * =========================================================================
 * Board: ESP32 (e.g., WeMos D1 R32 Uno-format board)
 * Display: 2.4" TFT LCD Shield (8-bit parallel, MCUFRIEND_kbv)
 * Mode: PC Companion Mode (Supports Spotify Free & Spotify Web Player)
 * =========================================================================
 */

#include <Adafruit_GFX.h>
#include <MCUFRIEND_kbv.h>
#include <TJpg_Decoder.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include "config.h"

// -------------------------------------------------------------------------
// Display & Server Instances
// -------------------------------------------------------------------------
MCUFRIEND_kbv tft;
WebServer server(80);

// -------------------------------------------------------------------------
// Color Palette (RGB565)
// -------------------------------------------------------------------------
#define COLOR_BLACK       0x0000
#define COLOR_WHITE       0xFFFF
#define COLOR_SPOTIFY     0x1DB8   // Spotify Green #1DB954
#define COLOR_DARK_GRAY   0x2104   // Dark gray
#define COLOR_MED_GRAY    0x8410   // Medium gray
#define COLOR_LIGHT_GRAY  0xCE59   // Light gray
#define COLOR_ORANGE      0xFD20   // Orange

// -------------------------------------------------------------------------
// State Variables
// -------------------------------------------------------------------------
String currentTrackName = "";
String currentArtistName = "";
String currentAlbumName = "";
String currentImageUrl = "";
bool isCurrentlyPlaying = false;
unsigned long progressMs = 0;
unsigned long durationMs = 0;
unsigned long lastProgressUpdate = 0;
int lastBarWidth = -1;

// -------------------------------------------------------------------------
// TJpg_Decoder Callback for MCUFRIEND_kbv
// -------------------------------------------------------------------------
bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  if (y >= tft.height() || x >= tft.width()) return 0;
  tft.setAddrWindow(x, y, x + w - 1, y + h - 1);
  tft.pushColors(bitmap, w * h, 1);
  return 1;
}

// -------------------------------------------------------------------------
// Forward Declarations
// -------------------------------------------------------------------------
void handleTrackUpdate();
void handlePing();
void downloadAndDrawImage(const String& url, int x, int y);
void drawWaitingScreen();
void drawTrackLayout();
void updateProgressBar();
String formatTime(unsigned long ms);
void printWrapped(const String& text, int x, int y, int maxWidth, int maxLines, int size, uint16_t color);

// =========================================================================
// Setup
// =========================================================================
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n[Spotify Display] Initializing...");

  // 1. CONNECT WI-FI FIRST
  // (Crucial on ESP32: Wi-Fi stack touches ADC2 pins on startup;
  //  connecting first ensures tft.begin() configures display pins cleanly!)
  Serial.print("[Wi-Fi] Connecting to: ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 40) {
    delay(400);
    Serial.print(".");
    retries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[Wi-Fi] Connected! IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\n[Wi-Fi] Connection Failed! Check SSID/Password.");
  }

  // 2. INITIALIZE TFT DISPLAY (After Wi-Fi is established)
  uint16_t identifier = tft.readID();
  Serial.printf("[TFT] LCD ID found: 0x%04X\n", identifier);
  if (identifier == 0 || identifier == 0xD3D3) {
    identifier = 0x9341;
  }
  tft.begin(identifier);
  tft.setRotation(1); // Landscape mode (320x240)
  tft.fillRect(0, 0, 320, 240, COLOR_BLACK);

  // Initialize JPEG Decoder
  TJpgDec.setCallback(tft_output);
  TJpgDec.setJpgScale(2);       // Scale 300x300 down by 1/2 -> 150x150
  TJpgDec.setSwapBytes(false);  // Swap if colors are inverted

  // 3. START HTTP SERVER
  server.on("/track", HTTP_POST, handleTrackUpdate);
  server.on("/ping", HTTP_GET, handlePing);
  server.begin();
  Serial.println("[WebServer] Listening on port 80 for PC Companion updates");

  // 4. DRAW READY SCREEN
  drawWaitingScreen();
}

// =========================================================================
// Main Loop
// =========================================================================
void loop() {
  server.handleClient();

  // Increment progress bar when music is playing
  if (isCurrentlyPlaying && durationMs > 0) {
    unsigned long elapsed = millis() - lastProgressUpdate;
    if (elapsed >= 1000) {
      progressMs += elapsed;
      if (progressMs > durationMs) progressMs = durationMs;
      lastProgressUpdate = millis();
      updateProgressBar();
    }
  }

  delay(10);
}

// =========================================================================
// WebServer Handlers
// =========================================================================
void handlePing() {
  server.send(200, "text/plain", "OK");
}

void handleTrackUpdate() {
  String body = server.arg("plain");
  Serial.println("[WebServer] Received track payload: " + body);

#if ARDUINOJSON_VERSION_MAJOR >= 7
  JsonDocument doc;
#else
  DynamicJsonDocument doc(4096);
#endif

  DeserializationError error = deserializeJson(doc, body);
  if (error) {
    Serial.println("[WebServer] JSON Parse Error: " + String(error.c_str()));
    server.send(400, "text/plain", "Invalid JSON");
    return;
  }

  server.send(200, "application/json", "{\"status\":\"ok\"}");

  bool playing = doc["playing"] | false;

  if (!playing) {
    if (isCurrentlyPlaying) {
      isCurrentlyPlaying = false;
      // Mark as PAUSED on header badge
      tft.fillRect(235, 8, 75, 16, COLOR_BLACK);
      tft.setTextSize(1);
      tft.setTextColor(COLOR_ORANGE);
      tft.setCursor(245, 12);
      tft.print("PAUSED");
    }
    return;
  }

  String title = doc["title"] | "Unknown Title";
  String artist = doc["artist"] | "Unknown Artist";
  String album = doc["album"] | "";
  String imageUrl = doc["image"] | "";
  unsigned long duration = doc["duration_ms"] | 0;

  bool trackChanged = (title != currentTrackName || artist != currentArtistName);

  currentTrackName = title;
  currentArtistName = artist;
  currentAlbumName = album;
  isCurrentlyPlaying = true;
  durationMs = duration;
  progressMs = 0;
  lastProgressUpdate = millis();

  if (trackChanged) {
    drawTrackLayout();
    if (imageUrl.length() > 0 && imageUrl != currentImageUrl) {
      currentImageUrl = imageUrl;
      downloadAndDrawImage(imageUrl, 10, 32);
    }
  }

  updateProgressBar();
}

// =========================================================================
// Image Downloader & Renderer
// =========================================================================
void downloadAndDrawImage(const String& url, int x, int y) {
  Serial.println("[Image] Fetching poster: " + url);
  WiFiClientSecure client;
  client.setInsecure(); // Save memory, skip root cert validation

  HTTPClient http;
  http.begin(client, url);

  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    int totalLen = http.getSize();
    if (totalLen > 0 && totalLen < 60000) {
      uint8_t* imgBuffer = (uint8_t*)malloc(totalLen);
      if (imgBuffer) {
        WiFiClient* stream = http.getStreamPtr();
        int bytesRead = 0;
        unsigned long timeout = millis() + 6000;

        while (http.connected() && (bytesRead < totalLen) && (millis() < timeout)) {
          size_t avail = stream->available();
          if (avail) {
            int r = stream->readBytes(imgBuffer + bytesRead, totalLen - bytesRead);
            bytesRead += r;
          }
          delay(1);
        }

        if (bytesRead == totalLen) {
          TJpgDec.drawJpg(x, y, imgBuffer, totalLen);
          tft.drawRect(x - 1, y - 1, 152, 152, COLOR_SPOTIFY);
          Serial.println("[Image] Poster rendered successfully!");
        } else {
          Serial.printf("[Image] Stream incomplete (%d of %d bytes)\n", bytesRead, totalLen);
        }
        free(imgBuffer);
      } else {
        Serial.println("[Image] Failed to allocate RAM for image.");
      }
    } else {
      Serial.printf("[Image] Unexpected image size: %d\n", totalLen);
    }
  } else {
    Serial.printf("[Image] HTTP download failed: %d\n", httpCode);
  }
  http.end();
}

// =========================================================================
// UI Layouts
// =========================================================================
void drawWaitingScreen() {
  Serial.println("[UI] Drawing Waiting Screen...");
  tft.fillRect(0, 0, 320, 240, COLOR_BLACK);
  delay(50);

  // Top Title
  tft.fillRect(15, 14, 8, 8, COLOR_SPOTIFY);
  tft.setTextColor(COLOR_SPOTIFY, COLOR_BLACK);
  tft.setTextSize(2);
  tft.setCursor(30, 10);
  tft.print("Spotify Ready!   ");

  // IP Address info
  tft.setTextSize(1);
  tft.setTextColor(COLOR_LIGHT_GRAY, COLOR_BLACK);
  tft.setCursor(30, 60);
  tft.print("ESP32 IP Address:           ");

  tft.setTextSize(2);
  tft.setTextColor(COLOR_WHITE, COLOR_BLACK);
  tft.setCursor(30, 75);
  tft.print(WiFi.localIP().toString());

  // Next steps
  tft.setTextSize(1);
  tft.setTextColor(COLOR_MED_GRAY, COLOR_BLACK);
  tft.setCursor(30, 120);
  tft.print("PC Companion Connected!     ");

  tft.setTextSize(2);
  tft.setTextColor(COLOR_SPOTIFY, COLOR_BLACK);
  tft.setCursor(30, 140);
  tft.print("HIT PLAY ON SPOTIFY ");

  tft.setTextSize(1);
  tft.setTextColor(COLOR_LIGHT_GRAY, COLOR_BLACK);
  tft.setCursor(30, 185);
  tft.print("Playing in Chrome will show here!");
}

void drawTrackLayout() {
  tft.fillScreen(COLOR_BLACK);
  lastBarWidth = -1;

  // Header
  tft.fillRect(12, 12, 6, 6, COLOR_SPOTIFY);
  tft.setTextColor(COLOR_SPOTIFY);
  tft.setTextSize(1);
  tft.setCursor(24, 12);
  tft.print("SPOTIFY");

  // Status Badge
  tft.setCursor(245, 12);
  if (isCurrentlyPlaying) {
    tft.setTextColor(COLOR_SPOTIFY);
    tft.print("PLAYING");
  } else {
    tft.setTextColor(COLOR_ORANGE);
    tft.print("PAUSED");
  }

  // Placeholder frame for poster
  tft.drawRect(9, 31, 152, 152, COLOR_DARK_GRAY);

  // Track Title (right side)
  printWrapped(currentTrackName, 170, 36, 140, 3, 2, COLOR_WHITE);

  // Artist Name
  printWrapped(currentArtistName, 170, 95, 140, 2, 1, COLOR_LIGHT_GRAY);

  // Album Name
  printWrapped(currentAlbumName, 170, 120, 140, 2, 1, COLOR_MED_GRAY);

  // Progress Bar Frame (x: 10, y: 195, w: 300, h: 6)
  tft.fillRect(10, 195, 300, 6, COLOR_DARK_GRAY);
}

void updateProgressBar() {
  if (durationMs == 0) return;

  int barMaxWidth = 300;
  int currentBarWidth = (int)(((float)progressMs / (float)durationMs) * barMaxWidth);
  if (currentBarWidth > barMaxWidth) currentBarWidth = barMaxWidth;

  if (currentBarWidth != lastBarWidth) {
    if (currentBarWidth > 0) {
      tft.fillRect(10, 195, currentBarWidth, 6, COLOR_SPOTIFY);
    }
    lastBarWidth = currentBarWidth;
  }

  // Time string (mm:ss / mm:ss)
  tft.setTextSize(1);
  tft.fillRect(10, 208, 300, 12, COLOR_BLACK);

  tft.setTextColor(COLOR_MED_GRAY);
  tft.setCursor(10, 210);
  tft.print(formatTime(progressMs));

  String durStr = formatTime(durationMs);
  int durWidth = durStr.length() * 6;
  tft.setCursor(310 - durWidth, 210);
  tft.print(durStr);
}

void printWrapped(const String& text, int x, int y, int maxWidth, int maxLines, int size, uint16_t color) {
  tft.setTextColor(color, COLOR_BLACK);
  tft.setTextSize(size);
  int charWidth = 6 * size;
  int lineHeight = 9 * size;
  int maxCharsPerLine = maxWidth / charWidth;

  int startIdx = 0;
  int line = 0;

  while (startIdx < text.length() && line < maxLines) {
    int take = min(maxCharsPerLine, (int)(text.length() - startIdx));
    String lineStr = text.substring(startIdx, startIdx + take);

    if (line == maxLines - 1 && (startIdx + take) < text.length() && lineStr.length() >= 3) {
      lineStr = lineStr.substring(0, lineStr.length() - 3) + "...";
    }

    tft.setCursor(x, y + (line * lineHeight));
    tft.print(lineStr);

    startIdx += take;
    line++;
  }
}

String formatTime(unsigned long ms) {
  unsigned long totalSeconds = ms / 1000;
  unsigned int minutes = totalSeconds / 60;
  unsigned int seconds = totalSeconds % 60;
  char buf[10];
  snprintf(buf, sizeof(buf), "%02u:%02u", minutes, seconds);
  return String(buf);
}
