/**
 * @file JKBMS.cpp
 * @brief JKBMS Battery Management System communication library
 * 
 * This file implements the JKBMS class for communicating with JKBMS devices
 * via Bluetooth Low Energy (BLE). It handles device discovery, connection
 * management, data parsing, and command transmission.
 * 
 * Features:
 * - Multi-device BMS support
 * - Real-time data monitoring
 * - Settings configuration
 * - Error handling and reconnection
 * - Memory-efficient data processing
 * 
 * @author Andrea Riccardi <PhotonSolarSystems SRL>
 * @date 2025
 */

#include "JKBMS.h"

//********************************************
// JKBMS Class Implementation
//********************************************

/**
 * @brief Constructor for JKBMS class
 * 
 * Initializes a new JKBMS instance with the specified MAC address.
 * Sets up default values for all data fields and prepares the instance
 * for BLE connection and data processing.
 * 
 * @param mac The MAC address of the BMS device to connect to (format: "xx:xx:xx:xx:xx:xx")
 */
JKBMS::JKBMS(const std::string& mac) : targetMAC(mac) {
  // Initialize all data fields to safe defaults
  connected = false;
  doConnect = false;
  pChr = nullptr;
  advDevice = nullptr;
  lastNotifyTime = 0;
  frame = 0;
  received_start = false;
  received_complete = false;
  new_data = false;
  ignoreNotifyCount = 0;
  
  // Clear data buffers
  memset(receivedBytes, 0, sizeof(receivedBytes));
  memset(cellVoltage, 0, sizeof(cellVoltage));
  memset(wireResist, 0, sizeof(wireResist));
}

/**
 * @brief Establishes BLE connection to the BMS server
 * 
 * Creates BLE client, connects to device, subscribes to notifications, and requests initial data.
 * Implements proper error handling and connection parameter optimization for reliable communication.
 * 
 * @return true if connection and setup successful, false otherwise
 */
bool JKBMS::connectToServer() {
  DEBUG_PRINTF("Attempting to connect to %s...\n", targetMAC.c_str());
  
  // Check if client already exists for this device
  NimBLEClient* pClient = NimBLEDevice::getClientByPeerAddress(advDevice->getAddress());

  if (!pClient) {
    // Check total client count to avoid resource exhaustion
    // ESP32 typically supports 3-4 concurrent BLE connections
    if (NimBLEDevice::getCreatedClientCount() >= 3) {
      DEBUG_PRINTF("Maximum BLE connections reached (%d)\n", NimBLEDevice::getCreatedClientCount());
      return false;
    }
    
    // Create new client with optimized settings
    pClient = NimBLEDevice::createClient();
    if (!pClient) {
      DEBUG_PRINTF("Failed to create BLE client for %s\n", targetMAC.c_str());
      return false;
    }
    
    DEBUG_PRINTLN("New BLE client created.");
    pClient->setClientCallbacks(new ClientCallbacks(this), true);
    
    // More conservative connection parameters for multi-BLE stability
    // Interval: 24*1.25ms = 30ms, Latency: 0, Timeout: 400*10ms = 4s
    pClient->setConnectionParams(24, 24, 0, 400);
    pClient->setConnectTimeout(10000);  // 10 second connection timeout
  }

  // Add small delay to avoid resource conflicts with BLE server
  delay(100);

  // Attempt connection with retry logic
  int retryCount = 0;
  const int maxRetries = 3;
  
  while (retryCount < maxRetries) {
    DEBUG_PRINTF("Connection attempt %d/%d to %s...\n", retryCount + 1, maxRetries, targetMAC.c_str());
    
    if (pClient->connect(advDevice)) {
      DEBUG_PRINTF("Connected to: %s RSSI: %d (attempt %d)\n", 
                   pClient->getPeerAddress().toString().c_str(), 
                   pClient->getRssi(), retryCount + 1);
      break;
    }
    
    // Get more specific error information
    int connectionState = pClient->isConnected() ? 1 : 0;
    DEBUG_PRINTF("Connection attempt %d failed for %s (state: %d)\n", 
                 retryCount + 1, targetMAC.c_str(), connectionState);
    
    retryCount++;
    
    if (retryCount < maxRetries) {
      delay(2000 + (1000 * retryCount));  // Progressive delay: 3s, 4s, 5s
    }
  }
  
  if (retryCount >= maxRetries) {
    DEBUG_PRINTF("Failed to connect to %s after %d attempts\n", targetMAC.c_str(), maxRetries);
    return false;
  }

  // Get the service and characteristic for JKBMS communication
  NimBLERemoteService* pSvc = nullptr;
  
  // Retry service discovery
  for (int i = 0; i < 3; i++) {
    delay(500);  // Allow time for service discovery
    pSvc = pClient->getService("ffe0");
    if (pSvc) break;
    DEBUG_PRINTF("Service discovery attempt %d failed\n", i + 1);
  }
  
  if (pSvc) {
    pChr = pSvc->getCharacteristic("ffe1");
    if (pChr && pChr->canNotify()) {
      // Subscribe to notifications for real-time data
      if (pChr->subscribe(true, notifyCB)) {
        DEBUG_PRINTF("Successfully subscribed to notifications for %s\n", pChr->getUUID().toString().c_str());
        
        // Request initial device information and data with proper delays
        delay(1000);  // Longer initial delay for stability
        writeRegister(0x97, 0x00000000, 0x00);  // Request device info
        delay(800);
        writeRegister(0x96, 0x00000000, 0x00);  // Request cell info
        delay(800);

        // Enable BMS functions (charge, discharge, balance)
        enableBMSFunctions();
        
        connected = true;
        lastNotifyTime = millis();
        DEBUG_PRINTF("BMS %s fully connected and initialized\n", targetMAC.c_str());
        return true;
      } else {
        DEBUG_PRINTLN("Failed to subscribe to notifications");
      }
    } else {
      DEBUG_PRINTLN("Characteristic ffe1 not found or cannot notify");
    }
  } else {
    DEBUG_PRINTLN("Service 'ffe0' not found");
  }
  
  // Connection failed, disconnect client
  DEBUG_PRINTF("Connection setup failed for %s, disconnecting\n", targetMAC.c_str());
  pClient->disconnect();
  return false;
}

