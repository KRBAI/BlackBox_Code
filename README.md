# AXEL: Smart Vehicular Black Box & Driving Assistant

AXEL is an IoT-enabled vehicular black box and active safety suite built on the ESP32-S3 microcontroller. It bridges the gap between passive post-crash forensic logging and real-time active driver assistance.

## Key Features
* **Accident & Rollover Detection:** High-frequency MPU6050 polling detects high-G impacts and critical rollover angles.
* **Automated SOS Alerts:** Fallback SIM800L module automatically transmits ultra-short SMS emergency alerts with precise Google Maps coordinates if a crash occurs.
* **Proactive Blindspot Monitoring:** Dual ultrasonic sensors warn the driver of lateral hazards via an OLED dashboard and audible buzzer.
* **Edge-to-Cloud Telemetry:** Synchronizes continuous flight data (speed, location, G-force) to a Firebase Realtime Database while maintaining local offline backups in the FFat file system.
* **Multi-Core Architecture:** Utilizes FreeRTOS to handle blocking network tasks on Core 0 while keeping critical safety and display routines uninterrupted on Core 1.

## Hardware Stack
* **Master Controller:** ESP32-S3 Dual-Core
* **Blindspot Sub-node:** ESP32-C3
* **Kinematic Sensor:** MPU-6050 (Tri-axial Accelerometer & Gyroscope)
* **Location & Comms:** Ublox NEO-6M GPS & SIM800L GPRS/GSM Transceiver
* **Proximity Sensors:** 2x JSN-SR04T (Waterproof Ultrasonic)
* **Display:** SSD1306 0.96-inch OLED

## Software Dependencies
To compile this project in the Arduino IDE, install the following libraries:
* `U8g2` (for OLED graphics)
* `Adafruit MPU6050` & `Adafruit Unified Sensor`
* `TinyGPSPlus`
* `FirebaseClient` (by Mobizt)

## Setup Instructions
1. Clone this repository.
2. Open `BlackBox_1_0.ino` in the Arduino IDE.
3. In `Config.h`, update the following parameters with your credentials:
   * `ROUTER_SSID` and `ROUTER_PASS`
   * `FIREBASE_URL` and `FIREBASE_API_KEY`
   * `PHONE_NUMBER` (for emergency SMS routing)
4. Compile and upload to your ESP32-S3. Ensure the partition scheme is set to support FFat (e.g., *16MB Flash, 2MB APP/2MB FATFS*).

## System Architecture
* **Core 0:** Wi-Fi connection, Firebase syncing, SIM800L AT-command sequencing, and blindspot polling.
* **Core 1:** IMU sensor reading, OLED display rendering, UI state machine, and audio buzzer control.
