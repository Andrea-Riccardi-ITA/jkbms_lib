#include <Arduino.h>
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "libs/JKBMS.h"
#include "libs/debug_functions.h"

/**
 * @brief Global array of JKBMS device instances
 * 
 * Configure your BMS devices by adding their MAC addresses here.
 * Each entry represents one BMS device that the system will monitor.
 */
JKBMS jkBmsDevices[] = {
  JKBMS("c8:47:80:31:9b:02"), // Example Mac address of a JKBMS device
};

const int bmsDeviceCount = sizeof(jkBmsDevices) / sizeof(jkBmsDevices[0]);

// BLE Scanning
NimBLEScan* pScan;
unsigned long lastScanTime = 0;
ScanCallbacks scanCallbacks;

// Debug functions for JKBMS library
void debugPrintForJKBMS(const char* format, ...) {
  char buffer[256];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  Serial.print(buffer);
}

void debugPrintlnForJKBMS(const char* message) {
  Serial.println(message);
}

void debugPrintSimpleForJKBMS(const char* message) {
  Serial.print(message);
}

//********************************************
// Main Program
//********************************************
void setup() {
  Serial.begin(115200);
  
  // Set up debug functions for JKBMS library
  debugPrintFunc = debugPrintForJKBMS;
  debugPrintlnFunc = debugPrintlnForJKBMS;
  debugPrintSimpleFunc = debugPrintSimpleForJKBMS;

  // Initialize NimBLE first (used to communicate with JKBMS)
  DEBUG_PRINTLN("Initializing NimBLE");
  NimBLEDevice::init("Photon test");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9); // Maximum power for better range
  
  // Set larger BLE configurations for stability
  NimBLEDevice::setMTU(517); // Larger MTU for better throughput

  // Setup NimBLE scan for JKBMS communication with more conservative settings
  pScan = NimBLEDevice::getScan();
  pScan->setScanCallbacks(&scanCallbacks);
  pScan->setInterval(1600); // 1000ms scan interval (less aggressive)
  pScan->setWindow(100);    // 62.5ms scan window
  pScan->setActiveScan(true);
  
  delay(3000); // Wait for BLE stack to stabilize and turning on the BMS 
  
  DEBUG_PRINTLN("Setup complete!");
}

void loop() {
  // Connection management for BMS devices
  int connectedCount = 0;
  static unsigned long lastConnectionAttempt = 0;

  for (int i = 0; i < bmsDeviceCount; i++) {
    if (jkBmsDevices[i].targetMAC.empty()) continue;

    // Attempt to connect if not already connected
    // Add delay between connection attempts to reduce resource conflicts
    if (jkBmsDevices[i].doConnect && !jkBmsDevices[i].connected) {
      if (millis() - lastConnectionAttempt > 5000) {  // Wait 5 seconds between attempts
        if (jkBmsDevices[i].connectToServer()) {
          DEBUG_PRINTF("%s connected successfully\n", jkBmsDevices[i].targetMAC.c_str());
        } else {
          DEBUG_PRINTF("%s connection failed\n", jkBmsDevices[i].targetMAC.c_str());
        }
        jkBmsDevices[i].doConnect = false;
        lastConnectionAttempt = millis();
      }
    }

    // Check connection status and handle timeouts 
    if (jkBmsDevices[i].connected) {
      connectedCount++;
      if (millis() - jkBmsDevices[i].lastNotifyTime > 25000) {  // Increased timeout
        DEBUG_PRINTF("%s connection timeout (no data for 25s)\n", jkBmsDevices[i].targetMAC.c_str());
        NimBLEClient* pClient = NimBLEDevice::getClientByPeerAddress(jkBmsDevices[i].advDevice->getAddress());
        if (pClient) {
          pClient->disconnect();
          jkBmsDevices[i].connected = false;
        }
      }
    }
  }

  // Start scan only if not all devices are connected and enough time has passed
  // Reduce scan frequency to minimize conflicts with mobile app and improve stability
  bool shouldScan = (connectedCount < bmsDeviceCount) && 
                    (millis() - lastScanTime >= 20000) && // Scan every 20 seconds
                    (millis() - lastConnectionAttempt > 10000); // Wait 10s after connection attempts
  
  if (shouldScan) {
    DEBUG_PRINTF("Starting BMS scan... (Connected: %d/%d)\n", connectedCount, bmsDeviceCount);
    pScan->start(3000, false, true); // 3 second scan duration
    lastScanTime = millis();
  }

  // Small delay to prevent excessive CPU usage and allow BLE stack to process
  // Increased delay for better stability (ideally the BMS need 100ms between requests)
  delay(100);
}