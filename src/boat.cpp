#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <SPI.h> 
#include <LoRa.h> 

// ==================Definicje Globalne===================

// Ustawienia telemetrii
#define TELEMETRY_INTERVAL_MS 5000
#define RECONNECT_INTERVAL_MS 5000

// Ustawienia WiFi / AP
#define WIFI_CHANNEL 11
#define SSID "BOBER_AP"
#define PASSWORD "bober123"

// Ustawienia UDP
#define LOCAL_UDP_PORT 4444      
#define TRANSCIVER_MODE false

// LoRa PINOUT
#define NSS_PIN  5
#define RST_PIN  14
#define DIO0_PIN 2

// ==================Struktury===================
struct __attribute__((packed)) telemetryData {
    uint8_t packetID;      
    int8_t boatTemperature;     
    int8_t boatTemp;       
    int16_t sens1;         
    int16_t sens2;         
    float sens3;           
    float sens4;          
    int16_t Rssi;          
};

struct __attribute__((packed)) controlData {
    uint8_t packetID;      
    float horizontal;      
    float throttle;        
};

struct deviceCredentials{
    IPAddress ip;
    uint16_t port;
    bool connected;
};

enum dataType {
    TELEMETRY,
    CONTROL
};

enum deviceType {
    BOAT,
    ANDROID,
    SERVER,
    UNKONWN
};

enum rudderType {
    LEFT,
    RIGHT
};

// ==================Zmienne globalne===================

WiFiUDP udp; // Obiekt do obsługi UDP

deviceCredentials serverDevice;

telemetryData telemetry;
controlData control;
JsonDocument doc;

unsigned long currentTime = 0;
unsigned long lastTelemetryTime = 0;
unsigned long lastReconnectAttempt = 0;

bool shortRangeCommunication = true;
bool longRangeCommunication = false;

// ==================Konfiguracja Wifi==================
bool wifiConnect() {
  Serial.println("Próba połączenia z siecią WiFi: " SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);

  Serial.print("Łączenie z siecią WiFi");
  
  int timeout = 60;
  while (WiFi.status() != WL_CONNECTED && timeout > 0) {
    delay(500);
    Serial.print(".");
    timeout--;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Połączono! Adres IP: ");
    Serial.println(WiFi.localIP());
    serverDevice.ip = WiFi.gatewayIP();
    serverDevice.port = LOCAL_UDP_PORT;
    
    // Uruchomienie gniazda UDP po pomyślnym podłączeniu do sieci
    udp.begin(LOCAL_UDP_PORT);
    Serial.printf("Nasłuch UDP uruchomiony na porcie: %d\n", LOCAL_UDP_PORT);
    return true;
  } else {
    Serial.println("Nie udało się połączyć z WiFi");
    return false;
  }
}

bool restartWifi(){
    return wifiConnect();
}

bool isConnected() {
    if (WiFi.status() == WL_CONNECTED) {
        if (!serverDevice.connected) {
            serverDevice.connected = true;
            Serial.println("Połączono z siecią WiFi!");
        }
        return true;
    } else {
        if (serverDevice.connected) {
            serverDevice.connected = false;
            Serial.println("Utracono połączenie z siecią WiFi!");
        }
        return false;
    }
}
// ==============Konfiguracja SPI i LoRa================
bool setupLoRa() {
    SPI.begin(SCK, MISO, MOSI, NSS_PIN); // SCK, MISO, MOSI, SS
    
    LoRa.setPins(NSS_PIN, RST_PIN, DIO0_PIN);

    if (!LoRa.begin(915E6)) {
        Serial.println("Błąd inicjalizacji LoRa! Sprawdź podłączenie SPI.");
        return false;
    }
    LoRa.enableCrc();
    LoRa.setCodingRate4(8);
    // LoRa.setSpreadingFactor(9); // Jeśli będzie bardzo przerywać, włącz to || wolniejszy przesył danych
    Serial.println("LoRa zainicjalizowana pomyślnie.");
    return true;
}

// ==============Komunikajca Krótki zasięg==============
String getJson(telemetryData& telemetry, deviceType myDeviceType) {
    JsonDocument doc;
    
    doc["deviceType"] = static_cast<int>(myDeviceType); 
    doc["dataType"] = static_cast<int>(TELEMETRY);
    
    doc["BTemp"] = telemetry.boatTemperature;
    doc["rssi"] = telemetry.Rssi;

    String output;
    serializeJson(doc, output);
    return output; 
}

String getJson(controlData& control, deviceType myDeviceType) {
    JsonDocument doc;

    doc["deviceType"] = static_cast<int>(myDeviceType);
    doc["dataType"] = static_cast<int>(CONTROL);
    
    doc["throttle"] = control.throttle;
    doc["rudder"] = control.horizontal;

    String output;
    serializeJson(doc, output);
    return output; 
}

void unpackJson(JsonDocument& doc, controlData& control) {
    control.throttle = doc["throttle"] | 0.0f; // Domyślna wartość 0.0f
    control.horizontal = doc["rudder"] | 0.0f; // Domyślna wartość 0.0f
}

