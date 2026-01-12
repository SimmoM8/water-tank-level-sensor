/*
all references to storage found in water_level_sensor.cpp:
 - namespace
 - dry and wet
 - inv
 - tank volume
 - rod length
 - sime enabled
 - sim mode
- loadConfigValues()
- updateTankVolume()
- updateRodLength()
- setSimulationEnabled()
- setSimulationModeInternal()
- loadCalibration()
- clearCalibration()
- captureCalibrationPoint()
- handleInvertCalibration()
- handleSerialCommands()
- connectWiFi()
- appSetup()
*/

#pragma once // prevent multiple inclusion of this header file

#include <Arduino.h>

/* ---------------- Boot Lifecycle ---------------- */
bool storageBegin();
void storageEnd(); // optional cleanup on shutdown/restart

/* ---------------- Calibration ---------------- */
bool loadActiveCalibration(int32_t &dry, int32_t &wet, bool &inverted);
void saveCalibrationDry(int32_t dry);
void saveCalibrationWet(int32_t wet);
void saveCalibrationInverted(bool inverted);
void clearCalibration();

/* ---------------- Tank Configuration ---------------- */
bool loadTank(float &volumeLiters, float &tankHeightCm);
void saveTankVolume(float volumeLiters);
void saveTankHeight(float tankHeightCm);

/* ---------------- Simulation Configuration ---------------- */
bool loadSimulation(bool &enabled, uint8_t &mode);
void saveSimulationEnabled(bool enabled);
void saveSimulationMode(uint8_t mode);