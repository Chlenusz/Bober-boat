#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include "boat_lib.h"

// ==================Definicje Globalne===================
#define DEBUG

// Ustawienia WiFi / AP
#define WIFI_CHANNEL 11
#define SSID "BOBER_AP"
#define PASSWORD "bober123"
#define RECONNECT_INTERVAL_MS 5000

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
bool newRouteReceived = false;
#ifdef DEBUG
uint32_t packetUID = 0;
#endif
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
    
    // Surowe dane, które nie potrzebują matematyki (1 bajtowe)
    doc["boatTemp"] = telemetry.boatTemp;
    doc["serverTemp"] = telemetry.serverTemp;
    doc["boatRssi"] = telemetry.boatRssi;
    doc["PT100"] = telemetry.PT100;
    doc["GPSLat"] = (double)telemetry.GPSLat / 10000000.0;
    doc["GPSLng"] = (double)telemetry.GPSLng / 10000000.0;
    
    doc["DHTTemp"] = (float)telemetry.DHTTemp / 10.0f;
    doc["DHTHumid"] = (float)telemetry.DHTHumid / 10.0f;

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
    // Odczyt przepustnicy
    float tempT = doc["throttle"] | 0.0f; 
    if (tempT < 0.0f) tempT = 0.0f; 
    if (tempT > 65000.0f) tempT = 65000.0f; // Limit bezpieczeństwa
    control.throttle = (uint16_t)tempT;     // Bezpośrednie przypisanie!

    // Odczyt steru (rudder)
    float tempR = doc["rudder"] | 0.0f; 
    if (tempR < 0.0f) tempR = 0.0f; 
    if (tempR > 65000.0f) tempR = 65000.0f; // Limit bezpieczeństwa
    control.rudder = (uint16_t)tempR;       // Bezpośrednie przypisanie!
}
/**
 * @brief Funkcja odpowiedzialna za rozpakowanie danych telemetrycznych z formatu JSON do struktury telemetryData.
 * 
 * @param doc 
 * @param telemetry 
 */
void unpackJson(JsonDocument& doc, telemetryData& telemetry) {
    // Odczyt prostych wartości 1-bajtowych
    telemetry.serverTemp = doc["serverTemp"] | 0;
    telemetry.boatTemp = doc["boatTemp"] | 0;
    telemetry.boatRssi = doc["boatRssi"] | 0;
    telemetry.PT100 = doc["PT100"] | 0;

    // Odczyt ułamków i kompresja do int32_t (Fixed-Point)
    double lat = doc["GPSLat"] | 0.0;
    double lng = doc["GPSLng"] | 0.0;
    telemetry.GPSLat = (int32_t)(lat * 10000000.0);
    telemetry.GPSLng = (int32_t)(lng * 10000000.0);

    float dhtT = doc["DHTTemp"] | 0.0f;
    float dhtH = doc["DHTHumid"] | 0.0f;
    telemetry.DHTTemp = (int32_t)(dhtT * 10.0f);
    telemetry.DHTHumid = (int32_t)(dhtH * 10.0f);
}
/**
 * @brief Funkcja odpowiedzialna za rozpakowanie danych trasy z formatu JSON do struktury routeData.
 * 
 * @param doc 
 * @param route 
 */
void unpackJson(JsonDocument& doc, routeData& route) {
    JsonArray routeArray = doc["route"];
    route.pointsCount = routeArray.size();
    
    // Zabezpieczenie na poziomie serwera (na wszelki wypadek)
    if (route.pointsCount > 30) route.pointsCount = 30;

    for (int i = 0; i < route.pointsCount; i++) {
        // Wyciągamy double i od razu konwertujemy na Fixed-Point dla LoRa (mnożenie)
        double lat = routeArray[i]["lat"];
        double lng = routeArray[i]["lng"];
        
        route.waypoints[i].lat = (int32_t)(lat * 10000000.0);
        route.waypoints[i].lng = (int32_t)(lng * 10000000.0);
    }
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

    Serial.println("Wysłano pakiet UDP do "+String(targetDevice.ip.toString())+" ID:"+String(packetUID));
    packetUID++;
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
                Serial.println("Odebrano dane sterujące: Throttle = " + String(control.throttle) + ", Rudder = " + String(control.rudder));
                break;
            case TELEMETRY:
                unpackJson(doc, telemetry);
                Serial.println("Odebrano telemetrię: BoatTemp = " + String(telemetry.boatTemp) + 
               "°C, PT100 = " + String(telemetry.PT100) + 
               "°C, DHT Temp = " + String(telemetry.DHTTemp / 10.0f, 1) + 
               "°C, DHT Humid = " + String(telemetry.DHTHumid / 10.0f, 1) + 
               "%, RSSI = " + String(telemetry.boatRssi) + " dBm");
                break;
            case ROUTE:
                unpackJson(doc, route);
                newRouteReceived = true;     // Podnosimy flagę dla pętli loop
                Serial.println("Odebrano nową trasę z Androida! Punktów: " + String(route.pointsCount));
                break;
            case CONNECT:
                Serial.println("Odebrano żądanie połączenia.");
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
    telemetry.boatTemp = 36.6f;
}

void loop(){
    currentTime = millis();

    if (WiFi.softAPgetStationNum() == 0) {
        if (androidDevice.connected) {
            androidDevice.connected = false;
            Serial.println("Urządzenie odłączone od Wi-Fi. Wstrzymuję wysyłanie UDP.");
        }
    }

    if((currentTime-lastTelemetryTime >= TELEMETRY_INTERVAL_MS) && androidDevice.connected){
        lastTelemetryTime = currentTime;
        telemetry.serverTemp = temperatureRead();
        sendUDP(androidDevice, getJson(telemetry,SERVER));
    }

    if (LoRaStatus) {
        if ((currentTime - lastControlTime >= CONTROL_INTERVAL_MS)) {
            lastControlTime = currentTime;
            if(!isReceiving()){
                sendMessage(BOAT_ADDRESS, SERVER_ADDRESS, control);
                LoRa.receive();
            }
        }
        if (newRouteReceived) {
            if(!isReceiving()){
                newRouteReceived = false; // Reset flagi
                sendMessage(BOAT_ADDRESS, SERVER_ADDRESS, route);
                LoRa.receive();
            }
        }
    } else {
        LoRaStatus = setupLoRa();
    }

    if (newDataReady) {
        newDataReady = false; 
        bool decodeSuccess = decodeMessage(packetId);
        if (!decodeSuccess) {
            Serial.println("Nie można przetworzyć odebranej wiadomości telemetrycznej.");
        }
    }

    receiveUDP();
}




