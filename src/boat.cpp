#include <SPI.h> 
#include <LoRa.h> 
#include "boat_lib.h"

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

void onReceive(int packetSize);

// ==================Zmienne globalne===================
deviceCredentials serverDevice; // Struktura przechowująca dane serwera (adres IP, port, status połączenia)

telemetryData telemetry; // Struktura przechowująca dane telemetryczne
controlData control; // Struktura przechowująca dane sterujące

unsigned long currentTime = 0;
unsigned long lastTelemetryTime = 0;

bool longRangeCommunication = false;

uint8_t rxBuffer[256];      // Bufor na odebrane bajty
uint8_t rxLength = 0;       // Zapisana długość odebranej wiadomości
bool newDataReady = false;  // Flaga informująca, że czeka nowa wiadomość
byte msgCount = 0;            // count of outgoing messages

// ==============Konfiguracja LoRa================
/**
 * @brief Funkcja odpowiedzialna za konfigurację modułu LoRa.
 * 
 * @return `true` jeśli inicjalizacja LoRa zakończyła się sukcesem, `false` w przypadku błędu.
 */
bool setupLoRa() {
    LoRa.setPins(NSS_PIN, RST_PIN, DIO0_PIN);

    if (!LoRa.begin(868E6)) {
        Serial.println("Błąd inicjalizacji LoRa! Sprawdź podłączenie SPI.");
        return false;
    }
    LoRa.enableCrc();
    LoRa.setCodingRate4(8);
    LoRa.onReceive(onReceive);
    //LoRa.setSpreadingFactor(9); // Jeśli będzie bardzo przerywać, włącz to || wolniejszy przesył danych
    Serial.println("LoRa zainicjalizowana pomyślnie.");
    return true;
}
// ===============Komunikacja Daleki zasięg===============
/**
 * @brief Wysyła wiadomość przez LoRa.
 * 
 * @param outgoing String zawierający wiadomość do wysłania.
 */
void sendMessage() {
  LoRa.beginPacket();                   // start packet
  LoRa.write(SERVER_ADDRESS);           // add destination address
  LoRa.write(BOAT_ADDRESS);             // add sender address
  LoRa.write(msgCount);                 // add message ID
  LoRa.write(sizeof(telemetryData));        // add payload length
  LoRa.write((const uint8_t*)&telemetry, sizeof(telemetryData));                 // add payload
  LoRa.endPacket();                     // finish packet and send it
  msgCount++;                           // increment message ID
}
/**
 * @brief Funkcja odpowiedzialna za obsługę odebranych wiadomości LoRa.
 * Wywoływana automatycznie przez bibliotekę LoRa po odebraniu pakietu.
 * 
 * @param packetSize 
 */
void onReceive(int packetSize) {
  if (packetSize == 0) return;          // if there's no packet, return

  // read packet header bytes:
  int recipient = LoRa.read();          // recipient address
  byte sender = LoRa.read();            // sender address
  byte incomingMsgId = LoRa.read();     // incoming msg ID
  byte incomingLength = LoRa.read();    // incoming msg length

  int i = 0;
  while (LoRa.available() && i < sizeof(rxBuffer)) {
    rxBuffer[i] = LoRa.read();
    i++;
  }

  if (incomingLength != i) {   // check length for error
    Serial.println("error: message length does not match length");
    return;                             // skip rest of function
  }

  // if the recipient isn't this device or broadcast,
  if (recipient != BOAT_ADDRESS && recipient != BROADCAST_ADDRESS) {
    Serial.println("This message is not for me.");
    return;                             // skip rest of function
  }
  
  // if message is for this device, or broadcast, print details:
  Serial.println("Received from: 0x" + String(sender, HEX));
  rxLength = incomingLength;
  newDataReady = true;
  //Serial.println("Message length: " + String(incomingLength));
  //Serial.println("Message: " + incoming);
}
/**
 * @brief Dekoduje odebrane bajty z bufora do globalnej struktury sterującej (control).
 * * @return true jeśli rozmiar się zgadza i dekodowanie powiodło się, false w przypadku błędu.
 */
bool decodeMessage() {
    // Sprawdzamy, czy odebrana ilość bajtów pasuje do rozmiaru struktury controlData
    if (rxLength == sizeof(controlData)) {
        // Kopiujemy bajty z bufora rxBuffer prosto do pamięci struktury 'control'
        memcpy(&control, rxBuffer, sizeof(controlData));
        Serial.println("Wiadomosc zdekodowana: zaktualizowano dane sterujace.");
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
}
// ===================== Wstępna konfiguracja ======================
void setup(){
    Serial.begin(115200);
    Serial.println("");
    Serial.println("Setup zakończony");
    longRangeCommunication = setupLoRa();
    Serial.println("LoRa setup: " + String(longRangeCommunication ? "sukces" : "niepowodzenie"));
}
// ===================== Główna pętla programu =====================
void loop(){
    currentTime = millis();
    if (longRangeCommunication){
        if(currentTime - lastTelemetryTime >= TELEMETRY_INTERVAL_MS) {
            lastTelemetryTime = currentTime;
            sendMessage();
        }
        LoRa.receive();
    }else {
        longRangeCommunication = setupLoRa();
    }

    if (newDataReady) {
        newDataReady = false; // Reset flag after processing
        decodeMessage();
    }

}



