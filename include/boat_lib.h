#include <cstdint>
#include <ArduinoJson.h>
#include <IPAddress.h>
#include <cstddef>
#include <SPI.h> 
#include <LoRa.h> 

// ==================Definicje Globalne===================
#define TELEMETRY_INTERVAL_MS 1000
#define CONTROL_INTERVAL_MS 100

#define NSS_PIN  5
#define RST_PIN  14
#define DIO0_PIN 33

#define PWM_RESOULTION 8
// ==================Struktury danych===================
struct __attribute__((packed)) telemetryData {
    int8_t serverTemp;     
    int8_t boatTemp; 
    int8_t boatRssi;  
    int8_t PT100;   
    int32_t GPSLat;
    int32_t GPSLng;     
    int32_t DHTTemp;           
    int32_t DHTHumid;    
};

struct __attribute__((packed)) Waypoint {
    int32_t lat; 
    int32_t lng; 
};

struct __attribute__((packed)) controlData {
    uint16_t rudder;      
    uint16_t throttle;
};

struct __attribute__((packed)) routeData {
    uint8_t pointsCount;  
    Waypoint waypoints[30];
};

struct deviceCredentials{
    IPAddress ip;
    uint16_t port;
    bool connected;
};

enum dataType {
    TELEMETRY,
    CONTROL,
    ROUTE,
    CONNECT
};

enum deviceType {
    ANDROID,
    SERVER,
    UNKONWN
};

enum PacketID : uint8_t {
    ID_TELEMETRY = 1,
    ID_CONTROL = 2,
    ID_ROUTE = 3
};
// ==================Definicje globalne===================
const uint8_t BROADCAST_ADDRESS = 0xFF;
const uint8_t BOAT_ADDRESS = 0xAA;
const uint8_t SERVER_ADDRESS = 0xBB;

uint8_t rxBuffer[256];      // Bufor na odebrane bajty
uint8_t rxLength = 0;       // Zapisana długość odebranej wiadomości

bool newDataReady = false;  // Flaga informująca, że czeka nowa wiadomość
uint8_t LoRaReconnectAttempts = 0; 

PacketID packetId;

telemetryData telemetry; // Struktura przechowująca dane telemetryczne
controlData control; // Struktura przechowująca dane sterujące
routeData route; // Struktura przechowująca dane trasy
// ==================Konfiguracja LoRa===================
/**
 * @brief Funkcja odpowiedzialna za obsługę odebranych wiadomości LoRa.
 * Wywoływana automatycznie przez bibliotekę LoRa po odebraniu pakietu.
 * 
 * @param packetSize 
 */
void onReceive(int packetSize) {
    if (packetSize == 0) return;          // if there's no packet, return

    // read packet header bytes:
    byte recipient = LoRa.read();        // recipient address
    byte sender = LoRa.read();           // sender address
    byte receivedPacketId = LoRa.read(); // check if the recipient is the device itself or a broadcast message
    byte incomingLength = LoRa.read();   // incoming packet length

    int i = 0;
    while (LoRa.available() && i < sizeof(rxBuffer)) {
        rxBuffer[i] = LoRa.read();
        i++;
    }

    if (incomingLength != i) {   // check length for error
        Serial.println("Blad: ucieta ramka LoRa!");
        Serial.println("Oczekiwano bajtow: " + String(incomingLength) + ", odebrano bajtow: " + String(i));
        return;                             
    }
    packetId = static_cast<PacketID>(receivedPacketId);
    rxLength = incomingLength;
    newDataReady = true;
    //Serial.println("Received from: 0x" + String(sender, HEX));
}
/**
 * @brief Funkcja odpowiedzialna za konfigurację modułu LoRa.
 * 
 * @return `true` jeśli inicjalizacja LoRa zakończyła się sukcesem, `false` w przypadku błędu.
 */
bool setupLoRa(uint8_t ssPin = 5, uint8_t rstPin = 14, uint8_t irqPin = 33) {
    LoRa.setPins(ssPin, rstPin, irqPin);

    if (!LoRa.begin(868E6)) {
        Serial.println("Błąd inicjalizacji LoRa! Sprawdź podłączenie SPI.");
        return false;
    }
    LoRa.enableCrc();
    //LoRa.setCodingRate4(8);
    LoRa.onReceive(onReceive);
    //LoRa.setSpreadingFactor(9); // Jeśli będzie bardzo przerywać, włącz to || wolniejszy przesył danych
    Serial.println("LoRa zainicjalizowana pomyślnie.");
    return true;
}
/**
 * @brief Funkcja sprawdzająca, czy moduł LoRa aktualnie odbiera dane.
 * 
 * @return true 
 * @return false 
 */
