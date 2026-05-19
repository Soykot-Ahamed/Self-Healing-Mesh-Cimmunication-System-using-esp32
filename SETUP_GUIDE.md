# MeshChat — Complete Setup Guide
## ESP32 Mesh + DHT11 + Firebase + Gemini AI

---

## WIRING (Node A and Node B)

```
ESP32 Pin   →   DHT11 Pin
3.3V        →   VCC  (pin 1)
GPIO 4      →   DATA (pin 2)  + 10kΩ pull-up between DATA and 3.3V
GND         →   GND  (pin 4)
```
Node C has NO DHT11.

---

## STEP 1 — Install Arduino Libraries

Tools → Manage Libraries → install:
- DHT sensor library (Adafruit)
- Adafruit Unified Sensor
- ArduinoJson by Benoit Blanchon (v6.x)

Board: ESP32 Dev Module, 240MHz, 4MB

---

## STEP 2 — Add Your WiFi Networks

In each .ino, edit knownNets[]:
```cpp
KnownWiFi knownNets[] = {
  {"YourHomeWiFi",   "homepassword"},
  {"OfficeWiFi",     "officepass"},
  {"MyHotspot",      "hotspotpass"},
};
```
Node auto-scans and connects to whichever it finds. Dynamic too — use the WiFi panel in UI.

---

## STEP 3 — Find Your Ubuntu PC IP and Update BACKEND_URL

```bash
ip addr show | grep "inet "
# Example result: inet 192.168.1.100/24
```

In each .ino update:
```cpp
#define BACKEND_URL "http://192.168.1.100:3000"
```

---

## STEP 4 — Firebase Setup (optional)

1. firebase.google.com → Create project → Realtime Database → Test mode
2. Copy DB URL → paste into each .ino:
```cpp
#define FIREBASE_URL "https://YOUR-PROJECT-default-rtdb.firebaseio.com"
```
Data is pushed to /nodes/A, /nodes/B, /nodes/C every 15s when WiFi is connected.

---

## STEP 5 — Run Backend Server on Ubuntu

```bash
cd Backend/
sudo apt install nodejs npm   # if not installed
npm install
node server.js
```

Keep terminal open. To run in background:
```bash
nohup node server.js > server.log 2>&1 &
```

Test it:
```bash
curl http://localhost:3000/
```

---

## STEP 6 — Flash the ESP32s

- NodeA.ino → ESP32 #1
- NodeB.ino → ESP32 #2  
- NodeC.ino → ESP32 #3

Serial Monitor at 115200 to see startup info.

---

## STEP 7 — Connect and Use

| WiFi Name   | Password  | URL             |
|-------------|-----------|-----------------|
| MeshChat-A  | mesh1234  | 192.168.4.1     |
| MeshChat-B  | mesh1234  | 192.168.4.1     |
| MeshChat-C  | mesh1234  | 192.168.4.1     |

---

## How Gemini Works

```
User types question
  → ESP32 POST /gemini to your backend PC
    → Backend adds sensor context + calls Gemini API
      → Answer returned to ESP32
        → Shown in UI + broadcast to all nodes via ESP-NOW
```
API key stays on backend only. ESP32 is never burdened by heavy HTTPS calls.

---

## Architecture

```
  Phone/Browser
      │ WiFi AP (192.168.4.1)
      ▼
  Node A (DHT11) ◄──ESP-NOW──► Node B (DHT11)
       │                              │
       └──────► Node C (Relay) ◄──────┘
                     │
              WiFi (when available)
                     │
         ┌───────────┴──────────┐
         │  Backend (Ubuntu PC) │──► Gemini API
         │  node server.js      │──► Firebase
         └──────────────────────┘
```

---

## All Features

- Offline ESP-NOW mesh chat (broadcast + private)
- Auto-relay via Node C when direct link fails
- DHT11 temperature + humidity on Node A and B
- Sensor data shown in UI header, broadcast every 5s
- Auto WiFi scan — connects to any known network
- Manual WiFi join from UI (no reflashing needed)
- Firebase push every 15s when WiFi available
- Gemini AI answers via backend (ESP stays lightweight)
- Gemini answers broadcast to all nodes
- Node online/offline dots in header
- Relay tag shown on relayed messages
- Same UI on all 3 nodes at 192.168.4.1
