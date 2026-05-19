#include "boat_lib.h"
#include "Navigation.h"
#include <ESP32Servo.h>
#include "DHT.h"
#include <TinyGPSPlus.h>
#include <HardwareSerial.h>
#include <PID_v1.h>

// ==================Definicje Globalne===================
#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22

#define MOTOR_PIN 32
#define MOTOR_STATE_PIN 25

#define SERVO_PIN 21

#define DTH_PIN 26
#define DHT_INTERVAL 5000
#define DHTTYPE DHT11

#define GPS_RX_PIN 16 
#define GPS_TX_PIN 17 
#define GPS_BAUD 9600



#define DEBUG

// ==================Zmienne globalne===================
unsigned long currentTime = 0;
unsigned long lastTelemetryTime = 0;
unsigned long lastDHTTime = 0;
unsigned long lastControlTime = 0;

double distanceToTarget = 0.0;
double course = 0.0;

Servo servo;

bool LoRaStatus = false;

double Setpoint, Input, Output;
double Kp=2.0, Ki=0.5, Kd=1.0;
PID myPID(&Input, &Output, &Setpoint, Kp, Ki, Kd, DIRECT);

DHT dht(DTH_PIN, DHTTYPE); // Inicjalizacja czujnika DHT11
TinyGPSPlus gps; // Inicjalizacja obiektu TinyGPSPlus do obsługi GPS
HardwareSerial gpsSerial(2); // Inicjalizacja sprzętowego UART2 na wybranych pinach
#ifndef DEBUG
NavigationSystem navigation; // Inicjalizacja systemu nawigacji
#endif

// ===================== Funkcje Lokalne ==========================
void setThrottle(uint16_t value) {
    ledcWrite(0, value); 
    digitalWrite(MOTOR_STATE_PIN, value > 0 ? HIGH : LOW); 
}

void setRudder(uint16_t value) {
    servo.write(value);// Ustawienie wartości PWM na podstawie wartości skrętu zakres: 0-180
    Serial.println("Ustawianie skrętu na wartość: " + String(value));
}
#ifndef DEBUG
void computeWaypoints() {
    navigation.update(); // Aktualizacja danych nawigacyjnych

    Setpoint = TinyGPSPlus::courseTo(telemetry.GPSLat, telemetry.GPSLng, route.waypoints[0].lat, route.waypoints[0].lng);
    Input = navigation.getHeading();
    double headingError = Setpoint - Input;

    if (headingError > 180) headingError -= 360;
    if (headingError < -180) headingError += 360;
    Input = headingError;
    Setpoint = 0; // Chcemy, aby błąd wynosił 0
    myPID.Compute();
    // sterowanie serwem
    //rudder = 90+output; // Przykładowa konwersja z PID na kąt steru
}
#endif

// ===================== Wstępna konfiguracja ======================
void setup(){
    Serial.begin(115200);
    Serial.println("");
    Serial.println("Setup zakończony");

    gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

    myPID.SetMode(AUTOMATIC);
    myPID.SetOutputLimits(-45, 45);

    servo.attach(SERVO_PIN);

    pinMode(MOTOR_STATE_PIN, OUTPUT);
    ledcSetup(0,20000, 8);
    ledcAttachPin(MOTOR_PIN, 0);

    #ifndef DEBUG
    navigation.begin(); // Inicjalizacja systemu nawigacji
    #endif

    LoRaStatus = setupLoRa(NSS_PIN, RST_PIN, DIO0_PIN);
    Serial.println("LoRa setup: " + String(LoRaStatus ? "sukces" : "niepowodzenie"));
    packetId = PacketID::ID_CONTROL;
    dht.begin(); 
}
// ===================== Główna pętla programu =====================
void loop(){
    currentTime = millis();

    //Konwersja waypointów z formatu int32_t do double dla pierwszego punktu docelowego
    if(route.pointsCount > 0){
        double targetLat = (double)route.waypoints[0].lat / 10000000.0;
        double targetLng = (double)route.waypoints[0].lng / 10000000.0;
    }

    //computeWaypoints();

    while (gpsSerial.available() > 0) {
        gps.encode(gpsSerial.read());
    }

    if (gps.location.isUpdated()) {
        telemetry.GPSLat = gps.location.lat()*10000000.0;
        telemetry.GPSLng = gps.location.lng()*10000000.0;
        Serial.println("Aktualizacja danych GPS: Lat = " + String(telemetry.GPSLat/10000000.0, 6) + ", Lng = " + String(telemetry.GPSLng/10000000.0, 6));
    }

    if (LoRaStatus){
        if(currentTime - lastTelemetryTime >= TELEMETRY_INTERVAL_MS) {
            lastTelemetryTime = currentTime;
            telemetry.boatTemp = temperatureRead();
            telemetry.boatRssi = LoRa.packetRssi();
            sendMessage(SERVER_ADDRESS, BOAT_ADDRESS, telemetry);
            LoRa.receive();
        }
        
    }else {
        if(LoRaReconnectAttempts <5){LoRaStatus = setupLoRa();}
        if(!(LoRaReconnectAttempts>10)){
            LoRaReconnectAttempts++;
            Serial.println("Nie można połączyć się z LoRa! Próba ponownego połączenia: " + String(LoRaReconnectAttempts));
        }

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
        Serial.println("Aktualizacja sterowania: " + String(control.throttle)+", " + String(control.rudder));
        setRudder(control.rudder);
        setThrottle(control.throttle);
    }

    if(currentTime - lastDHTTime >= DHT_INTERVAL) {
        lastDHTTime = currentTime;
        telemetry.DHTTemp = dht.readTemperature();
        telemetry.DHTHumid = dht.readHumidity();
    }
}