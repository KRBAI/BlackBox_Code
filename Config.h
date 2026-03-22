#ifndef CONFIG_H
#define CONFIG_H

// ==========================================
// PIN DEFINITIONS (ESP32-S3)
// ==========================================

// --- 1. I2C SENSORS (MPU6050, Display) ---
#define SDA_PIN 8
#define SCL_PIN 9

// --- 2. GPS MODULE (UART 1) ---
#define GPS_RX_PIN 16 
#define GPS_TX_PIN 17 

// --- 3. SIM800L MODULE (UART 0) ---
#define SIM800_RX_PIN 5   
#define SIM800_TX_PIN 4   

// --- 4. TOUCH SENSOR ---
#define TOUCH_PIN 6

// --- 5. BUTTONS & AUDIO ---
#define BUZZER_PIN 7
#define SOS_PIN 10

// ==========================================
// SYSTEM SETTINGS
// ==========================================
#define LOG_INTERVAL 1000      

// ==========================================
// NETWORK & CLOUD SETTINGS
// ==========================================

// --- 1. ESP32 OFFLINE HOTSPOT (For Local Downloads) ---
// This is the network the ESP32 broadcasts if you want to connect a laptop to it.
#define WIFI_SSID "BlackBox_S3"
#define WIFI_PASS "12345678"

// --- 2. INTERNET CONNECTION (Your Phone's Hotspot) ---
// The ESP32 will connect to this to get internet access for Firebase.
#define ROUTER_SSID "Pixel7pro"
#define ROUTER_PASS "krb@2903"

// --- 3. FIREBASE CREDENTIALS ---
#define FIREBASE_URL "axel-7e7e5-default-rtdb.asia-southeast1.firebasedatabase.app/"
#define FIREBASE_API_KEY "AIzaSyBvvH1ahhPmBqE2af32Ln0uIfOvzMfyQ4Y"

// --- 4. DEVICE & USER IDENTITY ---
#define USER_ID "E25zB1dcAmc9Mknc9d0QPjicgVm2"

#endif