bool isReceiving() {
// Rozpoczęcie transakcji SPI (parametry zgodne z modułem SX1276)
    SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
    digitalWrite(NSS_PIN, LOW); // Wybór układu (Chip Select)
    
    // Wysłanie adresu rejestru RegModemStat (0x18). 
    // W układach Semtech bit nr 7 ustawiony na 0 oznacza operację ODCZYTU.
    SPI.transfer(0x18 & 0x7F); 
    
    // Pobranie zawartości rejestru (wysyłamy puste 0x00, aby wygenerować zegar SCK)
    uint8_t modemStat = SPI.transfer(0x00);
    
    digitalWrite(NSS_PIN, HIGH); // Koniec komunikacji z układem
    SPI.endTransaction();
    
    // Bit 2 (RxOngoing) mówi o tym, czy moduł właśnie zasysa dane z eteru
    bool rxOngoing = (modemStat & 0x04) != 0; 
    // Bit 1 (SignalSynchronized) mówi o namierzeniu preambuły
    bool signalSync = (modemStat & 0x02) != 0;
    
    return rxOngoing || signalSync;
}
// ==================Funkcje Daleki zasięg===================
/**
 * @brief Funkcja odpowiedzialna za wysyłanie danych telemetrycznych przez LoRa.
 * 
 * @param destinationAddress Adres docelowy 
 * @param senderAddress Adres nadawcy
 * @param telemetry Dane telemetryczne do wysłania
 */
void sendMessage(uint8_t destinationAddress, uint8_t senderAddress, const telemetryData& telemetry) {
    LoRa.beginPacket();                   // start packet
    LoRa.write(destinationAddress);           // add destination address
    LoRa.write(senderAddress);             // add sender address
    LoRa.write(PacketID::ID_TELEMETRY); // add packet type
    LoRa.write(sizeof(telemetryData));        // add payload length
    LoRa.write((const uint8_t*)&telemetry, sizeof(telemetryData));                 // add payload
    LoRa.endPacket();                     // finish packet and send it
}
/**
 * @brief Funkcja odpowiedzialna za wysyłanie danych sterujących przez LoRa.
 * 
 * @param destinationAddress 
 * @param senderAddress 
 * @param control 
 */
void sendMessage(uint8_t destinationAddress, uint8_t senderAddress, const controlData& control) {
    LoRa.beginPacket();                   // start packet
    LoRa.write(destinationAddress);           // add destination address
    LoRa.write(senderAddress);             // add sender address
    LoRa.write(PacketID::ID_CONTROL); // add packet type
    LoRa.write(sizeof(controlData));        // add payload length
    LoRa.write((const uint8_t*)&control, sizeof(controlData));                 // add payload
    LoRa.endPacket();                     // finish packet and send it
}
/**
 * @brief Funkcja odpowiedzialna za wysyłanie danych trasy przez LoRa.
 * 
 * @param destinationAddress 
 * @param senderAddress 
 * @param route 
 */
void sendMessage(uint8_t destinationAddress, uint8_t senderAddress, const routeData& route) {
    size_t payloadSize = sizeof(route.pointsCount) + (route.pointsCount * sizeof(Waypoint));
    LoRa.beginPacket();                   // start packet
    LoRa.write(destinationAddress);           // add destination address
    LoRa.write(senderAddress);             // add sender address
    LoRa.write(PacketID::ID_ROUTE); // add packet type
    LoRa.write(payloadSize);        // add payload length
    LoRa.write((const uint8_t*)&route, payloadSize);                 // add payload
    LoRa.endPacket();                     // finish packet and send it
}
/**
 * @brief Funkcja odpowiedzialna za wysyłanie danych sterujących przez LoRa.
 * 
 * @param expectedPacketId 
 * @return true 
 * @return false 
 */
bool decodeMessage(PacketID PacketId) {
    switch (PacketId) {
        case PacketID::ID_CONTROL:
            if (rxLength == sizeof(controlData)) {
                memcpy(&control, rxBuffer, sizeof(controlData));
                telemetry.boatRssi = LoRa.packetRssi(); 
                return true;
            } else {
                Serial.print("Blad dekodowania sterowania! Oczekiwano: ");
                Serial.print(sizeof(controlData));
                Serial.print(" bajtow, odebrano: ");
                Serial.print(rxLength);
                Serial.println(" bajtow.");
                return false;
            }

        case PacketID::ID_TELEMETRY:
            if (rxLength == sizeof(telemetryData)) {
                memcpy(&telemetry, rxBuffer, sizeof(telemetryData));
                return true;
            } else {
                Serial.print("Blad dekodowania telemetrii! Oczekiwano: ");
                Serial.print(sizeof(telemetryData));
                Serial.print(" bajtow, odebrano: ");
                Serial.print(rxLength);
                Serial.println(" bajtow.");
                return false;
            }

        case PacketID::ID_ROUTE: { 
            // Wyciągamy pierwszy bajt (ilość punktów) z odebranego bufora
            uint8_t receivedPointsCount = rxBuffer[0]; 
            
            // Obliczamy, ile fizycznie powinna ważyć paczka
            size_t expectedSize = sizeof(route.pointsCount) + (receivedPointsCount * sizeof(Waypoint));

            // Sprawdzamy, czy waga się zgadza i czy nie wykracza poza rozmiar struktury
            if (rxLength == expectedSize && expectedSize <= sizeof(routeData)) {
                memcpy(&route, rxBuffer, rxLength);
                Serial.println("Zaladowano nowa trase GPS! Punktow: " + String(receivedPointsCount));
                return true;
            } else {
                Serial.print("Blad dekodowania trasy! Oczekiwano: ");
                Serial.print(expectedSize);
                Serial.print(" bajtow, odebrano: ");
                Serial.print(rxLength);
                Serial.println(" bajtow.");
                return false;
            }
        }

        default:
            Serial.println("Nieznany typ pakietu do dekodowania.");
            return false;
    }
}
