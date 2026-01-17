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
- saveTankVolume()
- saveRodLength()
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
bool storage_begin();
void storage_end(); // optional cleanup on shutdown/restart

/* ---------------- Calibration ---------------- */
bool storage_loadActiveCalibration(int32_t &dry, int32_t &wet, bool &inverted);
void storage_saveCalibrationDry(int32_t dry);
void storage_saveCalibrationWet(int32_t wet);
void storage_saveCalibrationInverted(bool inverted);
void storage_clearCalibration();

/* ---------------- Tank Configuration ---------------- */
bool storage_loadTank(float &volumeLiters, float &tankHeightCm);
void storage_saveTankVolume(float volumeLiters);
void storage_saveTankHeight(float tankHeightCm);

/* ---------------- Simulation Configuration ---------------- */
bool storage_loadSimulation(bool &enabled, uint8_t &mode);
void storage_saveSimulationEnabled(bool enabled);
void storage_saveSimulationMode(uint8_t mode);