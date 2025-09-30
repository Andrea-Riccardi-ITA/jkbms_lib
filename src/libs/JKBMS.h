#ifndef JKBMS_H
#define JKBMS_H

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <string>

// Forward declarations
class NimBLERemoteCharacteristic;
class NimBLEAdvertisedDevice;

#define DEBUG_ENABLED false

// Debug output function type
typedef void (*DebugPrintFunc)(const char* format, ...);
typedef void (*DebugPrintlnFunc)(const char* message);
typedef void (*DebugPrintSimpleFunc)(const char* message);

// External debug functions that will be set by main.cpp
extern DebugPrintFunc debugPrintFunc;
extern DebugPrintlnFunc debugPrintlnFunc;
extern DebugPrintSimpleFunc debugPrintSimpleFunc;

// Debug macros
#if DEBUG_ENABLED
#define DEBUG_PRINTF(...) if(debugPrintFunc) debugPrintFunc(__VA_ARGS__)
#define DEBUG_PRINTLN(msg) if(debugPrintlnFunc) debugPrintlnFunc(msg)
#define DEBUG_PRINT(msg) if(debugPrintSimpleFunc) debugPrintSimpleFunc(msg)
#else
#define DEBUG_PRINTF(...)
#define DEBUG_PRINTLN(msg)
#define DEBUG_PRINT(msg)
#endif

class JKBMS {
public:
  JKBMS(const std::string& mac);

  // BLE Components
  NimBLERemoteCharacteristic* pChr = nullptr;
  const NimBLEAdvertisedDevice* advDevice = nullptr;
  bool doConnect = false;
  bool connected = false;
  uint32_t lastNotifyTime = 0;
  std::string targetMAC;

  // Data Processing
  byte receivedBytes[320];
  int frame = 0;
  bool received_start = false;
  bool received_complete = false;
  bool new_data = false;
  int ignoreNotifyCount = 0;

  // BMS Data Fields
  float cellVoltage[16] = { 0 };
  float wireResist[16] = { 0 };
  float Average_Cell_Voltage = 0;
  float Delta_Cell_Voltage = 0;
  float Battery_Voltage = 0;
  float Battery_Power = 0;
  float Charge_Current = 0;
  float Battery_T1 = 0;
  float Battery_T2 = 0;
  float MOS_Temp = 0;
  int Percent_Remain = 0;
  float Capacity_Remain = 0;
  float Nominal_Capacity = 0;
  float Cycle_Count = 0;
  float Cycle_Capacity = 0;
  uint32_t Uptime;
  uint8_t sec, mi, hr, days;
  float Balance_Curr = 0;
  bool Balance = false;
  bool Charge = false;
  bool Discharge = false;
  int Balancing_Action = 0;

  // BMS Settings
  float balance_trigger_voltage = 0;
  float cell_voltage_undervoltage_protection = 0;
  float cell_voltage_undervoltage_recovery = 0;
  float cell_voltage_overvoltage_protection = 0;
  float cell_voltage_overvoltage_recovery = 0;
  float power_off_voltage = 0;
  float max_charge_current = 0;
  float charge_overcurrent_protection_delay = 0;
  float charge_overcurrent_protection_recovery_time = 0;
  float max_discharge_current = 0;
  float discharge_overcurrent_protection_delay = 0;
  float discharge_overcurrent_protection_recovery_time = 0;
  float short_circuit_protection_recovery_time = 0;
  float max_balance_current = 0;
  float charge_overtemperature_protection = 0;
  float charge_overtemperature_protection_recovery = 0;
  float discharge_overtemperature_protection = 0;
  float discharge_overtemperature_protection_recovery = 0;
  float charge_undertemperature_protection = 0;
  float charge_undertemperature_protection_recovery = 0;
  float power_tube_overtemperature_protection = 0;
  float power_tube_overtemperature_protection_recovery = 0;
  int cell_count = 0;
  float total_battery_capacity = 0;
  float short_circuit_protection_delay = 0;
  float balance_starting_voltage = 0;

  // Methods
  bool connectToServer();
  void parseDeviceInfo();
  void parseData();
  void bms_settings();
  void writeRegister(uint8_t address, uint32_t value, uint8_t length);
  void handleNotification(uint8_t* pData, size_t length);
  void enableBMSFunctions();

private:
  uint8_t crc(const uint8_t data[], uint16_t len);
};

// Callback classes
class ClientCallbacks : public NimBLEClientCallbacks {
  JKBMS* bms;
public:
  ClientCallbacks(JKBMS* bmsInstance);
  void onConnect(NimBLEClient* pClient) override;
  void onDisconnect(NimBLEClient* pClient, int reason) override;
};

class ScanCallbacks : public NimBLEScanCallbacks {
public:
  void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override;
};

// Global callback function
void notifyCB(NimBLERemoteCharacteristic* pChr, uint8_t* pData, size_t length, bool isNotify);

// External variables that need to be accessible
extern JKBMS jkBmsDevices[];
extern const int bmsDeviceCount;

#endif // JKBMS_H
