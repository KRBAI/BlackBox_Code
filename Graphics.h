#ifndef GRAPHICS_H
#define GRAPHICS_H

#include <Arduino.h>
#include <U8g2lib.h>
#include "Sensors.h"
#include "Config.h"

// Forward declaration — avoids including SimManager.h here which
// causes a circular include chain (SimManager.h -> Config.h -> ... -> Graphics.h)
// The full SimManager.h is included only in Graphics.cpp
struct SimStatus;

extern U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2;

void initDisplay();
void showCalibratingText();
void playStartupAnimation();
void drawConnectingAnimation(); // Animated SIM init screen — call from a task while initSimManager() blocks

// Face
void drawFace(SensorData data, bool isHappy);

// Dashboard pages
void drawClockPage(SensorData data);
void drawAccelPage(SensorData data);
void drawGyroPage(SensorData data);
void drawGPSPage(SensorData data);
void drawSystemPage(SensorData data);
void drawSimPage(SimStatus& sim);

// Emergency screens
void drawCrashPage(float gForce);
void drawCountdownPage(int secondsLeft);
void drawDriveSafePage();
void drawCallingPage();

#endif