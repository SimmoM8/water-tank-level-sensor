#pragma once

#include <stdint.h>
#include "device_state.h"

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
bool storage_loadSimulation(SenseMode &senseMode, uint8_t &mode);
void storage_saveSimulationMode(uint8_t mode);
void storage_saveSenseMode(SenseMode senseMode);

/* ---------------- OTA Options ---------------- */
bool storage_loadOtaOptions(bool &force, bool &reboot);
void storage_saveOtaForce(bool force);
void storage_saveOtaReboot(bool reboot);
bool storage_loadOtaLastSuccess(uint32_t &ts);
void storage_saveOtaLastSuccess(uint32_t ts);
bool storage_loadBootCount(uint32_t &count);
void storage_saveBootCount(uint32_t count);

/* ---------------- Crash Loop State ---------------- */
bool storage_loadCrashLoop(uint32_t &winBoots, uint32_t &winBad, uint32_t &lastBoot, bool &latched, uint32_t &lastStable, uint32_t &lastReason);
void storage_saveCrashLoop(uint32_t winBoots, uint32_t winBad, uint32_t lastBoot, bool latched, uint32_t lastStable, uint32_t lastReason);

/* ---------------- Debug ---------------- */
void storage_dump();