/**
 * Enable BMS functions (charge, discharge, balance)
 * Sends commands to enable charging, discharging, and balancing functions
 * Should be called after successful BMS connection and initialization
 */
void JKBMS::enableBMSFunctions() {
  DEBUG_PRINTF("Enabling BMS functions for %s\n", targetMAC.c_str());
  
  // Enable charging (address 0x1D, value 0x00000001)
  writeRegister(0x1D, 0x00000001, 0x04);
  delay(500); // Delay between commands for stability
  
  // Enable discharging (address 0x1E, value 0x00000001)
  writeRegister(0x1E, 0x00000001, 0x04);
  delay(500);
  
  // Enable balancing (address 0x1F, value 0x00000001)
  writeRegister(0x1F, 0x00000001, 0x04);
  delay(500);
  
  DEBUG_PRINTF("BMS functions enabled for %s\n", targetMAC.c_str());
}

/**
 * Handle incoming BLE notifications from the JK BMS device
 * 
 * This is the core notification handler that processes incoming data frames from the BMS.
 * It implements a state machine to handle multi-packet data frames and dispatches
 * complete frames to appropriate parsing functions based on frame type.
 * 
 * Protocol Frame Structure:
 * - Start bytes: 0x55 0xAA 0xEB 0x90 (4 bytes)
 * - Frame type: byte 4 (0x01=settings, 0x02=cell data, 0x03=device info)
 * - Data payload: variable length
 * - Maximum frame size: 300 bytes
 * 
 * State Machine:
 * 1. Wait for start frame (0x55 0xAA 0xEB 0x90)
 * 2. Accumulate subsequent packets until frame >= 300 bytes
 * 3. Dispatch complete frame to appropriate parser
 * 4. Reset state for next frame
 * 
 * @param pData Pointer to the received notification data buffer
 * @param length Number of bytes in the received notification
 * 
 * @note This function maintains global state via received_start, received_complete,
 *       frame counter, and receivedBytes buffer
 * @note Implements notification throttling via ignoreNotifyCount mechanism
 * @note Updates lastNotifyTime for connection monitoring
 */
