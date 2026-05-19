# 🧠 Self-Healing IoT Mesh System (ESP32 + ESP-NOW + AI Backend)

An advanced IoT mesh networking project built using ESP32 nodes, ESP-NOW communication, WiFi gateway, and a Node.js backend integrated with AI (Gemini API) and Firebase. The system is designed for **self-healing communication, real-time sensor monitoring, and smart device control**.

---

## 🚀 Key Features

- 🔗 **Self-Healing Mesh Network**
  - Automatic rerouting if a node fails
  - ESP-NOW peer-to-peer communication

- 🌡️ **Real-Time Sensor Monitoring**
  - DHT11 temperature & humidity sensors
  - Live data transmission between nodes

- 📡 **WiFi Gateway (Node A)**
  - ESP32 SoftAP (192.168.4.1)
  - Web dashboard access from phone/browser

- 🔌 **Relay Control System (Node C)**
  - Remote switching via mesh or backend

- 🖥️ **Backend Server (Node.js)**
  - Data collection and processing
  - REST API for IoT communication

- 🤖 **AI Integration (Gemini API)**
  - Smart decision-making
  - Data analysis & predictions

- ☁️ **Cloud Sync (Firebase)**
  - Real-time database updates
  - Remote monitoring support

---

## 🏗️ System Architecture

---

## 🔧 Hardware Requirements

- ESP32 Dev Boards (x3)
- DHT11 Temperature & Humidity Sensors (x2)
- Relay Module (x1)
- Jumper Wires
- Power Supply

---

## 💻 Software Requirements

- Arduino IDE / PlatformIO
- Node.js (Backend)
- Firebase Account
- Google Gemini API Key

---

## 📂 Project Structure

```text
Self-Healing-Mesh-IoT/
│
├── esp32-firmware/
│   ├── node_a_gateway.ino
│   ├── node_b_sensor.ino
│   ├── node_c_relay.ino
│
├── backend/
│   ├── server.js
│   ├── firebase.js
│   ├── routes/
│
├── frontend/
│   ├── dashboard.html
│
├── docs/
│   ├── architecture.png
│
└── README.md
