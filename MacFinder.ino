/*
 * ============================================================
 *  MAC ADDRESS FINDER
 *  Flash this to EACH ESP32 first, then open Serial Monitor
 *  Copy the MAC address and paste it into NodeA/B/C.ino
 * ============================================================
 *  Steps:
 *  1. Flash this sketch to ESP32 #1
 *  2. Open Serial Monitor at 115200 baud
 *  3. Write down the MAC address shown (e.g. AA:BB:CC:DD:EE:01)
 *  4. Repeat for ESP32 #2 and #3
 *  5. Then update MAC_A, MAC_B, MAC_C in each NodeX.ino
 * ============================================================
 */

#include <WiFi.h>

void setup() {
  Serial.begin(115200);
  delay(1000);

  WiFi.mode(WIFI_STA);
  Serial.println("\n==========================================");
  Serial.print("  This ESP32 MAC Address: ");
  Serial.println(WiFi.macAddress());
  Serial.println("==========================================");
  Serial.println("Copy this address into the correct node file.");
  Serial.println("Format for code: {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}");

  // Parse and print in array format
  String mac = WiFi.macAddress();
  Serial.print("Array format: {");
  for (int i = 0; i < 6; i++) {
    String hex = mac.substring(i*3, i*3+2);
    Serial.print("0x"); Serial.print(hex);
    if (i < 5) Serial.print(", ");
  }
  Serial.println("}");
}

void loop() {
  // Nothing needed
}