void JKBMS::handleNotification(uint8_t* pData, size_t length) {
  DEBUG_PRINTLN("Handling notification...");
  lastNotifyTime = millis();

  // Handle notification throttling - skip processing if count > 0
  if (ignoreNotifyCount > 0) {
    ignoreNotifyCount--;
    DEBUG_PRINTF("Ignoring notification. Remaining: %d\n", ignoreNotifyCount);
    return;
  }

  // Bounds check for minimum frame header
  if (length < 4) {
    DEBUG_PRINTF("Notification too short: %d bytes\n", length);
    return;
  }

  // Check for start of new data frame (JK BMS protocol header)
  if (pData[0] == 0x55 && pData[1] == 0xAA && pData[2] == 0xEB && pData[3] == 0x90) {
    DEBUG_PRINTLN("Start of data frame detected.");
    frame = 0;
    received_start = true;
    received_complete = false;

    // Store the received data with bounds checking
    for (int i = 0; i < length && frame < 300; i++) {
      receivedBytes[frame++] = pData[i];
    }
  } 
  // Continue accumulating data for an already started frame
  else if (received_start && !received_complete) {
    DEBUG_PRINTLN("Continuing data frame...");
    
    // Append new data to existing frame with bounds checking
    for (int i = 0; i < length && frame < 300; i++) {
      receivedBytes[frame++] = pData[i];
      
      // Check if we've received a complete frame (300 bytes)
      if (frame >= 300) {
        received_complete = true;
        received_start = false;
        new_data = true;
        DEBUG_PRINTLN("New data available for parsing.");

        // Determine frame type and dispatch to appropriate parser
        // Frame type is stored in byte 4 of the complete frame
        switch (receivedBytes[4]) {
          case 0x01:
            DEBUG_PRINTLN("BMS Settings frame detected.");
            bms_settings();
            break;
          case 0x02:
            DEBUG_PRINTLN("Cell data frame detected.");
            parseData();
            break;
          case 0x03:
            DEBUG_PRINTLN("Device info frame detected.");
            parseDeviceInfo();
            break;
          default:
            DEBUG_PRINTF("Unknown frame type: 0x%02X\n", receivedBytes[4]);
            break;
        }

        break;  // Exit the loop after processing the complete frame
      }
    }
  }
  // Received data but no frame is started - potentially corrupted or out of sync
  else {
    DEBUG_PRINTLN("Received notification but no frame started - ignoring");
  }
}

/**
 * Write a register command to the BMS
 * Sends a command frame to modify BMS settings or request data
 * @param address Register address to write to
 * @param value 32-bit value to write (little-endian format)
 * @param length Length parameter for the command
 */
void JKBMS::writeRegister(uint8_t address, uint32_t value, uint8_t length) {
  DEBUG_PRINTF("Writing register: address=0x%02X, value=0x%08lX, length=%d\n", address, value, length);
  uint8_t frame[20] = { 0xAA, 0x55, 0x90, 0xEB, address, length };

  // Insert value (Little-Endian)
  frame[6] = value >> 0;  // LSB
  frame[7] = value >> 8;
  frame[8] = value >> 16;
  frame[9] = value >> 24;  // MSB

  // Calculate CRC
  frame[19] = crc(frame, 19);

  // Debug: Print the entire frame in hexadecimal format
  DEBUG_PRINTF("Frame to be sent: ");
  for (int i = 0; i < sizeof(frame); i++) {
    DEBUG_PRINTF("%02X ", frame[i]);
  }
  DEBUG_PRINTF("\n");

  if (pChr) {
    pChr->writeValue((uint8_t*)frame, (size_t)sizeof(frame));
  }
}

/**
 * Parse and process BMS settings data
 * Extracts protection thresholds, current limits, temperature limits, and other configuration parameters
 * from the received data frame and updates corresponding class member variables
 */
