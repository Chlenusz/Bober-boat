#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_wifi.h>


#define TELEMETRY_INTERVAL_MS 500
#define RECONNECT_INTERVAL_MS 5000
#define PWM_FREQUENCY 10 
#define WIFI_CHANNEL 11
#define PORT 1883 // Port docelowy (zdalny)
#define UDP_AP true
#define DEBUG false
#define LONG_RANGE_MODE true

//PINOUT
#define PWM_PIN 26
#define SIDE_PIN 27
#define THROTTLE_PIN 25

// Ustawienia UDP
#define TARGET_IP "192.168.0.29" // ZMIEŃ: adres IP urządzenia odbierającego
#define LOCAL_UDP_PORT 4444       // Port lokalny nasłuchu dla ESP32


WiFiUDP udp; // Obiekt do obsługi UDP

void wifiSetup();
bool wifiConnect();
void wifiDisconnect();


IPAddress androidIP;
uint16_t androidPort = 0;
bool clientConnected = false;
const char* ssid = "Chlebak";
const char* password = "Chlebak1234";
bool wifi_state = false;
bool firstConnect = true;
String wifiName;
String wifiPass;

long rssi = 0;
int tempC = 0;

bool backwards = false;
bool forward = false;

bool left = false;
bool right = false;

float throttle = 0.0;
float horizontal = 0.0;

unsigned long previousMillis = 0;
unsigned long previousBurst = 0;

float intensity = 0.0;

// -------------------- FUNKCJA: konfiguracja i start serwera --------------------
void wifiSetup(bool debug, bool udp_ap = false) {
  WiFi.mode(WIFI_AP);
  WiFi.channel(11);
  bool ok = WiFi.softAP(ssid, password);
  WiFi.enableLongRange(LONG_RANGE_MODE);

  if (!ok) {
    Serial.println("Błąd: nie udało się uruchomić AP!");
    return;
  }
  IPAddress ip = WiFi.softAPIP();
  Serial.println("AP uruchomione!");
  Serial.println("SSID: " + String(ssid));
  Serial.println("IP: " + ip.toString());

  if (udp_ap) {
    udp.begin(LOCAL_UDP_PORT);
    Serial.printf("Nasłuch UDP uruchomiony na porcie: %d\n", LOCAL_UDP_PORT);
  }
}

bool wifiConnect() {
  Serial.println("Próba połączenia z siecią WiFi: " + wifiName);
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiName.c_str(), wifiPass.c_str());

  Serial.print("Łączenie z siecią WiFi");
  
  int timeout = 40;
  while (WiFi.status() != WL_CONNECTED && timeout > 0) {
    delay(500);
    Serial.print(".");
    timeout--;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Połączono! Adres IP: ");
    Serial.println(WiFi.localIP());
    
    // Uruchomienie gniazda UDP po pomyślnym podłączeniu do sieci
    udp.begin(LOCAL_UDP_PORT);
    Serial.printf("Nasłuch UDP uruchomiony na porcie: %d\n", LOCAL_UDP_PORT);
    
    return true;
  } else {
    Serial.println("Nie udało się połączyć z WiFi, powrót do trybu AP");
    WiFi.disconnect();
    wifiSetup(DEBUG);
    return false;
  }
}

void wifiDisconnect() {
  Serial.println("Wyłączanie Access Point...");
  WiFi.disconnect(true,true);
  delay(1000);  
  Serial.println(WiFi.status());
  WiFi.mode(WIFI_OFF);
  delay(1000);
}


// -------------------- FUNKCJE UDP --------------------
void publishTelemetry(bool udp_ap_mode = false) {
  if (!clientConnected) return; 


  String payload = "temp: " + String(tempC);
  
  // Wysyłamy unicastem dokładnie tam, skąd przyszła komenda
  udp.beginPacket(androidIP, androidPort);
  udp.print(payload);
  udp.endPacket();
  
  Serial.println("Wysłano pakiet UDP do Androida: " + payload);
}

