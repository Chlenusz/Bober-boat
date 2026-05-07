#include "boat_lib.h"
#include "DHT.h"
#include <TinyGPSPlus.h>
#include <HardwareSerial.h>

// ==================Definicje Globalne===================

#define PWM_PIN 25

#define DTH_PIN 26
#define DHT_INTERVAL 5000
#define DHTTYPE DHT11

// Definicje pinów GPS 
#define GPS_RX_PIN 16 // RX modułu NEO-6M
#define GPS_TX_PIN 17 // TX modułu NEO-6M
#define GPS_BAUD 9600

#define DEBUG

// ==================Zmienne globalne===================
unsigned long currentTime = 0;
unsigned long lastTelemetryTime = 0;
unsigned long lastDHTTime = 0;
unsigned long lastControlTime = 0;

bool LoRaStatus = false;

DHT dht(DTH_PIN, DHTTYPE); // Inicjalizacja czujnika DHT11
TinyGPSPlus gps; // Inicjalizacja obiektu TinyGPSPlus do obsługi GPS
HardwareSerial gpsSerial(2); // Inicjalizacja sprzętowego UART2 na wybranych pinach

// ===================== Funkcje Lokalne ==========================
void setThrottle(int8_t value) {
    ledcWrite(0, value); // Ustawienie wartości PWM na podstawie wartości przepustnicy
    #ifdef DEBUG
    Serial.println("Ustawianie przepustnicy na wartość: " + String(value));
    #endif
}


// ===================== Wstępna konfiguracja ======================
void setup(){
    Serial.begin(115200);
    Serial.println("");
    Serial.println("Setup zakończony");

    gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

    ledcSetup(0,500, 8); // Konfiguracja kanału PWM: kanał 0, częstotliwość 500 Hz, rozdzielczość 8 bitów
    ledcAttachPin(PWM_PIN, 0); // Przypisanie pinu do kanału PWM

    LoRaStatus = setupLoRa(NSS_PIN, RST_PIN, DIO0_PIN);
    Serial.println("LoRa setup: " + String(LoRaStatus ? "sukces" : "niepowodzenie"));
    packetId = PacketID::ID_CONTROL;
    dht.begin(); 
}
// ===================== Główna pętla programu =====================
void loop(){
    currentTime = millis();

    while (gpsSerial.available() > 0) {
        gps.encode(gpsSerial.read());
    }

    if (gps.location.isUpdated()) {
        telemetry.GPSLat = gps.location.lat();
        telemetry.GPSLng = gps.location.lng();
        Serial.println("Aktualizacja danych GPS: Lat = " + String(telemetry.GPSLat, 6) + ", Lng = " + String(telemetry.GPSLng, 6));
    }

    if (LoRaStatus){
        if(currentTime - lastTelemetryTime >= TELEMETRY_INTERVAL_MS) {
            lastTelemetryTime = currentTime;
            telemetry.boatTemp = temperatureRead();
            telemetry.boatRssi = LoRa.packetRssi();
            Serial.println("LoRa Rssi:"+String(telemetry.boatRssi));
            sendMessage(SERVER_ADDRESS, BOAT_ADDRESS, telemetry);
            LoRa.receive();
        }
        
    }else {
        LoRaStatus = setupLoRa();
    }
    
    if (newDataReady) {
        newDataReady = false; // Reset flag after processing
        bool decodeSuccess = decodeMessage(packetId);
        if (!decodeSuccess) {
            Serial.println("Nie można przetworzyć odebranej wiadomości sterującej.");
        }

    }

    if (currentTime-lastControlTime >= CONTROL_INTERVAL_MS) {
        lastControlTime = currentTime;

        //setThrottle(control.throttle);
    }

    if(currentTime - lastDHTTime >= DHT_INTERVAL) {
        lastDHTTime = currentTime;
        telemetry.sens3 = dht.readTemperature();
        telemetry.sens4 = dht.readHumidity();
    }

}