void JKBMS::bms_settings() {
  DEBUG_PRINTLN("Processing BMS settings...");
  cell_voltage_undervoltage_protection = ((receivedBytes[13] << 24 | receivedBytes[12] << 16 | receivedBytes[11] << 8 | receivedBytes[10]) * 0.001);
  cell_voltage_undervoltage_recovery = ((receivedBytes[17] << 24 | receivedBytes[16] << 16 | receivedBytes[15] << 8 | receivedBytes[14]) * 0.001);
  cell_voltage_overvoltage_protection = ((receivedBytes[21] << 24 | receivedBytes[20] << 16 | receivedBytes[19] << 8 | receivedBytes[18]) * 0.001);
  cell_voltage_overvoltage_recovery = ((receivedBytes[25] << 24 | receivedBytes[24] << 16 | receivedBytes[23] << 8 | receivedBytes[22]) * 0.001);
  balance_trigger_voltage = ((receivedBytes[29] << 24 | receivedBytes[28] << 16 | receivedBytes[27] << 8 | receivedBytes[26]) * 0.001);
  power_off_voltage = ((receivedBytes[49] << 24 | receivedBytes[48] << 16 | receivedBytes[47] << 8 | receivedBytes[46]) * 0.001);
  max_charge_current = ((receivedBytes[53] << 24 | receivedBytes[52] << 16 | receivedBytes[51] << 8 | receivedBytes[50]) * 0.001);
  charge_overcurrent_protection_delay = ((receivedBytes[57] << 24 | receivedBytes[56] << 16 | receivedBytes[55] << 8 | receivedBytes[54]));
  charge_overcurrent_protection_recovery_time = ((receivedBytes[61] << 24 | receivedBytes[60] << 16 | receivedBytes[59] << 8 | receivedBytes[58]));
  max_discharge_current = ((receivedBytes[65] << 24 | receivedBytes[64] << 16 | receivedBytes[63] << 8 | receivedBytes[62]) * 0.001);
  discharge_overcurrent_protection_delay = ((receivedBytes[69] << 24 | receivedBytes[68] << 16 | receivedBytes[67] << 8 | receivedBytes[66]));
  discharge_overcurrent_protection_recovery_time = ((receivedBytes[73] << 24 | receivedBytes[72] << 16 | receivedBytes[71] << 8 | receivedBytes[70]));
  short_circuit_protection_recovery_time = ((receivedBytes[77] << 24 | receivedBytes[76] << 16 | receivedBytes[75] << 8 | receivedBytes[74]));
  max_balance_current = ((receivedBytes[81] << 24 | receivedBytes[80] << 16 | receivedBytes[79] << 8 | receivedBytes[78]) * 0.001);
  charge_overtemperature_protection = ((receivedBytes[85] << 24 | receivedBytes[84] << 16 | receivedBytes[83] << 8 | receivedBytes[82]) * 0.1);
  charge_overtemperature_protection_recovery = ((receivedBytes[89] << 24 | receivedBytes[88] << 16 | receivedBytes[87] << 8 | receivedBytes[86]) * 0.1);
  discharge_overtemperature_protection = ((receivedBytes[93] << 24 | receivedBytes[92] << 16 | receivedBytes[91] << 8 | receivedBytes[90]) * 0.1);
  discharge_overtemperature_protection_recovery = ((receivedBytes[97] << 24 | receivedBytes[96] << 16 | receivedBytes[95] << 8 | receivedBytes[94]) * 0.1);
  charge_undertemperature_protection = ((receivedBytes[101] << 24 | receivedBytes[100] << 16 | receivedBytes[99] << 8 | receivedBytes[98]) * 0.1);
  charge_undertemperature_protection_recovery = ((receivedBytes[105] << 24 | receivedBytes[104] << 16 | receivedBytes[103] << 8 | receivedBytes[102]) * 0.1);
  power_tube_overtemperature_protection = ((receivedBytes[109] << 24 | receivedBytes[108] << 16 | receivedBytes[107] << 8 | receivedBytes[106]) * 0.1);
  power_tube_overtemperature_protection_recovery = ((receivedBytes[113] << 24 | receivedBytes[112] << 16 | receivedBytes[111] << 8 | receivedBytes[110]) * 0.1);
  cell_count = ((receivedBytes[117] << 24 | receivedBytes[116] << 16 | receivedBytes[115] << 8 | receivedBytes[114]));
  // 118   4   0x01 0x00 0x00 0x00    Charge switch
  // 122   4   0x01 0x00 0x00 0x00    Discharge switch
  // 126   4   0x01 0x00 0x00 0x00    Balancer switch
  total_battery_capacity = ((receivedBytes[133] << 24 | receivedBytes[132] << 16 | receivedBytes[131] << 8 | receivedBytes[130]) * 0.001);
  short_circuit_protection_delay = ((receivedBytes[137] << 24 | receivedBytes[136] << 16 | receivedBytes[135] << 8 | receivedBytes[134]) * 1);
  balance_starting_voltage = ((receivedBytes[141] << 24 | receivedBytes[140] << 16 | receivedBytes[139] << 8 | receivedBytes[138]) * 0.001);

  DEBUG_PRINTF("Cell voltage undervoltage protection: %.2fV\n", cell_voltage_undervoltage_protection);
  DEBUG_PRINTF("Cell voltage undervoltage recovery: %.2fV\n", cell_voltage_undervoltage_recovery);
  DEBUG_PRINTF("Cell voltage overvoltage protection: %.2fV\n", cell_voltage_overvoltage_protection);
  DEBUG_PRINTF("Cell voltage overvoltage recovery: %.2fV\n", cell_voltage_overvoltage_recovery);
  DEBUG_PRINTF("Balance trigger voltage: %.2fV\n", balance_trigger_voltage);
  DEBUG_PRINTF("Power off voltage: %.2fV\n", power_off_voltage);

  DEBUG_PRINTF("Max charge current: %.2fA\n", max_charge_current);
  DEBUG_PRINTF("Charge overcurrent protection delay: %.2fs\n", charge_overcurrent_protection_delay);
  DEBUG_PRINTF("Charge overcurrent protection recovery time: %.2fs\n", charge_overcurrent_protection_recovery_time);
  DEBUG_PRINTF("Max discharge current: %.2fA\n", max_discharge_current);
  DEBUG_PRINTF("Discharge overcurrent protection delay: %.2fs\n", discharge_overcurrent_protection_delay);
  DEBUG_PRINTF("Discharge overcurrent protection recovery time: %.2fs\n", discharge_overcurrent_protection_recovery_time);
  DEBUG_PRINTF("Short circuit protection recovery time: %.2fs\n", short_circuit_protection_recovery_time);
  DEBUG_PRINTF("Max balance current: %.2fA\n", max_balance_current);
  DEBUG_PRINTF("Charge overtemperature protection: %.2fC\n", charge_overtemperature_protection);
  DEBUG_PRINTF("Charge overtemperature protection recovery: %.2fC\n", charge_overtemperature_protection_recovery);
  DEBUG_PRINTF("Discharge overtemperature protection: %.2fC\n", discharge_overtemperature_protection);
  DEBUG_PRINTF("Discharge overtemperature protection recovery: %.2fC\n", discharge_overtemperature_protection_recovery);
  DEBUG_PRINTF("Charge undertemperature protection: %.2fC\n", charge_undertemperature_protection);
  DEBUG_PRINTF("Charge undertemperature protection recovery: %.2fC\n", charge_undertemperature_protection_recovery);
  DEBUG_PRINTF("Power tube overtemperature protection: %.2fC\n", power_tube_overtemperature_protection);
  DEBUG_PRINTF("Power tube overtemperature protection recovery: %.2fC\n", power_tube_overtemperature_protection_recovery);
  DEBUG_PRINTF("Cell count: %.d\n", cell_count);
  DEBUG_PRINTF("Total battery capacity: %.2fAh\n", total_battery_capacity);
  DEBUG_PRINTF("Short circuit protection delay: %.2fus\n", short_circuit_protection_delay);
  DEBUG_PRINTF("Balance starting voltage: %.2fV\n", balance_starting_voltage);
}

