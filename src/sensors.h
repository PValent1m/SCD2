#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>
#include "config.h"

void initSensors();
long readLoadCell();
float readNTC(int channel);
float readThermocouple();
void readAllSensors(DadosSensores &dados);

// Taxa de amostragem do ADS1256 (pode ser trocada com o sistema ligado)
void setSampleRate(int indice);
int getSampleRateIndex();
int getSPSValue(int indice);
int findSPSIndex(int valorSPS);

#endif