void receiveUDP() {
  int packetSize = udp.parsePacket();
  if (packetSize) {
    // 1. Zapisanie IP i Portu telefonu
    androidIP = udp.remoteIP();
    androidPort = udp.remotePort();
    clientConnected = true;

    // 2. Odczyt danych
    char incomingPacket[255];
    int len = udp.read(incomingPacket, 255);
    if (len > 0) {
      incomingPacket[len] = '\0'; 
    }

    horizontal = String(incomingPacket).substring(0, String(incomingPacket).indexOf(';')).toFloat();
    throttle = String(incomingPacket).substring(String(incomingPacket).indexOf(';') + 1).toFloat();
  }

}

void sendUDP(const String& message) {
  if (!clientConnected) {
    Serial.println("Brak połączenia z klientem UDP. Nie można wysłać wiadomości.");
    return;
  }
  
  udp.beginPacket(androidIP, androidPort);
  udp.print(message);
  udp.endPacket();
  
  Serial.println("Wysłano pakiet UDP do Androida: " + message);
}

void setThrottle() {
  if (throttle > 0) {
    digitalWrite(THROTTLE_PIN, HIGH); // Przykładowo, LOW dla przodu
    ledcWrite(0,round(throttle * 255));
  } else if (throttle < 0) {
    digitalWrite(THROTTLE_PIN, LOW); // Przykładowo, HIGH dla tyłu
    throttle = -throttle;
    ledcWrite(0,round(throttle * 255));
  } else {
    digitalWrite(THROTTLE_PIN, LOW); // Brak ruchu
    ledcWrite(0, 0);
  }
}

void setHorizontal() {
  if (horizontal > 0) {
    digitalWrite(SIDE_PIN, HIGH);
  } else if (horizontal < 0) {
    digitalWrite(SIDE_PIN, LOW);
  }else {
    digitalWrite(SIDE_PIN, LOW);
  }
}

void isConnected() {
  // Jeśli jesteśmy w trybie AP i nie ma żadnego podłączonego urządzenia
  if (UDP_AP && WiFi.softAPgetStationNum() == 0 && clientConnected) {
      Serial.println("Rozłączono wszystkie urządzenia Wi-Fi!");
      clientConnected = false;
      setThrottle();
  }else if (UDP_AP && WiFi.softAPgetStationNum() > 0 && !clientConnected) {
      Serial.println("Nowe urządzenie Wi-Fi połączone!");
      clientConnected = true;
  }
}

// -------------------- FUNKCJA SETUP --------------------
void setup() {
  Serial.begin(115200);
  wifiSetup(DEBUG, UDP_AP);
  firstConnect = false;
  Serial.println("Setup zakończony");
  
  //pinout setup
  pinMode(SIDE_PIN, OUTPUT);
  pinMode(THROTTLE_PIN, OUTPUT);
  pinMode(PWM_PIN, OUTPUT);

  // Konfiguracja PWM
  ledcSetup(0, PWM_FREQUENCY, 8);
  ledcAttachPin(PWM_PIN, 0);
}

// -------------------- PĘTLA GŁÓWNA --------------------
void loop() {
  unsigned long currentMillis = millis();
  tempC = temperatureRead();
  
  // Wysyłanie telemetrii co 500 ms
  if (currentMillis - previousMillis >= TELEMETRY_INTERVAL_MS) {
    previousMillis = currentMillis;
    publishTelemetry(UDP_AP);
  }

  // Zmienianie wartości PWM co 100 ms, jeśli jest połączony klient
  if (((currentMillis - previousBurst) >= 1/PWM_FREQUENCY) && clientConnected) {
    previousBurst = currentMillis;
    setThrottle();
    setHorizontal();
  }

  if(!clientConnected){
    throttle = 0.0;
    setThrottle();
  }

  // Sprawdzanie, czy przyszły nowe pakiety UDP
  receiveUDP();

  // Sprawdzanie stanu połączenia Wi-Fi
  isConnected();
  
  // Logika ponownego łączenia
  if(!UDP_AP){
    if(WiFi.status() != WL_CONNECTED && !firstConnect){
    Serial.println("Utracono połączenie. Próba ponownego podłączenia do WiFi...");
    wifi_state = wifiConnect();
    Serial.println("Stan WiFi: " + String(wifi_state));
    }
  }
  
}