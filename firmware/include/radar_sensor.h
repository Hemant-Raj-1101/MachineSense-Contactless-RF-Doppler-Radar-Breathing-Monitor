#ifndef RADAR_SENSOR_H
#define RADAR_SENSOR_H

#include <Arduino.h>

// Function prototypes to expose them to main.cpp
void initRadar();
void vRadarAcquisitionTask(void *pvParameters);

#endif // RADAR_SENSOR_H