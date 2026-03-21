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
    float rudder;      
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

enum PacketID : uint8_t {
    ID_TELEMETRY = 1,
    ID_CONTROL = 2
};

// ==================Zmienne globalne===================

WiFiUDP udp; // Obiekt do obsługi UDP
deviceCredentials serverDevice; // Struktura przechowująca dane serwera (adres IP, port, status połączenia)

telemetryData telemetry; // Struktura przechowująca dane telemetryczne
controlData control; // Struktura przechowująca dane sterujące
JsonDocument doc;

unsigned long currentTime = 0;
unsigned long lastTelemetryTime = 0;
unsigned long lastReconnectAttempt = 0;

bool shortRangeCommunication = true;
bool longRangeCommunication = false;

// ==================Konfiguracja Wifi==================
/**
 * @brief Funkcja odpowiedzialna za połącznie z siecią WiFi.
 * Próbuje połączyć się z siecią WiFi o zadanych SSID i PASSWORD.
 * Jeśli połączenie jest udane, ustawia adres IP i port docelowego urządzenia (serwera) oraz uruchamia nasłuch UDP.
 * W przypadku niepowodzenia, zwraca false.
 * 
 * @return `true` jeśli połączenie z siecią WiFi zostało nawiązane pomyślnie, `false` w przypadku błędu.
 */
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
/**
 * @brief Funkcja odpowiedzialna za ponowne uruchomienie połączenia WiFi.
 * 
 * @return `true` jeśli ponowne połączenie z siecią WiFi zakończyło się sukcesem, `false` w przypadku błędu.
 */
bool restartWifi(){
    return wifiConnect();
}
/**
 * @brief Funkcja odpowiedzialna za sprawdzenie statusu połączenia WiFi.
 * Sprawdza, czy urządzenie jest aktualnie połączone z siecią WiFi.
 * Jeśli status połączenia uległ zmianie (połączenie nawiązane lub utracone),
 * aktualizuje flagę `connected` w strukturze `serverDevice` i wyświetla odpowiedni komunikat na monitorze szeregowym.
 * 
 * @return `true` jeśli urządzenie jest połączone z siecią WiFi, `false` w przeciwnym przypadku.
 */
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
/**
 * @brief Funkcja odpowiedzialna za konfigurację modułu LoRa.
 * 
 * @return `true` jeśli inicjalizacja LoRa zakończyła się sukcesem, `false` w przypadku błędu.
 */
bool setupLoRa() {
    SPI.begin(SCK, MISO, MOSI, NSS_PIN); // SCK:18, MISO:19, MOSI:23, NSS:5
    
    LoRa.setPins(NSS_PIN, RST_PIN, DIO0_PIN);

    if (!LoRa.begin(915E6)) {
        Serial.println("Błąd inicjalizacji LoRa! Sprawdź podłączenie SPI.");
        return false;
    }
    LoRa.enableCrc();
    LoRa.setCodingRate4(8);
    //LoRa.setSpreadingFactor(9); // Jeśli będzie bardzo przerywać, włącz to || wolniejszy przesył danych
    Serial.println("LoRa zainicjalizowana pomyślnie.");
    return true;
}
// ==============Komunikajca Krótki zasięg==============
/**
 * @brief Funkcja odpowiedzialna za konwersję danych telemetrycznych do formatu JSON.
 * 
 * @param telemetry
 * @param myDeviceType 
 * 
 * @return String
 */
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
/**
 * @brief Funkcja odpowiedzialna za konwersję danych sterujących do formatu JSON.
 * 
 * @param control 
 * @param myDeviceType 
 * @return String
 */
String getJson(controlData& control, deviceType myDeviceType) {
    JsonDocument doc;

    doc["deviceType"] = static_cast<int>(myDeviceType);
    doc["dataType"] = static_cast<int>(CONTROL);
    
    doc["throttle"] = control.throttle;
    doc["rudder"] = control.rudder;

    String output;
    serializeJson(doc, output);
    return output; 
}
/**
 * @brief Funkcja odpowiedzialna za rozpakowanie danych sterujących z formatu JSON.
 * Dane od razu zapisywane są do struktury controlData, z bezpiecznymi wartościami domyślnymi w przypadku braku kluczy w JSONie.
 * @param doc 
 * @param control 
 */
