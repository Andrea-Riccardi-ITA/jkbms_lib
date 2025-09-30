# Documentazione Libreria JKBMS

## Panoramica

La libreria JKBMS fornisce un'interfaccia completa per la comunicazione con i sistemi di gestione batteria (BMS) JKBMS tramite Bluetooth Low Energy (BLE). Questa libreria permette il monitoraggio in tempo reale di batterie LiFePO4 e Li-ion, la configurazione dei parametri di protezione e il controllo delle funzioni di carica/scarica.

## Indice

1. [Architettura del Sistema](#architettura-del-sistema)
2. [Protocollo di Comunicazione](#protocollo-di-comunicazione)
3. [Struttura dei Messaggi](#struttura-dei-messaggi)
4. [Parsing dei Dati](#parsing-dei-dati)
5. [Funzione CRC](#funzione-crc)
6. [API della Libreria](#api-della-libreria)
7. [Configurazione e Utilizzo](#configurazione-e-utilizzo)
8. [Gestione degli Errori](#gestione-degli-errori)
9. [Esempi di Utilizzo](#esempi-di-utilizzo)
10. [Troubleshooting](#troubleshooting)

---

## Architettura del Sistema

### Componenti Principali

- **JKBMS Class**: Classe principale che gestisce la comunicazione BLE e il parsing dei dati
- **ClientCallbacks**: Gestisce gli eventi di connessione/disconnessione BLE
- **ScanCallbacks**: Gestisce la scoperta di dispositivi BMS durante la scansione
- **Debug System**: Sistema modulare per il debug con livelli configurabili

### Flusso di Comunicazione

```txt  
ESP32 ←→ BLE ←→ JKBMS Device
   ↓
Data Processing
   ↓
Application Logic
```

---

## Protocollo di Comunicazione

### Connessione BLE

La libreria utilizza le seguenti specifiche BLE per la comunicazione:

- **Service UUID**: `ffe0`
- **Characteristic UUID**: `ffe1`
- **MTU Size**: 517 bytes (massimo per ESP32)
- **Connection Parameters**:
  - Interval: 24 * 1.25ms = 30ms
  - Latency: 0
  - Timeout: 400 * 10ms = 4s

### Parametri di Scansione

- **Scan Interval**: 1600 * 0.625ms = 1000ms
- **Scan Window**: 100 * 0.625ms = 62.5ms
- **Active Scan**: Abilitata per ricevere scan response

---

## Struttura dei Messaggi

### Frame Header (Messaggi dal BMS all'ESP32)

Tutti i messaggi ricevuti dal BMS iniziano con un header standard:

```txt
Byte 0-1: Start Sequence [0x55, 0xAA]
Byte 2-3: Command [0xEB, 0x90]
Byte 4:   Frame Type
Byte 5+:  Data Payload
```

### Tipi di Frame

| Frame Type | Descrizione | Lunghezza |
|------------|-------------|-----------|
| `0x01` | BMS Settings | 300 bytes |
| `0x02` | Cell Data | 300 bytes |
| `0x03` | Device Info | 300 bytes |

### Comando di Scrittura (ESP32 → BMS)

Struttura per i comandi inviati al BMS:

```txt
Byte 0-1:   Header [0xAA, 0x55]
Byte 2-3:   Command [0x90, 0xEB]
Byte 4:     Register Address
Byte 5:     Data Length
Byte 6-9:   Value (Little-Endian, 32-bit)
Byte 10-18: Padding (0x00)
Byte 19:    CRC Checksum
```

NB: I byte [0 -> 4] scambiati volutamente

---

## Parsing dei Dati (Messaggi BMS to ESP32)

### 1. Frame Type = 0x02 - Dati delle Celle

#### Tensioni delle Celle (Byte 6-37)

- NB: Di questi byte alcuni potrebbero avere il valore "0" poiché il bms supporta un numero di celle da 8 a 20

```cpp
// Ogni cella occupa 2 bytes (Little-Endian)
for (int j = 0, i = 7; i < 38; j++, i += 2) {
    cellVoltage[j] = ((receivedBytes[i] << 8 | receivedBytes[i - 1]) * 0.001);
}
```

**Formato dati**:

- Posizione: Byte 6-37 (32 bytes)
- Risoluzione: 1mV (moltiplicatore 0.001)
- Range: 0.000V - 65.535V per cella

#### Tensione Media delle Celle (Byte 74-75)

```cpp
Average_Cell_Voltage = (((int)receivedBytes[75] << 8 | receivedBytes[74]) * 0.001);
```

#### Delta Tensione (Byte 76-77)

```cpp
Delta_Cell_Voltage = (((int)receivedBytes[77] << 8 | receivedBytes[76]) * 0.001);
```

#### Resistenze dei Fili (Byte 80-111)

```cpp
for (int j = 0, i = 81; i < 112; j++, i += 2) {
    wireResist[j] = (((int)receivedBytes[i] << 8 | receivedBytes[i - 1]) * 0.001);
}
```

#### Temperatura MOS (Byte 144-145)

```cpp
if (receivedBytes[145] == 0xFF) {
    // Temperatura negativa (complemento a due)
    MOS_Temp = ((0xFF << 24 | 0xFF << 16 | receivedBytes[145] << 8 | receivedBytes[144]) * 0.1);
} else {
    // Temperatura positiva
    MOS_Temp = ((receivedBytes[145] << 8 | receivedBytes[144]) * 0.1);
}
```

#### Tensione Batteria (Byte 150-153)

```cpp
Battery_Voltage = ((receivedBytes[153] << 24 | receivedBytes[152] << 16 | 
                   receivedBytes[151] << 8 | receivedBytes[150]) * 0.001);
```

#### Corrente di Carica (Byte 158-161)

```cpp
Charge_Current = ((receivedBytes[161] << 24 | receivedBytes[160] << 16 | 
                  receivedBytes[159] << 8 | receivedBytes[158]) * 0.001);
```

#### Temperature Batteria T1 e T2 (Byte 162-165)

```cpp
// Temperatura T1 (con gestione numeri negativi)
if (receivedBytes[163] == 0xFF) {
    Battery_T1 = ((0xFF << 24 | 0xFF << 16 | receivedBytes[163] << 8 | receivedBytes[162]) * 0.1);
} else {
    Battery_T1 = ((receivedBytes[163] << 8 | receivedBytes[162]) * 0.1);
}

// Temperatura T2 (stesso metodo)
if (receivedBytes[165] == 0xFF) {
    Battery_T2 = ((0xFF << 24 | 0xFF << 16 | receivedBytes[165] << 8 | receivedBytes[164]) * 0.1);
} else {
    Battery_T2 = ((receivedBytes[165] << 8 | receivedBytes[164]) * 0.1);
}
```

#### Corrente di Bilanciamento (Byte 170-171)

```cpp
if ((receivedBytes[171] & 0xF0) == 0x0) {
    // Corrente positiva
    Balance_Curr = ((receivedBytes[171] << 8 | receivedBytes[170]) * 0.001);
} else if ((receivedBytes[171] & 0xF0) == 0xF0) {
    // Corrente negativa
    Balance_Curr = (((receivedBytes[171] & 0x0F) << 8 | receivedBytes[170]) * -0.001);
}
```

#### Stato di Carica e Capacità (Byte 172-189)

```cpp
Balancing_Action = receivedBytes[172];           // Azione di bilanciamento
Percent_Remain = receivedBytes[173];             // Percentuale rimanente
Capacity_Remain = ((receivedBytes[177] << 24 | receivedBytes[176] << 16 | 
                   receivedBytes[175] << 8 | receivedBytes[174]) * 0.001);  // Capacità rimanente
Nominal_Capacity = ((receivedBytes[181] << 24 | receivedBytes[180] << 16 | 
                    receivedBytes[179] << 8 | receivedBytes[178]) * 0.001); // Capacità nominale
Cycle_Count = ((receivedBytes[185] << 24 | receivedBytes[184] << 16 | 
               receivedBytes[183] << 8 | receivedBytes[182]));              // Numero di cicli
Cycle_Capacity = ((receivedBytes[189] << 24 | receivedBytes[188] << 16 | 
                  receivedBytes[187] << 8 | receivedBytes[186]) * 0.001);   // Capacità per ciclo
```

#### Uptime (Byte 194-196)

```cpp
Uptime = receivedBytes[196] << 16 | receivedBytes[195] << 8 | receivedBytes[194];
sec = Uptime % 60;
Uptime /= 60;
mi = Uptime % 60;
Uptime /= 60;
hr = Uptime % 24;
days = Uptime / 24;
```

#### Stati di Controllo (Byte 198-201)

```cpp
Charge = (receivedBytes[198] > 0);      // Stato carica
Discharge = (receivedBytes[199] > 0);   // Stato scarica
Balance = (receivedBytes[201] > 0);     // Stato bilanciamento
```

### 2. Frame Tipo 0x01 - Impostazioni BMS

#### Protezioni Tensione (Byte 10-29)

```cpp
cell_voltage_undervoltage_protection = ((receivedBytes[13] << 24 | receivedBytes[12] << 16 | 
                                        receivedBytes[11] << 8 | receivedBytes[10]) * 0.001);
cell_voltage_undervoltage_recovery = ((receivedBytes[17] << 24 | receivedBytes[16] << 16 | 
                                      receivedBytes[15] << 8 | receivedBytes[14]) * 0.001);
cell_voltage_overvoltage_protection = ((receivedBytes[21] << 24 | receivedBytes[20] << 16 | 
                                       receivedBytes[19] << 8 | receivedBytes[18]) * 0.001);
cell_voltage_overvoltage_recovery = ((receivedBytes[25] << 24 | receivedBytes[24] << 16 | 
                                     receivedBytes[23] << 8 | receivedBytes[22]) * 0.001);
balance_trigger_voltage = ((receivedBytes[29] << 24 | receivedBytes[28] << 16 | 
                           receivedBytes[27] << 8 | receivedBytes[26]) * 0.001);
```

#### Limiti di Corrente (Byte 50-81)

```cpp
max_charge_current = ((receivedBytes[53] << 24 | receivedBytes[52] << 16 | 
                      receivedBytes[51] << 8 | receivedBytes[50]) * 0.001);
charge_overcurrent_protection_delay = ((receivedBytes[57] << 24 | receivedBytes[56] << 16 | 
                                       receivedBytes[55] << 8 | receivedBytes[54]));
max_discharge_current = ((receivedBytes[65] << 24 | receivedBytes[64] << 16 | 
                         receivedBytes[63] << 8 | receivedBytes[62]) * 0.001);
max_balance_current = ((receivedBytes[81] << 24 | receivedBytes[80] << 16 | 
                       receivedBytes[79] << 8 | receivedBytes[78]) * 0.001);
```

#### Protezioni Temperatura (Byte 82-113)

```cpp
charge_overtemperature_protection = ((receivedBytes[85] << 24 | receivedBytes[84] << 16 | 
                                     receivedBytes[83] << 8 | receivedBytes[82]) * 0.1);
charge_undertemperature_protection = ((receivedBytes[101] << 24 | receivedBytes[100] << 16 | 
                                      receivedBytes[99] << 8 | receivedBytes[98]) * 0.1);
power_tube_overtemperature_protection = ((receivedBytes[109] << 24 | receivedBytes[108] << 16 | 
                                         receivedBytes[107] << 8 | receivedBytes[106]) * 0.1);
```

### 3. Frame Tipo 0x03 - Informazioni Dispositivo

#### Parsing Informazioni Base

```cpp
std::string vendorID(receivedBytes + 6, receivedBytes + 6 + 16);        // Byte 6-21
std::string hardwareVersion(receivedBytes + 22, receivedBytes + 22 + 8); // Byte 22-29
std::string softwareVersion(receivedBytes + 30, receivedBytes + 30 + 8); // Byte 30-37
uint32_t uptime = (receivedBytes[41] << 24) | (receivedBytes[40] << 16) | 
                  (receivedBytes[39] << 8) | receivedBytes[38];           // Byte 38-41
std::string deviceName(receivedBytes + 46, receivedBytes + 46 + 16);     // Byte 46-61
std::string serialNumber(receivedBytes + 86, receivedBytes + 86 + 11);   // Byte 86-96
```

---

## Funzione CRC

### Algoritmo di Checksum

La funzione `crc()` implementa un semplice algoritmo di checksum a somma:

```cpp
uint8_t JKBMS::crc(const uint8_t data[], uint16_t len) {
    uint8_t crc = 0;
    for (uint16_t i = 0; i < len; i++) {
        crc += data[i];
    }
    return crc;
}
```

### Come Funziona

1. **Inizializzazione**: Il CRC viene inizializzato a 0
2. **Accumulo**: Ogni byte del frame viene sommato al valore CRC
3. **Overflow**: Il risultato viene automaticamente troncato a 8-bit tramite overflow naturale di `uint8_t`
4. **Risultato**: Il valore finale rappresenta il checksum del frame

### Utilizzo nel Protocollo

**Per comandi inviati (ESP32 → BMS)**:

```cpp
uint8_t frame[20] = { 0xAA, 0x55, 0x90, 0xEB, address, length, ...};
// ... popola i dati ...
frame[19] = crc(frame, 19);  // CRC calcolato sui primi 19 bytes
```

**Esempio pratico**:

```txt
Frame: [0xAA, 0x55, 0x90, 0xEB, 0x97, 0x00, 0x00, 0x00, 0x00, 0x00, ...]
CRC = (0xAA + 0x55 + 0x90 + 0xEB + 0x97 + 0x00 + ... ) & 0xFF
```

### Verifica Integrità

Il BMS utilizza lo stesso algoritmo per verificare l'integrità dei comandi ricevuti. Se il CRC non corrisponde, il comando viene ignorato.

---

## API della Libreria

### Costruttore

```cpp
JKBMS(const std::string& mac);
```

- **Parametro**: `mac` - Indirizzo MAC del dispositivo BMS (formato: "xx:xx:xx:xx:xx:xx")
- **Descrizione**: Inizializza una nuova istanza per comunicare con un dispositivo BMS specifico

### Metodi Principali

#### Connessione e Comunicazione

```cpp
bool connectToServer();
```

- **Ritorno**: `true` se la connessione è riuscita, `false` altrimenti
- **Descrizione**: Stabilisce connessione BLE e configura notifiche

```cpp
void writeRegister(uint8_t address, uint32_t value, uint8_t length);
```

- **Parametri**:
  - `address`: Indirizzo del registro
  - `value`: Valore da scrivere (32-bit, little-endian)
  - `length`: Lunghezza del parametro
- **Descrizione**: Invia comando di scrittura al BMS

#### Processing Dati

```cpp
void handleNotification(uint8_t* pData, size_t length);
```

- **Parametri**:
  - `pData`: Buffer dati ricevuti
  - `length`: Lunghezza dati
- **Descrizione**: Gestisce notifiche BLE in arrivo

```cpp
void parseData();
void parseDeviceInfo();
void bms_settings();
```

- **Descrizione**: Parsano rispettivamente dati celle, info dispositivo e impostazioni BMS

#### Controllo BMS

```cpp
void enableBMSFunctions();
```

- **Descrizione**: Abilita funzioni di carica, scarica e bilanciamento

### Dati Accessibili

#### Tensioni e Correnti

```cpp
float cellVoltage[16];        // Tensioni celle (V)
float wireResist[16];         // Resistenze fili (Ω)
float Average_Cell_Voltage;   // Tensione media celle (V)
float Delta_Cell_Voltage;     // Delta tensione (V)
float Battery_Voltage;        // Tensione batteria (V)
float Battery_Power;          // Potenza batteria (W)
float Charge_Current;         // Corrente carica (A)
float Balance_Curr;           // Corrente bilanciamento (A)
```

#### Temperature

```cpp
float Battery_T1;             // Temperatura sensore 1 (°C)
float Battery_T2;             // Temperatura sensore 2 (°C)
float MOS_Temp;               // Temperatura MOSFET (°C)
```

#### Capacità e Stato

```cpp
int Percent_Remain;           // Percentuale carica rimanente (%)
float Capacity_Remain;        // Capacità rimanente (Ah)
float Nominal_Capacity;       // Capacità nominale (Ah)
float Cycle_Count;            // Numero cicli
float Cycle_Capacity;         // Capacità per ciclo (Ah)
```

#### Stati di Controllo

```cpp
bool Balance;                 // Stato bilanciamento
bool Charge;                  // Stato carica
bool Discharge;               // Stato scarica
int Balancing_Action;         // Azione bilanciamento in corso
```

#### Protezioni e Limiti

```cpp
float balance_trigger_voltage;                   // Tensione trigger bilanciamento (V)
float cell_voltage_undervoltage_protection;      // Protezione sottotensione (V)
float cell_voltage_overvoltage_protection;       // Protezione sovratensione (V)
float max_charge_current;                        // Corrente carica massima (A)
float max_discharge_current;                     // Corrente scarica massima (A)
float charge_overtemperature_protection;         // Protezione sovratemperatura carica (°C)
float discharge_overtemperature_protection;      // Protezione sovratemperatura scarica (°C)
// ... altri parametri di protezione
```

---

## Configurazione e Utilizzo

### Setup Base

```cpp
#include "libs/JKBMS.h"

// Configurazione dispositivi BMS
JKBMS jkBmsDevices[] = {
    JKBMS("c8:47:80:31:9b:02"),  // MAC address del primo BMS
    JKBMS("aa:bb:cc:dd:ee:ff"),  // MAC address del secondo BMS (opzionale)
};

const int bmsDeviceCount = sizeof(jkBmsDevices) / sizeof(jkBmsDevices[0]);
```

### Inizializzazione BLE

```cpp
void setup() {
    Serial.begin(115200);
    
    // Inizializza NimBLE
    NimBLEDevice::init("Photon test");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    NimBLEDevice::setMTU(517);
    
    // Configura scansione
    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->setScanCallbacks(&scanCallbacks);
    pScan->setInterval(1600);
    pScan->setWindow(100);
    pScan->setActiveScan(true);
}
```

### Loop Principale

```cpp
void loop() {
    // Gestione connessioni
    for (int i = 0; i < bmsDeviceCount; i++) {
        if (jkBmsDevices[i].doConnect && !jkBmsDevices[i].connected) {
            if (jkBmsDevices[i].connectToServer()) {
                Serial.printf("BMS %s connesso\n", jkBmsDevices[i].targetMAC.c_str());
            }
            jkBmsDevices[i].doConnect = false;
        }
        
        // Verifica timeout connessione
        if (jkBmsDevices[i].connected) {
            if (millis() - jkBmsDevices[i].lastNotifyTime > 25000) {
                Serial.printf("Timeout BMS %s\n", jkBmsDevices[i].targetMAC.c_str());
                // Gestione disconnessione...
            }
        }
    }
    
    // Avvia scansione se necessario
    if (connectedCount < bmsDeviceCount && shouldScan) {
        pScan->start(3000, false, true);
    }
    
    delay(100);
}
```

### Comandi Utili

#### Richiesta Dati

```cpp
writeRegister(0x96, 0x00000000, 0x00);  // Richiedi dati celle
writeRegister(0x97, 0x00000000, 0x00);  // Richiedi info dispositivo
writeRegister(0x01, 0x00000000, 0x00);  // Richiedi impostazioni
```

#### Controllo Funzioni

```cpp
writeRegister(0x1D, 0x00000001, 0x04);  // Abilita carica
writeRegister(0x1E, 0x00000001, 0x04);  // Abilita scarica
writeRegister(0x1F, 0x00000001, 0x04);  // Abilita bilanciamento

writeRegister(0x1D, 0x00000000, 0x04);  // Disabilita carica
writeRegister(0x1E, 0x00000000, 0x04);  // Disabilita scarica
writeRegister(0x1F, 0x00000000, 0x04);  // Disabilita bilanciamento
```

---

## Gestione degli Errori

### Errori di Connessione

#### Timeout di Connessione

```cpp
// La connessione fallisce dopo 10 secondi
pClient->setConnectTimeout(10000);

// Retry automatico con backoff progressivo
int retryCount = 0;
const int maxRetries = 3;
while (retryCount < maxRetries) {
    if (pClient->connect(advDevice)) break;
    delay(2000 + (1000 * retryCount));  // 3s, 4s, 5s
    retryCount++;
}
```

### Errori di Comunicazione

#### Timeout Notifiche

```cpp
// Disconnessione automatica se nessun dato per 25 secondi
if (millis() - lastNotifyTime > 25000) {
    DEBUG_PRINTF("Timeout comunicazione\n");
    pClient->disconnect();
    connected = false;
}
```

#### Frame Corrotti

```cpp
// Verifica header del frame
if (pData[0] == 0x55 && pData[1] == 0xAA && 
    pData[2] == 0xEB && pData[3] == 0x90) {
    // Frame valido
} else if (!received_start) {
    DEBUG_PRINTLN("Frame corrotto - ignorato");
    return;
}
```

### Sistema di Debug

#### Livelli di Debug

```cpp
#define DEBUG_ENABLED false  // Disabilita/abilita debug globalmente

#if DEBUG_ENABLED
#define DEBUG_PRINTF(...) if(debugPrintFunc) debugPrintFunc(__VA_ARGS__)
#define DEBUG_PRINTLN(msg) if(debugPrintlnFunc) debugPrintlnFunc(msg)
#else
#define DEBUG_PRINTF(...)
#define DEBUG_PRINTLN(msg)
#endif
```

#### Funzioni di Debug Personalizzabili

```cpp
// main.cpp
void debugPrintForJKBMS(const char* format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    Serial.print(buffer);
}

// Assegnazione delle funzioni
debugPrintFunc = debugPrintForJKBMS;
```

---

## Esempi di Utilizzo

### Esempio 1: Monitoraggio Base

```cpp
void printBMSData(JKBMS& bms) {
    if (bms.connected && bms.new_data) {
        Serial.printf("=== BMS %s ===\n", bms.targetMAC.c_str());
        Serial.printf("Tensione Batteria: %.2fV\n", bms.Battery_Voltage);
        Serial.printf("Corrente: %.2fA\n", bms.Charge_Current);
        Serial.printf("Potenza: %.2fW\n", bms.Battery_Power);
        Serial.printf("SOC: %d%%\n", bms.Percent_Remain);
        Serial.printf("Temperatura: %.1f°C\n", bms.Battery_T1);
        
        Serial.println("Tensioni Celle:");
        for (int i = 0; i < 16; i++) {
            if (bms.cellVoltage[i] > 0) {
                Serial.printf("  Cella %02d: %.3fV\n", i+1, bms.cellVoltage[i]);
            }
        }
        Serial.println();
    }
}
```

### Esempio 2: Sistema di Allarmi

```cpp
void checkAlarms(JKBMS& bms) {
    if (!bms.connected) return;
    
    // Verifica tensioni celle
    for (int i = 0; i < 16; i++) {
        if (bms.cellVoltage[i] > 0) {
            if (bms.cellVoltage[i] < 2.8) {
                Serial.printf("ALLARME: Cella %d sottotensione: %.3fV\n", i+1, bms.cellVoltage[i]);
            }
            if (bms.cellVoltage[i] > 3.65) {
                Serial.printf("ALLARME: Cella %d sovratensione: %.3fV\n", i+1, bms.cellVoltage[i]);
            }
        }
    }
    
    // Verifica temperature
    if (bms.Battery_T1 > 60) {
        Serial.printf("ALLARME: Sovratemperatura T1: %.1f°C\n", bms.Battery_T1);
    }
    if (bms.MOS_Temp > 80) {
        Serial.printf("ALLARME: Sovratemperatura MOSFET: %.1f°C\n", bms.MOS_Temp);
    }
    
    // Verifica correnti
    if (abs(bms.Charge_Current) > bms.max_charge_current) {
        Serial.printf("ALLARME: Sovracorrente: %.2fA (max: %.2fA)\n", 
                     bms.Charge_Current, bms.max_charge_current);
    }
}
```

### Esempio 3: Controllo Automatico

```cpp
void automaticControl(JKBMS& bms) {
    if (!bms.connected) return;
    
    // Disabilita carica se temperatura troppo alta
    if (bms.Battery_T1 > 45 && bms.Charge) {
        Serial.println("Disabilitazione carica per alta temperatura");
        bms.writeRegister(0x1D, 0x00000000, 0x04);
    }
    
    // Riabilita carica se temperatura normale
    if (bms.Battery_T1 < 40 && !bms.Charge) {
        Serial.println("Riabilitazione carica");
        bms.writeRegister(0x1D, 0x00000001, 0x04);
    }
    
    // Forza bilanciamento se delta tensione troppo alta
    if (bms.Delta_Cell_Voltage > 0.1 && !bms.Balance) {
        Serial.println("Attivazione bilanciamento forzato");
        bms.writeRegister(0x1F, 0x00000001, 0x04);
    }
}
```

### Esempio 4: Logging Dati

```cpp
#include <SPIFFS.h>

void logBMSData(JKBMS& bms) {
    if (!bms.connected || !bms.new_data) return;
    
    File logFile = SPIFFS.open("/bms_log.csv", "a");
    if (logFile) {
        // Timestamp, MAC, Voltage, Current, SOC, Temp
        logFile.printf("%lu,%s,%.3f,%.3f,%d,%.1f\n",
                      millis(),
                      bms.targetMAC.c_str(),
                      bms.Battery_Voltage,
                      bms.Charge_Current,
                      bms.Percent_Remain,
                      bms.Battery_T1);
        logFile.close();
    }
}
```

---

## Troubleshooting

### Problemi Comuni

#### 1. Impossibile Connettersi al BMS

**Sintomi**:

- `connectToServer()` ritorna `false`
- Messaggio "Connection failed"

**Soluzioni**:

```cpp
// Verifica che il BMS sia acceso e in range
// Controlla MAC address corretto
// Aumenta potenza BLE
NimBLEDevice::setPower(ESP_PWR_LVL_P9);

// Aumenta timeout connessione
pClient->setConnectTimeout(15000);

// Verifica che non ci siano troppe connessioni attive
if (NimBLEDevice::getCreatedClientCount() >= 3) {
    // Disconnetti client non utilizzati
}
```

#### 2. Dati Non Aggiornati

**Sintomi**:

- `new_data` rimane `false`
- Timeout di connessione frequenti

**Soluzioni**:

```cpp
// Richiedi dati esplicitamente
bms.writeRegister(0x96, 0x00000000, 0x00);

// Verifica notifiche abilitate
if (pChr && pChr->canNotify()) {
    pChr->subscribe(true, notifyCB);
}

// Aumenta timeout notifiche
if (millis() - bms.lastNotifyTime > 30000) {
    // Riconnetti
}
```

#### 3. Frame Corrotti

**Sintomi**:

- `parseData()` non viene chiamata
- Messaggi "Frame corrotto"

**Soluzioni**:

```cpp
// Verifica interferenze BLE
// Riduci frequenza scansioni
pScan->setInterval(2000);  // Intervallo maggiore

// Aggiungi controlli aggiuntivi
if (length < 4 || length > 20) {
    DEBUG_PRINTLN("Lunghezza frame non valida");
    return;
}
```

#### 4. Memoria Insufficiente

**Sintomi**:

- Crash random
- Errori di allocazione BLE

**Soluzioni**:

```cpp
// Limita numero connessioni simultanee
#define MAX_BMS_CONNECTIONS 2

// Ottimizza buffer
// Riduci dimensioni debug buffer
char buffer[128];  // Invece di 256

// Pulisci client disconnessi
NimBLEDevice::deleteAllClients();
```

### Diagnostics Avanzate

#### Monitor Connessione

```cpp
void printConnectionStats() {
    Serial.printf("Client attivi: %d\n", NimBLEDevice::getCreatedClientCount());
    Serial.printf("Memoria libera: %d bytes\n", ESP.getFreeHeap());
    
    for (int i = 0; i < bmsDeviceCount; i++) {
        Serial.printf("BMS %s: %s (ultimo dato: %lus fa)\n",
                     jkBmsDevices[i].targetMAC.c_str(),
                     jkBmsDevices[i].connected ? "CONN" : "DISC",
                     (millis() - jkBmsDevices[i].lastNotifyTime) / 1000);
    }
}
```

#### Debug Frame

```cpp
void debugFrame(uint8_t* data, size_t length) {
    Serial.printf("Frame ricevuto (%d bytes):\n", length);
    for (size_t i = 0; i < length; i++) {
        Serial.printf("%02X ", data[i]);
        if ((i + 1) % 16 == 0) Serial.println();
    }
    Serial.println();
}
```

---

## Note di Performance

### Ottimizzazioni

1. **Gestione Memoria**: La libreria usa buffer statici per evitare frammentazione
2. **BLE Throttling**: Sistema di throttling per evitare overflow di notifiche
3. **Connection Pooling**: Riutilizzo di client BLE esistenti
4. **Scan Optimization**: Parametri di scansione ottimizzati per ridurre interferenze

### Limiti Noti

- **Connessioni Simultanee**: Massimo 3-4 BMS per ESP32
- **Range BLE**: ~10-30 metri in campo aperto
- **Latenza Dati**: ~1-3 secondi per aggiornamento completo
- **Memoria**: ~8KB RAM per istanza BMS

### Raccomandazioni

1. Utilizzare alimentazione stabile per ESP32
2. Mantenere distanza < 10m per comunicazione affidabile
3. Evitare interferenze WiFi su canali 2.4GHz sovrapposti
4. Implementare watchdog per reset automatico in caso di blocco
5. Monitorare memoria heap per prevenire crash

---

## Conclusione

La libreria JKBMS fornisce un'interfaccia completa e robusta per il monitoraggio e controllo di sistemi di gestione batteria JKBMS. Con le sue funzionalità di parsing automatico, gestione errori e supporto multi-dispositivo, rappresenta una soluzione professionale per applicazioni industriali e domestiche.

Per supporto tecnico o contributi al progetto, consultare la documentazione del codice sorgente e i commenti inline per dettagli implementativi specifici.