/**
 * Parse and extract device information from BMS
 * Processes device info frame to extract vendor ID, hardware/software versions,
 * device name, serial number, manufacturing date, and other device-specific data
 */
void JKBMS::parseDeviceInfo() {
  DEBUG_PRINTLN("Processing device info...");
  new_data = false;

  // Debugging: Print the raw data received
  DEBUG_PRINTLN("Raw data received:");
  for (int i = 0; i < frame; i++) {
    DEBUG_PRINTF("%02X ", receivedBytes[i]);
    if ((i + 1) % 16 == 0) DEBUG_PRINTLN("");  // New line after 16 bytes
  }
  DEBUG_PRINTLN("");

  // Check if enough data has been received
  if (frame < 134) {  // 134 bytes are required for device info
    DEBUG_PRINTLN("Error: Not enough data received for device info.");
    return;
  }

  // Extract device information from the received bytes
  std::string vendorID(receivedBytes + 6, receivedBytes + 6 + 16);
  std::string hardwareVersion(receivedBytes + 22, receivedBytes + 22 + 8);
  std::string softwareVersion(receivedBytes + 30, receivedBytes + 30 + 8);
  uint32_t uptime = (receivedBytes[41] << 24) | (receivedBytes[40] << 16) | (receivedBytes[39] << 8) | receivedBytes[38];
  uint32_t powerOnCount = (receivedBytes[45] << 24) | (receivedBytes[44] << 16) | (receivedBytes[43] << 8) | receivedBytes[42];
  std::string deviceName(receivedBytes + 46, receivedBytes + 46 + 16);
  std::string devicePasscode(receivedBytes + 62, receivedBytes + 62 + 16);
  std::string manufacturingDate(receivedBytes + 78, receivedBytes + 78 + 8);
  std::string serialNumber(receivedBytes + 86, receivedBytes + 86 + 11);
  std::string passcode(receivedBytes + 97, receivedBytes + 97 + 5);
  std::string userData(receivedBytes + 102, receivedBytes + 102 + 16);
  std::string setupPasscode(receivedBytes + 118, receivedBytes + 118 + 16);

  // Debugging: Print the parsed device information
  DEBUG_PRINTF("  Vendor ID: %s\n", vendorID.c_str());
  DEBUG_PRINTF("  Hardware version: %s\n", hardwareVersion.c_str());
  DEBUG_PRINTF("  Software version: %s\n", softwareVersion.c_str());
  DEBUG_PRINTF("  Uptime: %d s\n", uptime);
  DEBUG_PRINTF("  Power on count: %d\n", powerOnCount);
  DEBUG_PRINTF("  Device name: %s\n", deviceName.c_str());
  DEBUG_PRINTF("  Device passcode: %s\n", devicePasscode.c_str());
  DEBUG_PRINTF("  Manufacturing date: %s\n", manufacturingDate.c_str());
  DEBUG_PRINTF("  Serial number: %s\n", serialNumber.c_str());
  DEBUG_PRINTF("  Passcode: %s\n", passcode.c_str());
  DEBUG_PRINTF("  User data: %s\n", userData.c_str());
  DEBUG_PRINTF("  Setup passcode: %s\n", setupPasscode.c_str());
}