void unpackJson(JsonDocument& doc, telemetryData& telemetry) {
    telemetry.boatTemperature = doc["BTemp"] | 0;
    telemetry.sens1 = doc["sens1"] | 0;
    telemetry.sens2 = doc["sens2"] | 0;
    telemetry.sens3 = doc["sens3"] | 0.0f;
    telemetry.sens4 = doc["sens4"] | 0.0f;
    telemetry.Rssi = doc["rssi"] | 0;
}

void sendUDP(deviceCredentials& targetDevice, const String& message) {
    if (!targetDevice.connected) return;
    bool deviceStatus;
    bool sentStatus;

    // Rozpoczęcie pakietu UDP
    deviceStatus = udp.beginPacket(targetDevice.ip, targetDevice.port);

    if (!deviceStatus) {
        Serial.println("Błąd: nie można rozpocząć pakietu UDP do urządzenia docelowego!");
        return;
    }
    
    udp.print(message);

    sentStatus = udp.endPacket();

    if (!sentStatus) {
        Serial.println("Błąd: nie można wysłać pakietu UDP do urządzenia docelowego!");
        return;
    }

    Serial.println("Wysłano pakiet UDP do "+String(targetDevice.ip.toString()));
}

void receiveUDP() {
    int packetSize = udp.parsePacket();
    if (packetSize) {
        char incomingPacket[255];
        int len = udp.read(incomingPacket, 255);
        if (len > 0) {
            incomingPacket[len] = '\0'; 
        }
        
        DeserializationError error = deserializeJson(doc, incomingPacket);

        if (error) {
            Serial.println("Błąd parsowania JSON w receiveUDP");
            return;
        }

        // Odczyt enumów z JSONa (z bezpiecznymi wartościami domyślnymi)
        deviceType senderDevice = static_cast<deviceType>(doc["deviceType"] | static_cast<int>(UNKONWN));
        dataType currentData = static_cast<dataType>(doc["dataType"] | static_cast<int>(TELEMETRY));
        // Aktualizacja statusu i danych na podstawie enuma

        switch (currentData){
            case CONTROL:
                unpackJson(doc, control);
                break;
            case TELEMETRY:
                unpackJson(doc, telemetry);
                break;
            default:
                break;
        }
    }
}
// ===============Komunikacja Daleki zasięg===============
void sendLoRa(const String& message) {
    LoRa.beginPacket();
    LoRa.print(message);
    LoRa.endPacket();
    Serial.println("Wysłano pakiet przez LoRa");
}

// DODANO: Funkcja do odbierania i przetwarzania pakietów LoRa
void receiveLoRa() {
    int packetSize = LoRa.parsePacket();
    if (packetSize) {
        String incomingPacket = "";
        while (LoRa.available()) {
            incomingPacket += (char)LoRa.read();
        }
        
        DeserializationError error = deserializeJson(doc, incomingPacket);

        if (error) {
            Serial.println("Błąd parsowania JSON w receiveLoRa");
            return;
        }

        deviceType senderDevice = static_cast<deviceType>(doc["deviceType"] | static_cast<int>(UNKONWN));
        dataType currentData = static_cast<dataType>(doc["dataType"] | static_cast<int>(TELEMETRY));

        switch (currentData){
            case CONTROL:
                unpackJson(doc, control);
                break;
            case TELEMETRY:
                unpackJson(doc, telemetry);
                break;
            default:
                break;
        }
        Serial.println("Odebrano pakiet przez LoRa");
    }
}
// -------------------- FUNKCJA: konfiguracja i start serwera --------------------

void setup(){
    Serial.begin(115200);
    Serial.println("");
    Serial.println("Setup zakończony");
    serverDevice.connected = wifiConnect();
    Serial.printf("PINOUT SPI: MOSI: %d, MISO: %d, SCK: %d, NSS: %d\n", MOSI, MISO, SCK, SS);
}

void loop(){
    currentTime = millis();

    if((currentTime-lastTelemetryTime >= TELEMETRY_INTERVAL_MS) && serverDevice.connected){
        lastTelemetryTime = currentTime;
        telemetry.Rssi = WiFi.RSSI();
        telemetry.boatTemperature = temperatureRead();
        sendUDP(serverDevice, getJson(telemetry, BOAT));
        Serial.printf("Throttle: %.2f, Horizontal: %.2f\n", control.throttle, control.horizontal);
    }
    if (serverDevice.connected) {
        receiveUDP();
    }

    if(currentTime - lastReconnectAttempt>=RECONNECT_INTERVAL_MS){
        lastReconnectAttempt = currentTime;
        serverDevice.connected = isConnected();
    }

    if (!serverDevice.connected){
        if (currentTime - lastReconnectAttempt >= RECONNECT_INTERVAL_MS) {
            lastReconnectAttempt = currentTime;
            serverDevice.connected = restartWifi();
        }
    }

}



