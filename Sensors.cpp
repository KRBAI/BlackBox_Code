#include "Sensors.h"
#include "WebManager.h"

Adafruit_MPU6050 mpu;
TinyGPSPlus gps;
HardwareSerial GPS_Serial(1);

// ── Software clock ────────────────────────────────────────────────
unsigned long softClockBase  = 0;
unsigned long softClockSetAt = 0;

void setSoftClock(unsigned long unixLKT) {
  softClockBase  = unixLKT;
  softClockSetAt = millis();
  Serial.printf("[Clock] Software clock set to Unix %lu\n", unixLKT);
}

// Calibration Offsets
float off_ax = 0, off_ay = 0, off_az = 0;
float off_gx = 0, off_gy = 0, off_gz = 0;

void initSensors() {
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setTimeOut(1000);

  if (!mpu.begin()) {
    Serial.println("MPU Not Found!");
  } else {
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  }

  GPS_Serial.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
}

void calibrateSensors() {
  Serial.println("Calibrating...");
  float sum_ax=0, sum_ay=0, sum_az=0;
  float sum_gx=0, sum_gy=0, sum_gz=0;
  int samples = 100;

  for (int i=0; i<samples; i++) {
    sensors_event_t a, g, t;
    mpu.getEvent(&a, &g, &t);
    sum_ax += a.acceleration.x; sum_ay += a.acceleration.y; sum_az += a.acceleration.z;
    sum_gx += g.gyro.x;         sum_gy += g.gyro.y;         sum_gz += g.gyro.z;
    delay(3);
  }

  off_ax = sum_ax / samples;
  off_ay = sum_ay / samples;
  off_az = (sum_az / samples) - 9.81;   // keep 1G reference for rollover trig
  off_gx = sum_gx / samples;
  off_gy = sum_gy / samples;
  off_gz = sum_gz / samples;
}

SensorData getSensorReadings() {
  SensorData d;

  // 1. GPS
  while (GPS_Serial.available() > 0) gps.encode(GPS_Serial.read());
  d.lat      = gps.location.lat();
  d.lon      = gps.location.lng();
  d.alt      = gps.altitude.meters();
  d.speed    = gps.speed.kmph();
  d.sats     = gps.satellites.value();
  d.gpsValid = gps.location.isValid();

  // 2. Software clock → local time (LKT = UTC+5:30)
  {
    static constexpr unsigned long LKT_OFFSET_SEC = 19800UL;
    unsigned long elapsed = (millis() - softClockSetAt) / 1000UL;
    unsigned long t = softClockBase + elapsed + LKT_OFFSET_SEC;

    uint8_t ss = t % 60;
    uint8_t mn = (t / 60) % 60;
    uint8_t hh = (t / 3600) % 24;

    uint32_t days = t / 86400UL;
    uint16_t year = 1970;
    while (true) {
      bool leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
      uint32_t diy = leap ? 366 : 365;
      if (days < diy) break;
      days -= diy; year++;
    }
    bool leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    static const uint8_t dim[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    uint8_t month = 1;
    for (; month <= 12; month++) {
      uint8_t md = dim[month-1] + (month == 2 && leap ? 1 : 0);
      if (days < md) break;
      days -= md;
    }
    uint8_t day = (uint8_t)(days + 1);

    char tBuf[10], dBuf[12];
    sprintf(tBuf, "%02d:%02d:%02d", hh, mn, ss);
    sprintf(dBuf, "%02d/%02d/%04d", day, month, year);
    d.timeStr = String(tBuf);
    d.dateStr = String(dBuf);
  }

  // 3. IMU
  sensors_event_t a, g, t;
  mpu.getEvent(&a, &g, &t);
  d.ax = a.acceleration.x - off_ax;
  d.ay = a.acceleration.y - off_ay;
  d.az = a.acceleration.z - off_az;
  d.gx = g.gyro.x - off_gx;
  d.gy = g.gyro.y - off_gy;
  d.gz = g.gyro.z - off_gz;
  d.temp = t.temperature;

  // 4. Internal storage status (injected from WebManager)
  if (internalMounted) {
    d.sdStatus  = true;
    d.sdUsedMB  = internalUsedMB;
    d.sdTotalMB = internalTotalMB;
  } else {
    d.sdStatus  = false;
    d.sdUsedMB  = 0;
    d.sdTotalMB = 0;
  }

  // 5. Web-injected location (from /push-location when GPS has no fix)
  d.webLat          = webInjectedLat;
  d.webLon          = webInjectedLon;
  d.webSpeed        = webInjectedSpeed;
  d.webLocationValid = webLocationInjected;

  // 6. Blindspot distances (injected from /push-blindspot endpoint)
  d.blindLeft  = webBlindLeft;
  d.blindRight = webBlindRight;

  return d;
}