/**
 * Parse real-time BMS data
 * Processes cell data frame to extract cell voltages, wire resistances, battery voltage,
 * current, power, temperatures, capacity information, charge/discharge status, and balancing data
 */
void JKBMS::parseData() {
  DEBUG_PRINTLN("Parsing data...");
  new_data = false;
  ignoreNotifyCount = 10;
  // Cell voltages
  for (int j = 0, i = 7; i < 38; j++, i += 2) {
    cellVoltage[j] = ((receivedBytes[i] << 8 | receivedBytes[i - 1]) * 0.001);
  }

  Average_Cell_Voltage = (((int)receivedBytes[75] << 8 | receivedBytes[74]) * 0.001);

  Delta_Cell_Voltage = (((int)receivedBytes[77] << 8 | receivedBytes[76]) * 0.001);

  for (int j = 0, i = 81; i < 112; j++, i += 2) {
    wireResist[j] = (((int)receivedBytes[i] << 8 | receivedBytes[i - 1]) * 0.001);
  }

  if (receivedBytes[145] == 0xFF) {
    MOS_Temp = ((0xFF << 24 | 0xFF << 16 | receivedBytes[145] << 8 | receivedBytes[144]) * 0.1);
  } else {
    MOS_Temp = ((receivedBytes[145] << 8 | receivedBytes[144]) * 0.1);
  }

  // Battery voltage
  Battery_Voltage = ((receivedBytes[153] << 24 | receivedBytes[152] << 16 | receivedBytes[151] << 8 | receivedBytes[150]) * 0.001);

  Charge_Current = ((receivedBytes[161] << 24 | receivedBytes[160] << 16 | receivedBytes[159] << 8 | receivedBytes[158]) * 0.001);

  Battery_Power = Battery_Voltage * Charge_Current;

  if (receivedBytes[163] == 0xFF) {
    Battery_T1 = ((0xFF << 24 | 0xFF << 16 | receivedBytes[163] << 8 | receivedBytes[162]) * 0.1);
  } else {
    Battery_T1 = ((receivedBytes[163] << 8 | receivedBytes[162]) * 0.1);
  }

  if (receivedBytes[165] == 0xFF) {
    Battery_T2 = ((0xFF << 24 | 0xFF << 16 | receivedBytes[165] << 8 | receivedBytes[164]) * 0.1);
  } else {
    Battery_T2 = ((receivedBytes[165] << 8 | receivedBytes[164]) * 0.1);
  }

  if ((receivedBytes[171] & 0xF0) == 0x0) {
    Balance_Curr = ((receivedBytes[171] << 8 | receivedBytes[170]) * 0.001);
  } else if ((receivedBytes[171] & 0xF0) == 0xF0) {
    Balance_Curr = (((receivedBytes[171] & 0x0F) << 8 | receivedBytes[170]) * -0.001);
  }

  Balancing_Action = receivedBytes[172];
  Percent_Remain = (receivedBytes[173]);
  Capacity_Remain = ((receivedBytes[177] << 24 | receivedBytes[176] << 16 | receivedBytes[175] << 8 | receivedBytes[174]) * 0.001);
  Nominal_Capacity = ((receivedBytes[181] << 24 | receivedBytes[180] << 16 | receivedBytes[179] << 8 | receivedBytes[178]) * 0.001);
  Cycle_Count = ((receivedBytes[185] << 24 | receivedBytes[184] << 16 | receivedBytes[183] << 8 | receivedBytes[182]));
  Cycle_Capacity = ((receivedBytes[189] << 24 | receivedBytes[188] << 16 | receivedBytes[187] << 8 | receivedBytes[186]) * 0.001);

  Uptime = receivedBytes[196] << 16 | receivedBytes[195] << 8 | receivedBytes[194];
  sec = Uptime % 60;
  Uptime /= 60;
  mi = Uptime % 60;
  Uptime /= 60;
  hr = Uptime % 24;
  days = Uptime / 24;

  if (receivedBytes[198] > 0) {
    Charge = true;
  } else if (receivedBytes[198] == 0) {
    Charge = false;
  }
  if (receivedBytes[199] > 0) {
    Discharge = true;
  } else if (receivedBytes[199] == 0) {
    Discharge = false;
  }
  if (receivedBytes[201] > 0) {
    Balance = true;
  } else if (receivedBytes[201] == 0) {
    Balance = false;
  }

  // Output values
  DEBUG_PRINTF("\n--- Data from %s ---\n", targetMAC.c_str());
  DEBUG_PRINTLN("Cell Voltages:");
  for (int j = 0; j < 16; j++) {
    DEBUG_PRINTF("  Cell %02d: %.3f V\n", j + 1, cellVoltage[j]);
  }
  DEBUG_PRINTLN("wire Resist:");
  for (int j = 0; j < 16; j++) {
    DEBUG_PRINTF("  Cell %02d: %.3f Ohm\n", j + 1, wireResist[j]);
  }
  DEBUG_PRINTF("Average Cell Voltage: %.2fV\n", Average_Cell_Voltage);
  DEBUG_PRINTF("Delta Cell Voltage: %.2fV\n", Delta_Cell_Voltage);
  DEBUG_PRINTF("Balance Curr: %.2fA\n", Balance_Curr);
  DEBUG_PRINTF("Battery Voltage: %.2fV\n", Battery_Voltage);
  DEBUG_PRINTF("Battery Power: %.2fW\n", Battery_Power);
  DEBUG_PRINTF("Charge Current: %.2fA\n", Charge_Current);
  DEBUG_PRINTF("Charge: %d%%\n", Percent_Remain);
  DEBUG_PRINTF("Capacity Remain: %.2fAh\n", Capacity_Remain);
  DEBUG_PRINTF("Nominal Capacity: %.2fAh\n", Nominal_Capacity);
  DEBUG_PRINTF("Cycle Count: %.2f\n", Cycle_Count);
  DEBUG_PRINTF("Cycle Capacity: %.2fAh\n", Cycle_Capacity);
  DEBUG_PRINTF("Temperature T1: %.1fC\n", Battery_T1);
  DEBUG_PRINTF("Temperature T2: %.1fC\n", Battery_T2);
  DEBUG_PRINTF("Temperature MOS: %.1fC\n", MOS_Temp);
  DEBUG_PRINTF("Uptime: %dd %dh %dm\n", days, hr, mi);
  DEBUG_PRINTF("Charge: %d\n", Charge);
  DEBUG_PRINTF("Discharge: %d\n", Discharge);
  DEBUG_PRINTF("Balance: %d\n", Balance);
  DEBUG_PRINTF("Balancing Action: %d\n", Balancing_Action);
}

