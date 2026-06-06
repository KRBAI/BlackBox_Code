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
// Angle (degrees) beyond which a roll or pitch is considered a rollover.
// 60° is conservative — adjust down to 45° for more sensitivity.
#define ROLLOVER_ANGLE_DEG      60.0f

// How long (ms) the angle must be sustained before triggering.
// Filters out brief hard cornering or speed bumps.
#define ROLLOVER_SUSTAIN_MS     1500

// ==========================================
// TEMPERATURE MONITORING
// ==========================================
// MPU6050 die temperature alert threshold (°C).
// The die runs ~2-3°C above ambient; 70°C die = ~67°C ambient.
// Raise to 80 if mounted close to the engine bay.
#define TEMP_ALERT_THRESHOLD_C  70.0f

// How long (ms) the temperature must stay above threshold before alerting.
// Prevents a single noisy sample from triggering a false alarm.
#define TEMP_ALERT_SUSTAIN_MS   5000

// How long (ms) to wait before re-alerting if temp stays high.
#define TEMP_ALERT_COOLDOWN_MS  60000

// ==========================================
// NETWORK & CLOUD SETTINGS
// ==========================================

// --- 1. ESP32 OFFLINE HOTSPOT (For Local Downloads) ---
// This is the network the ESP32 broadcasts if you want to connect a laptop to it.
#define WIFI_SSID "BlackBox_S3"
#define WIFI_PASS "12345678"

// --- 2. INTERNET CONNECTION ---
// These are compile-time fallback defaults only.
// Actual credentials are stored in NVS flash via SettingsManager and
// configured through the web UI at http://192.168.4.1/settings.
// Never commit real passwords here — use the web UI instead.
#define ROUTER_SSID ""
#define ROUTER_PASS ""

// --- 3. FIREBASE CREDENTIALS ---
// Leave blank — configure via the web settings page.
// Stored securely in NVS flash, never in source code.
#define FIREBASE_URL ""
#define FIREBASE_API_KEY ""

// --- 4. DEVICE & USER IDENTITY ---
// Leave blank — configure via the web settings page.
#define USER_ID ""

#endif