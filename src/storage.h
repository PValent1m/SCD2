#ifndef STORAGE_H
#define STORAGE_H

#include <Arduino.h>
#include "config.h"

extern SemaphoreHandle_t sdMutex;
extern String pathSave;

void initStorage();
String startNewSession();
void stopSession();
void saveData(DadosSensores &dados);
void runCalibrationRoutine();
void saveSampleRate(int indice);
int loadSampleRate();

#endif