#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include "boat_lib.h"

// ==================Definicje Globalne===================

// Ustawienia telemetrii
#define TELEMETRY_INTERVAL_MS 5000
#define CONTROL_INTERVAL_MS 1000
#define RECONNECT_INTERVAL_MS 5000

// Ustawienia WiFi / AP
#define WIFI_CHANNEL 11
#define SSID "BOBER_AP"
#define PASSWORD "bober123"

// Ustawienia UDP
#define LOCAL_UDP_PORT 4444       // Port lokalny nasłuchu dla ESP32
// ==================Zmienne globalne===================

WiFiUDP udp; // Obiekt do obsługi UDP

deviceCredentials androidDevice;

JsonDocument doc;

unsigned long currentTime = 0;
unsigned long lastTelemetryTime = 0;
unsigned long lastControlTime = 0;

bool LoRaStatus = false;

// ==================Konfiguracja Wifi===================

bool setupWifi() {
    WiFi.mode(WIFI_AP);
    WiFi.channel(WIFI_CHANNEL);
    bool ok = WiFi.softAP(SSID, PASSWORD);

    if (!ok) {
        Serial.println("Błąd: nie udało się uruchomić AP!");
        return false;
    }
    
    IPAddress ip = WiFi.softAPIP();

    Serial.println("AP uruchomione!");
    Serial.println("SSID: " + String(SSID));
    Serial.println("IP: " + ip.toString());

    bool udpOk = udp.begin(LOCAL_UDP_PORT);
    if (!udpOk) {
        Serial.println("Błąd: nie udało się uruchomić nasłuchu UDP!");
        return false;
    }

    Serial.printf("Nasłuch UDP uruchomiony na porcie: %d\n", LOCAL_UDP_PORT);
    
    return true;
}

void disableWifi(){
    Serial.println("Wyłączanie Access Point...");
    WiFi.disconnect(true,false);
    delay(100);  
    Serial.println(WiFi.status());
    WiFi.mode(WIFI_OFF);
    delay(100);
}

bool restartWifi(){
    disableWifi();
    return setupWifi();
}
// ==================Wysyłanie Danych===================
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
    
    doc["BTemp"] = telemetry.boatTemp;

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
 * @brief Funkcja odpowiedzialna za rozpakowanie danych sterujących z formatu JSON do struktury controlData.
 * 
 * @param doc 
 * @param control 
 */
void unpackJson(JsonDocument& doc, controlData& control) {
    control.throttle = doc["throttle"] | 0.0f; // Domyślna wartość 0.0f
    control.rudder = doc["rudder"] | 0.0f; // Domyślna wartość 0.0f
}
/**
 * @brief Funkcja odpowiedzialna za rozpakowanie danych telemetrycznych z formatu JSON do struktury telemetryData.
 * 
 * @param doc 
 * @param telemetry 
 */
void unpackJson(JsonDocument& doc, telemetryData& telemetry) {
    telemetry.boatTemp = doc["BTemp"] | 0;
    telemetry.sens1 = doc["sens1"] | 0;
    telemetry.sens2 = doc["sens2"] | 0;
    telemetry.sens3 = doc["sens3"] | 0.0f;
    telemetry.sens4 = doc["sens4"] | 0.0f;
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
        Serial.println("Błąd: nie można rozpocząć pakietu UDP do urządzenia docelowego!" + String(targetDevice.ip.toString()));
        return;
    }
    
    udp.print(message);

    sentStatus = udp.endPacket();

    if (!sentStatus) {
        Serial.println("Błąd: nie można wysłać pakietu UDP do urządzenia docelowego!" + String(targetDevice.ip.toString()));
        return;
    }

    Serial.println("Wysłano pakiet UDP do "+String(targetDevice.ip.toString()));
}
/**
 * @brief Funkcja odpowiedzialna za odbieranie wiadomości przez UDP.
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
        if(!androidDevice.connected){
            switch (senderDevice) {
            case ANDROID:
                androidDevice.ip = udp.remoteIP();
                androidDevice.port = udp.remotePort();
                androidDevice.connected = true;
                Serial.println("Połączono z urządzeniem Android!");
                break;
            default:
                Serial.println("Coś poszło nie tak");
                break;
            }
        }

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

// -------------------- FUNKCJA: konfiguracja i start serwera --------------------

void setup(){
    Serial.begin(115200);
    Serial.println("");
    Serial.println("Setup zakończony");
    setupWifi();
    LoRaStatus = setupLoRa();
    Serial.println("LoRa setup: " + String(LoRaStatus ? "sukces" : "niepowodzenie"));
}

void loop(){
    currentTime = millis();
    control.throttle = 128.0f;

    if((currentTime-lastTelemetryTime >= TELEMETRY_INTERVAL_MS)&&androidDevice.connected){
        lastTelemetryTime = currentTime;
        telemetry.serverTemp = temperatureRead();
        sendUDP(androidDevice, getJson(telemetry,SERVER));
    }

    if (LoRaStatus) {
        if ((currentTime - lastControlTime >= CONTROL_INTERVAL_MS)) {
            lastControlTime = currentTime;
            Serial.println("Wysyłanie danych sterujących do łódki" + String(control.throttle));
            sendMessage(BOAT_ADDRESS, SERVER_ADDRESS, control);
            LoRa.receive();
        }
        
    } else {
        LoRaStatus = setupLoRa();
    }

    receiveUDP();
}




