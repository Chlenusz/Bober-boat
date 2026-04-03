#include <cstdint>
#include <ArduinoJson.h>
#include <IPAddress.h>
#include <cstddef>

// ==================Struktury danych===================
struct __attribute__((packed)) telemetryData {
    int8_t boatTemperature;     
    int8_t boatTemp;       
    int16_t sens1;         
    int16_t sens2;         
    float sens3;           
    float sens4;          
    int16_t Rssi;          
};

struct __attribute__((packed)) controlData {
    float rudder;      
    float throttle;        
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
    BOAT,
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
const uint8_t BOAT_ADDRESS = 0x10;
const uint8_t SERVER_ADDRESS = 0x00;
// ==================Funkcje pomocnicze===================


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
    
    doc["BTemp"] = telemetry.boatTemperature;
    doc["rssi"] = telemetry.Rssi;

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

String undecodeMessage(String message){
    // Implementation for undecoding message
    return message;
}