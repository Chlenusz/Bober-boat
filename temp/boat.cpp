#include "boat_lib.h"

// ==================Definicje Globalne===================
#define NSS_PIN  5
#define RST_PIN  14
#define DIO0_PIN 33

#define PWM_PIN 16


void onReceive(int packetSize);

// ==================Zmienne globalne===================
unsigned long currentTime = 0;
unsigned long lastTelemetryTime = 0;
unsigned long lastControlTime = 0;

bool LoRaStatus = false;

// ===================== Funkcje Lokalne ==========================
void setThrottle(int8_t value) {
    ledcWrite(0, value); // Ustawienie wartości PWM na podstawie wartości przepustnicy
    Serial.println("Ustawianie przepustnicy na wartość: " + String(value));
}
// ===================== Wstępna konfiguracja ======================
void setup(){
    Serial.begin(115200);
    Serial.println("");
    Serial.println("Setup zakończony");
    ledcSetup(0,500, 8); // Konfiguracja kanału PWM: kanał 0, częstotliwość 500 Hz, rozdzielczość 8 bitów
    ledcAttachPin(PWM_PIN, 0); // Przypisanie pinu do kanału PWM
    LoRaStatus = setupLoRa(NSS_PIN, RST_PIN, DIO0_PIN);
    Serial.println("LoRa setup: " + String(LoRaStatus ? "sukces" : "niepowodzenie"));
    packetId = PacketID::ID_CONTROL;
}
// ===================== Główna pętla programu =====================
void loop(){
    currentTime = millis();
    if (LoRaStatus){
        if(currentTime - lastTelemetryTime >= TELEMETRY_INTERVAL_MS) {
            lastTelemetryTime = currentTime;
            sendMessage(SERVER_ADDRESS, BOAT_ADDRESS, telemetry);
        }
        LoRa.receive();
    }else {
        LoRaStatus = setupLoRa();
    }
    
    if (newDataReady) {
        newDataReady = false; // Reset flag after processing
        Serial.println("Przetwarzanie odebranej wiadomości...");
        bool decodeSuccess = decodeMessage(packetId);
        if (!decodeSuccess) {
            Serial.println("Nie można przetworzyć odebranej wiadomości sterującej.");
        }

    }

    if (currentTime-lastControlTime >= CONTROL_INTERVAL_MS) {
        lastControlTime = currentTime;
        setThrottle(control.throttle);
    }

}