/**
 * Calculate CRC checksum for data frame
 * Computes simple sum-based checksum for data integrity verification
 * @param data Pointer to the data array
 * @param len Length of the data array
 * @return Calculated CRC checksum value
 */
uint8_t JKBMS::crc(const uint8_t data[], uint16_t len) {
  uint8_t crc = 0;
  for (uint16_t i = 0; i < len; i++) crc += data[i];
  return crc;
}

//********************************************
// Callback Classes Implementation
//********************************************

/**
 * Constructor for ClientCallbacks class
 * Associates the callback instance with a specific JKBMS instance
 * @param bmsInstance Pointer to the JKBMS instance this callback will handle
 */
ClientCallbacks::ClientCallbacks(JKBMS* bmsInstance) : bms(bmsInstance) {}

/**
 * BLE client connection established callback
 * Called when the BLE client successfully connects to the BMS device
 * @param pClient Pointer to the connected BLE client
 */
void ClientCallbacks::onConnect(NimBLEClient* pClient) {
  DEBUG_PRINTF("Connected to %s\n", bms->targetMAC.c_str());
  bms->connected = true;
}

/**
 * BLE client disconnection callback
 * Called when the BLE client disconnects from the BMS device
 * @param pClient Pointer to the disconnected BLE client
 * @param reason Reason code for the disconnection
 */
