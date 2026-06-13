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
// ROLLOVER DETECTION
// ==========================================
#define ROLLOVER_ANGLE_DEG      60.0f
#define ROLLOVER_SUSTAIN_MS     1500

// ==========================================
// TEMPERATURE MONITORING
// ==========================================
#define TEMP_ALERT_THRESHOLD_C  70.0f
#define TEMP_ALERT_SUSTAIN_MS   5000
#define TEMP_ALERT_COOLDOWN_MS  60000

// ==========================================
// NETWORK & CLOUD SETTINGS (HARDCODED)
// ==========================================

// --- 1. INTERNET CONNECTION ---
#define ROUTER_SSID "Pixel7pro"
#define ROUTER_PASS "krb@2903"

// --- 2. FIREBASE CREDENTIALS ---
#define FIREBASE_URL "https://axel-7e7e5-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_API_KEY "AIzaSyBvvH1ahhPmBqE2af32Ln0uIfOvzMfyQ4Y"

// --- 3. DEVICE & USER IDENTITY ---
#define USER_ID "E25zB1dcAmc9Mknc9d0QPjicgVm2"

// --- 4. CELLULAR DATA & SMS ---
#define PHONE_NUMBER "+94727238727" // Emergency contact
#define SIM_APN "dialogbb"          // Mobile operator APN (e.g. dialogbb, mobitelbb)

#endif