void unpackJson(JsonDocument& doc, controlData& control) {
    control.throttle = doc["throttle"] | 0.0f; // Domyślna wartość 0.0f
    control.rudder = doc["rudder"] | 0.0f; // Domyślna wartość 0.0f
}
/**
 * @brief Funkcja odpowiedzialna za rozpakowanie danych telemetrycznych z formatu JSON.
 * Dane od razu zapisywane są do struktury telemetryData, z bezpiecznymi wartościami domyślnymi w przypadku braku kluczy w JSONie.
 * 
 * @param doc 
 * @param telemetry 
 */
void unpackJson(JsonDocument& doc, telemetryData& telemetry) {
    telemetry.boatTemperature = doc["BTemp"] | 0;
    telemetry.sens1 = doc["sens1"] | 0;
    telemetry.sens2 = doc["sens2"] | 0;
    telemetry.sens3 = doc["sens3"] | 0.0f;
    telemetry.sens4 = doc["sens4"] | 0.0f;
    telemetry.Rssi = doc["rssi"] | 0;
}
/**
 * @brief Funkcja odpowiedzialna za wysyłanie wiadomości przez UDP.
 * 
 * @param targetDevice 
 * @param message 
 */
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
/**
 * @brief Funkcja odpowiedzialna za odbieranie wiadomości przez UDP.
 * Odczytuje pakiety UDP, parsuje je jako JSON i aktualizuje odpowiednie struktury danych (controlData lub telemetryData) na podstawie typu danych zawartego w JSONie.
 * 
 */
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
void sendLoRaTelemetry(const telemetryData& data) {
    LoRa.beginPacket();
    // Bezpośrednie zrzucenie pamięci struktury do bufora FIFO modułu LoRa
    LoRa.write((const uint8_t*)&data, sizeof(telemetryData));
    LoRa.endPacket();
    Serial.println("Wysłano binarną telemetrię przez LoRa");
}

void receiveLoRa() {
    int packetSize = LoRa.parsePacket();
    if (packetSize) {
        // Odczytanie samego identyfikatora bez usuwania go z bufora układu LoRa
        uint8_t incomingPacketID = LoRa.peek();

        if (incomingPacketID == ID_TELEMETRY && packetSize == sizeof(telemetryData)) {
            telemetryData rxTelemetry;
            LoRa.readBytes((uint8_t*)&rxTelemetry, sizeof(telemetryData));
            
            telemetry.boatTemperature = rxTelemetry.boatTemp;
            telemetry.sens1 = rxTelemetry.sens1;
            telemetry.sens2 = rxTelemetry.sens2;
            telemetry.sens3 = rxTelemetry.sens3;
            telemetry.sens4 = rxTelemetry.sens4;
            
            Serial.println("Odebrano poprawnie telemetrię");

        } else if (incomingPacketID == ID_CONTROL && packetSize == sizeof(controlData)) {
            controlData rxControl;
            LoRa.readBytes((uint8_t*)&rxControl, sizeof(controlData));
            
            control.rudder = rxControl.rudder;
            control.throttle = rxControl.throttle;
            
            Serial.println("Odebrano poprawnie sterowanie");
        } else {
            // Pakiet nie pasuje rozmiarem lub ID - błąd struktury, czyszczenie bufora
            while (LoRa.available()) {
                LoRa.read();
            }
        }
    }
}
// ===================== Wstępna konfiguracja ======================

void setup(){
    Serial.begin(115200);
    Serial.println("");
    Serial.println("Setup zakończony");
    serverDevice.connected = wifiConnect();
    longRangeCommunication = setupLoRa();
}
// ===================== Główna pętla programu =====================
void loop(){
    currentTime = millis();
    if (longRangeCommunication){
        receiveLoRa();
    }
    if((currentTime-lastTelemetryTime >= TELEMETRY_INTERVAL_MS) && serverDevice.connected){
        lastTelemetryTime = currentTime;
        telemetry.Rssi = WiFi.RSSI();
        telemetry.boatTemperature = temperatureRead();
        sendUDP(serverDevice, getJson(telemetry, BOAT));
        Serial.printf("Throttle: %.2f, rudder: %.2f\n", control.throttle, control.rudder);
    }
    if (longRangeCommunication){
        sendLoRaTelemetry(telemetry);
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



