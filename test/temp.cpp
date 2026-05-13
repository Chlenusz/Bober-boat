#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>

Adafruit_MPU6050 mpu;

void setup(void) {
  Serial.begin(115200);

  // Inicjalizacja I2C na pinach ESP32
  if (!mpu.begin()) {
    Serial.println("Nie znaleziono ukladu MPU6050!");
    while (1) { delay(10); }
  }

  Serial.println("MPU6050 znaleziony!");

  // Konfiguracja zakresów (optymalna dla łodzi)
  mpu.setAccelerometerRange(MPU6050_RANGE_2_G);
  mpu.setGyroRange(MPU6050_RANGE_250_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  delay(100);
}

void loop() {
  /* Pobranie nowych danych */
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  /* Wyswietlenie wynikow */
  Serial.print("Accel X: "); Serial.print(a.acceleration.x);
  Serial.print(", Y: "); Serial.print(a.acceleration.y);
  Serial.print(", Z: "); Serial.print(a.acceleration.z);
  Serial.println(" m/s^2");

  Serial.print("Gyro X: "); Serial.print(g.gyro.x);
  Serial.print(", Y: "); Serial.print(g.gyro.y);
  Serial.print(", Z: "); Serial.print(g.gyro.z);
  Serial.println(" rad/s");

  delay(200);
}