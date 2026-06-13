#ifndef GRAPHICS_H
#define GRAPHICS_H

#include <Arduino.h>
#include <U8g2lib.h>
#include "Sensors.h"
#include "Config.h"

// Forward declaration — avoids a circular include between Graphics.h and SimManager.h
struct SimStatus;

extern U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2;

void initDisplay();
void showCalibratingText();
void playStartupAnimation();
void drawConnectingAnimation();

// Face
void drawFace(const SensorData& data, bool isHappy);

// Dashboard pages (0-5)
void drawClockPage(const SensorData& data);
void drawAccelPage(const SensorData& data);
void drawGyroPage(const SensorData& data);
void drawGPSPage(const SensorData& data);
void drawSystemPage(const SensorData& data);
void drawSimPage(SimStatus sim);     // Page 5: WiFi + SIM connection status

// Emergency screens
void drawCrashPage(float gForce);
void drawCountdownPage(int secondsLeft);
void drawDriveSafePage();
void drawSendingPage();
void drawCallingPage();
void drawRolloverPage(float roll, float pitch);
void drawTempAlertPage(float tempC);

// Blindspot warning screen
// bsLeft / bsRight: true if that side is within the hazard threshold
void drawBlindspotPage(bool bsLeft, bool bsRight,
                       int distLeft, int distRight);

#endif