void ClientCallbacks::onDisconnect(NimBLEClient* pClient, int reason) {
  DEBUG_PRINTF("%s disconnected, reason: %d\n", bms->targetMAC.c_str(), reason);
  bms->connected = false;
  bms->doConnect = false;
}

/**
 * BLE scan result callback
 * Called when a BLE device is discovered during scanning
 * Checks if the discovered device matches any target BMS MAC address
 * @param advertisedDevice Pointer to the discovered BLE device
 */
void ScanCallbacks::onResult(const NimBLEAdvertisedDevice* advertisedDevice) {
  DEBUG_PRINTF("BLE Device found: %s\n", advertisedDevice->toString().c_str());
  for (int i = 0; i < bmsDeviceCount; i++) {
    if (jkBmsDevices[i].targetMAC.empty()) continue;  // Skip empty MAC addresses
    if (advertisedDevice->getAddress().toString() == jkBmsDevices[i].targetMAC && !jkBmsDevices[i].connected && !jkBmsDevices[i].doConnect) {
      jkBmsDevices[i].advDevice = advertisedDevice;
      jkBmsDevices[i].doConnect = true;
      DEBUG_PRINTF("Found target device: %s\n", jkBmsDevices[i].targetMAC.c_str());
    }
  }
}

//********************************************
// Global Callback Function
//********************************************

/**
 * Global BLE notification callback function
 * Routes incoming notifications to the appropriate JKBMS instance
 * @param pChr Pointer to the characteristic that sent the notification
 * @param pData Pointer to the received data
 * @param length Length of the received data
 * @param isNotify Boolean indicating if this is a notification (vs indication)
 */
void notifyCB(NimBLERemoteCharacteristic* pChr, uint8_t* pData, size_t length, bool isNotify) {
  DEBUG_PRINTLN("Notification received...");
  for (int i = 0; i < bmsDeviceCount; i++) {
    if (jkBmsDevices[i].pChr == pChr) {
      jkBmsDevices[i].handleNotification(pData, length);
      break;
    }
  }
}
