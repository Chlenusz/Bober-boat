#include "boat_lib.h"

// ==================Definicje Globalne===================
#define NSS_PIN  5
#define RST_PIN  14
#define DIO0_PIN 2

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
    ledcAttachPin(PWM_PIN, 0); // Przypisanie pinu do kanału PWM
    LoRaStatus = setupLoRa();
    Serial.println("LoRa setup: " + String(LoRaStatus ? "sukces" : "niepowodzenie"));
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
        if(packetId == PacketID::ID_CONTROL) {
            bool decodeSuccess = decodeMessage(packetId);
            if (!decodeSuccess) {
                Serial.println("Nie można przetworzyć odebranej wiadomości sterującej.");
            }
        } else {
            Serial.println("Odebrano wiadomość, ale nie jest to pakiet sterujący. Ignorowanie.");
        }
    }

    if (currentTime-lastControlTime >= CONTROL_INTERVAL_MS) {
        lastControlTime = currentTime;
        setThrottle(control.throttle);
    }

}



