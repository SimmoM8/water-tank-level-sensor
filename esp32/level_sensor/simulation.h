#pragma once
#include <stdint.h>

int32_t readSimulatedRaw();
void sim_start(int32_t raw);
void setSimulationMode(uint8_t mode);
