#include <cstdint>
#include <ArduinoJson.h>
#include <IPAddress.h>
#include <cstddef>
#include <SPI.h> 
#include <LoRa.h> 

// ==================Definicje Globalne===================
#define TELEMETRY_INTERVAL_MS 5000
#define CONTROL_INTERVAL_MS 100

#define NSS_PIN  5
#define RST_PIN  14
#define DIO0_PIN 33

#define PWM_RESOULTION 8
// ==================Struktury danych===================
struct __attribute__((packed)) telemetryData {
    int8_t serverTemp;     
    int8_t boatTemp; 
    int boatRssi;  
    int16_t sens1;         
    int16_t sens2;         
    float sens3;           
    float sens4;          
};

struct __attribute__((packed)) controlData {
    uint16_t rudder;      
    uint16_t throttle;        
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
// ==================Definicje globalne===================
const uint8_t BROADCAST_ADDRESS = 0xFF;
const uint8_t BOAT_ADDRESS = 0xAA;
const uint8_t SERVER_ADDRESS = 0xBB;

byte msgCount = 0;  
uint8_t rxBuffer[256];      // Bufor na odebrane bajty
uint8_t rxLength = 0;       // Zapisana długość odebranej wiadomości

bool newDataReady = false;  // Flaga informująca, że czeka nowa wiadomość

PacketID packetId;

telemetryData telemetry; // Struktura przechowująca dane telemetryczne
controlData control; // Struktura przechowująca dane sterujące
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
  byte recipient = LoRa.read();          // recipient address
  byte sender = LoRa.read();            // sender address
  byte incomingMsgId = LoRa.read();     // incoming msg ID
  byte incomingLength = LoRa.read();    // incoming msg length

  int i = 0;
  while (LoRa.available() && i < sizeof(rxBuffer)) {
    rxBuffer[i] = LoRa.read();
    i++;
  }

  if (incomingLength != i) {   // check length for error
    return;                             
  }
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
    //LoRa.enableCrc();
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
  LoRa.write(msgCount);                 // add message ID
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
  LoRa.write(msgCount);                 // add message ID
  LoRa.write(sizeof(controlData));        // add payload length
  LoRa.write((const uint8_t*)&control, sizeof(controlData));                 // add payload
  LoRa.endPacket();                     // finish packet and send it
}
/**
 * @brief Funkcja odpowiedzialna za wysyłanie danych sterujących przez LoRa.
 * 
 * @param expectedPacketId 
 * @return true 
 * @return false 
 */
bool decodeMessage(PacketID expectedPacketId) {
    if (PacketID::ID_CONTROL == expectedPacketId) {

        // Sprawdzamy, czy odebrana ilość bajtów pasuje do rozmiaru struktury controlData
        if (rxLength == sizeof(controlData)) {
            // Kopiujemy bajty z bufora rxBuffer prosto do pamięci struktury 'control'
            memcpy(&control, rxBuffer, sizeof(controlData));
            telemetry.boatRssi = LoRa.packetRssi(); // Aktualizacja wartości RSSI na podstawie funkcji biblioteki LoRa
            //Serial.println("Wiadomosc zdekodowana: zaktualizowano dane sterujace.");
            return true;
        } else {
            // Jeśli długość się nie zgadza, wypisujemy błąd i ułatwiamy diagnozę
            Serial.print("Blad dekodowania! Oczekiwano: ");
            Serial.print(sizeof(controlData));
            Serial.print(" bajtow, odebrano: ");
            Serial.print(rxLength);
            Serial.println(" bajtow.");
            return false;
        }
    } else if(PacketID::ID_TELEMETRY == expectedPacketId) {
        // Sprawdzamy, czy odebrana ilość bajtów pasuje do rozmiaru struktury telemetryData
        if (rxLength == sizeof(telemetryData)) {
            // Kopiujemy bajty z bufora rxBuffer prosto do pamięci struktury 'telemetry'
            memcpy(&telemetry, rxBuffer, sizeof(telemetryData));
            //Serial.println("Wiadomosc zdekodowana: zaktualizowano dane telemetryczne.");
            return true;
        } else {
            // Jeśli długość się nie zgadza, wypisujemy błąd i ułatwiamy diagnozę
            Serial.print("Blad dekodowania! Oczekiwano: ");
            Serial.print(sizeof(telemetryData));
            Serial.print("bajtow, odebrano: ");
            Serial.print(rxLength);
            Serial.println(" bajtow.");
            return false;
        }
    } else {
        Serial.println("Nieznany typ pakietu do dekodowania.");
        return false;
    }
}