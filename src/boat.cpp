#include "boat_lib.h"
#include "Navigation.h"
#include "DHT.h"
#include <TinyGPSPlus.h>
#include <HardwareSerial.h>
#include <PID_v1.h>

// ================== Zmienne współdzielone (Zabezpieczone Mutexem) ===================
// Te struktury są aktualizowane na jednym rdzeniu i czytane na drugim
SemaphoreHandle_t mutexData; 

// ================== Definicje i obiekty ===================
#define PWM_PIN 25
#define DTH_PIN 26
#define DHT_INTERVAL 5000
#define DHTTYPE DHT11
#define GPS_RX_PIN 16 
#define GPS_TX_PIN 17 
#define GPS_BAUD 9600
#define DEBUG

unsigned long lastDHTTime = 0;
unsigned long lastTelemetryTime = 0;
bool LoRaStatus = false;

// Obiekty
PID myPID(&Input, &Output, &Setpoint, Kp, Ki, Kd, DIRECT);
DHT dht(DTH_PIN, DHTTYPE);
TinyGPSPlus gps;
HardwareSerial gpsSerial(2);

#ifndef DEBUG
NavigationSystem navigation;
#endif

// Uchwyty zadań
TaskHandle_t TaskHardware;
TaskHandle_t TaskLogic;

// ===================== Funkcje pomocnicze ==========================
void setThrottle(int16_t value) {
    ledcWrite(0, (uint8_t)value); 
}

// ===================================================================
// RDZEŃ 1: HARDWARE & COMMS (LoRa, GPS Raw, PWM)
// ===================================================================
void codeHardware(void * pvParameters) {
    for(;;) {
        unsigned long now = millis();

        // 1. Obsługa GPS (Raw data z UART)
        while (gpsSerial.available() > 0) {
            gps.encode(gpsSerial.read());
        }

        // 2. Obsługa LoRa (Odbieranie i wysyłanie)
        if (LoRaStatus) {
            // Jeśli przyszły dane (obsługiwane przez przerwanie onReceive w boat_lib)
            if (newDataReady) {
                newDataReady = false;
                xSemaphoreTake(mutexData, portMAX_DELAY);
                decodeMessage(packetId); // decodeMessage aktualizuje globalne 'control' i 'route'
                xSemaphoreGive(mutexData);
            }

            // Wysyłanie telemetrii (co interwał)
            if(now - lastTelemetryTime >= TELEMETRY_INTERVAL_MS) {
                lastTelemetryTime = now;
                xSemaphoreTake(mutexData, portMAX_DELAY);
                // Przygotowanie danych do wysyłki
                telemetry.boatRssi = LoRa.packetRssi();
                sendMessage(SERVER_ADDRESS, BOAT_ADDRESS, telemetry);
                xSemaphoreGive(mutexData);
                LoRa.receive();
            }
        } else {
            LoRaStatus = setupLoRa(NSS_PIN, RST_PIN, DIO0_PIN);
        }

        // 3. Bezpośrednie wyjście na silniki (PWM)
        xSemaphoreTake(mutexData, portMAX_DELAY);
        setThrottle(control.throttle);
        // Tutaj dodasz sterowanie serwem z rudder
        xSemaphoreGive(mutexData);

        vTaskDelay(10 / portTICK_PERIOD_MS); // 100Hz
    }
}

// ===================================================================
// RDZEŃ 0: LOGIC & MATH (Nawigacja, PID, Filtrowanie, DHT)
// ===================================================================
void codeLogic(void * pvParameters) {
    for(;;) {
        unsigned long now = millis();

        // 1. Aktualizacja danych GPS do telemetrii
        if (gps.location.isUpdated()) {
            xSemaphoreTake(mutexData, portMAX_DELAY);
            telemetry.GPSLat = gps.location.lat() * 10000000.0;
            telemetry.GPSLng = gps.location.lng() * 10000000.0;
            xSemaphoreGive(mutexData);
        }

        // 2. Czujnik DHT (rzadziej)
        if(now - lastDHTTime >= DHT_INTERVAL) {
            lastDHTTime = now;
            float t = dht.readTemperature();
            float h = dht.readHumidity();
            xSemaphoreTake(mutexData, portMAX_DELAY);
            telemetry.DHTTemp = (int32_t)(t * 10.0f);
            telemetry.DHTHumid = (int32_t)(h * 10.0f);
            telemetry.boatTemp = temperatureRead(); // Wbudowany czujnik ESP
            xSemaphoreGive(mutexData);
        }

        // 3. Nawigacja i PID (Ciężka matematyka)
        #ifndef DEBUG
        xSemaphoreTake(mutexData, portMAX_DELAY);
        // Kopia danych do lokalnych obliczeń, by nie trzymać Mutexa za długo
        routeData localRoute = route; 
        telemetryData localTel = telemetry;
        xSemaphoreGive(mutexData);

        if(localRoute.pointsCount > 0) {
            navigation.update();
            double targetLat = (double)localRoute.waypoints[0].lat / 10000000.0;
            double targetLng = (double)localRoute.waypoints[0].lng / 10000000.0;

            Setpoint = TinyGPSPlus::courseTo(localTel.GPSLat/10000000.0, localTel.GPSLng/10000000.0, targetLat, targetLng);
            Input = navigation.getHeading();
            
            double headingError = Setpoint - Input;
            if (headingError > 180) headingError -= 360;
            if (headingError < -180) headingError += 360;
            
            Input = headingError;
            Setpoint = 0; 
            myPID.Compute();
            
            // Wynik z PID (Output) zapisujemy do sterowania
            xSemaphoreTake(mutexData, portMAX_DELAY);
            control.rudder = (int16_t)Output; 
            xSemaphoreGive(mutexData);
        }
        #endif

        vTaskDelay(20 / portTICK_PERIOD_MS); // 50Hz
    }
}

// ===================== Konfiguracja ======================
void setup(){
    Serial.begin(115200);
    
    // Inicjalizacja Mutexa
    mutexData = xSemaphoreCreateMutex();

    gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
    
    myPID.SetMode(AUTOMATIC);
    myPID.SetOutputLimits(-45, 45);

    ledcSetup(0, 500, 8); 
    ledcAttachPin(PWM_PIN, 0);

    #ifndef DEBUG
    navigation.begin();
    #endif

    LoRaStatus = setupLoRa(NSS_PIN, RST_PIN, DIO0_PIN);
    dht.begin();

    // Wyłączamy radio WiFi/BT, aby zwolnić zasoby na Core 0
    WiFi.mode(WIFI_OFF);
    btStop();

    // Start zadań na osobnych rdzeniach
    xTaskCreatePinnedToCore(codeLogic, "TaskLogic", 10000, NULL, 1, &TaskLogic, 0);
    xTaskCreatePinnedToCore(codeHardware, "TaskHardware", 10000, NULL = 1, &TaskHardware, 1);
}

void loop(){
    // loop() jest pusty, zadania żyją w swoich Taskach
    vTaskDelete(NULL); 
}