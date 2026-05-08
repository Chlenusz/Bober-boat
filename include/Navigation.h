#ifndef NAVIGATION_H
#define NAVIGATION_H

#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <QMC5883LCompass.h>
#include <MadgwickAHRS.h>
/** @brief Navigation System class */
class NavigationSystem {
private:
    Adafruit_MPU6050 mpu;
    QMC5883LCompass compass;
    Madgwick filter;
    
    unsigned long lastUpdate;
    float currentHeading = 0.0;
    
    // Deklinacja magnetyczna (dla Polski ok. +5.5 stopnia)
    const float declinationAngle = 5.5; 

public:
    /**
     * @brief Initialize the navigation system
     * 
     * @return true | false
     */
    bool begin() {
        Wire.begin(); 
        
        if (!mpu.begin()) {
            Serial.println("Nie znaleziono MPU6050!");
            return false;
        }
        mpu.setAccelerometerRange(MPU6050_RANGE_2_G);
        mpu.setGyroRange(MPU6050_RANGE_250_DEG); 
        mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

        compass.init();
        
        // Inicjalizacja filtru Madgwicka
        filter.begin(100); 
        lastUpdate = micros();
        
        Serial.println("System Nawigacji (Madgwick AHRS) zainicjowany.");
        return true;
    }
    /**
     * @brief Update the navigation system
     * 
     */
    void update() {
        unsigned long now = micros();
        
        // Wykonujemy aktualizację co 10 ms (100 Hz)
        if (now - lastUpdate >= 10000) {
            lastUpdate = now;

            // 1. Pobranie danych z MPU6050 
            sensors_event_t a, g, temp;
            mpu.getEvent(&a, &g, &temp);

            // 2. Pobranie danych z kompasu
            compass.read();
            float magX = compass.getX();
            float magY = compass.getY();
            float magZ = compass.getZ();

            // 3. Konwersja żyroskopu: Biblioteka Adafruit zwraca radiany/s, 
            // a filtr Madgwicka wymaga stopni/s.
            float gyroX_deg = g.gyro.x * 57.2958f;
            float gyroY_deg = g.gyro.y * 57.2958f;
            float gyroZ_deg = g.gyro.z * 57.2958f;

            // 4. Aktualizujemy filtr Madgwicka z aktualnymi danymi
            filter.update(
                gyroX_deg, gyroY_deg, gyroZ_deg, 
                a.acceleration.x, a.acceleration.y, a.acceleration.z, 
                magX, magY, magZ
            );
            
            // 5. Pobranie gotowego azymutu i korekta o deklinację
            currentHeading = filter.getYaw() + declinationAngle;
            
            // Normalizacja do 0-360
            if (currentHeading < 0.0) currentHeading += 360.0;
            if (currentHeading > 360.0) currentHeading -= 360.0;
        }
    }
    /**
     * @brief Get the Heading correction
     * 
     * @return float 
     */
    float getHeading() { return currentHeading;}
    /**
     * @brief Get the Roll object
     * 
     * @return float 
     */
    float getRoll() { return filter.getRoll(); }
    /**
     * @brief Get the Pitch object
     * 
     * @return float 
     */
    float getPitch() { return filter.getPitch(); }
};

#endif