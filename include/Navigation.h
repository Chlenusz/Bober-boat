#ifndef NAVIGATION_H
#define NAVIGATION_H

#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <QMC5883LCompass.h>
#include <MadgwickAHRS.h>

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
    bool begin() {
        Wire.begin(); 
        
        if (!mpu.begin()) {
            Serial.println("Nie znaleziono MPU6050!");
            return false;
        }
        mpu.setAccelerometerRange(MPU6050_RANGE_2_G);
        // Żyroskop na największą czułość (250 stopni/s) - idealne dla powolnej łódki
        mpu.setGyroRange(MPU6050_RANGE_250_DEG); 
        mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

        compass.init();
        
        // Inicjalizacja filtru Madgwicka (szacowana częstotliwość próbkowania ok. 100 Hz)
        filter.begin(100); 
        lastUpdate = micros();
        
        Serial.println("System Nawigacji (Madgwick AHRS) zainicjowany.");
        return true;
    }

    // Ta funkcja MUSI być wywoływana w głównej pętli loop() jak najczęściej
    void update() {
        unsigned long now = micros();
        
        // Wykonujemy aktualizację co 10 000 mikrosekund (10 ms -> 100 Hz)
        if (now - lastUpdate >= 10000) {
            lastUpdate = now;

            // 1. Pobranie danych z MPU6050 (Akcelerometr i Żyroskop)
            sensors_event_t a, g, temp;
            mpu.getEvent(&a, &g, &temp);

            // 2. Pobranie danych z kompasu
            compass.read();
            float magX = compass.getX();
            float magY = compass.getY();
            float magZ = compass.getZ();

            // 3. Konwersja żyroskopu: Biblioteka Adafruit zwraca radiany/s, 
            // a filtr Madgwicka wymaga stopni/s. (1 radian = ~57.2958 stopni)
            float gyroX_deg = g.gyro.x * 57.2958f;
            float gyroY_deg = g.gyro.y * 57.2958f;
            float gyroZ_deg = g.gyro.z * 57.2958f;

            // 4. Magia fuzji czujników (Sensor Fusion)
            // Podajemy: żyroskop, akcelerometr oraz magnetometr do filtru
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

    // Zwraca gładki, skompensowany kurs łodzi
    float getHeading() {
        return currentHeading;
    }
    
    // Dodatkowe funkcje (gdybyś chciał robić alarm wywrotki łodzi)
    float getRoll() { return filter.getRoll(); }
    float getPitch() { return filter.getPitch(); }
};

#endif