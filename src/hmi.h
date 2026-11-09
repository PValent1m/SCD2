#ifndef HMI_H
#define HMI_H

#include <Arduino.h>
#include "config.h"

extern int selectedMenu;

void initHMI();
int readEncoder();
void updateMenuNavigation();
bool checkButtonClick();

void printMenu();
void showCollectionScreen(String pathSave);
void updateCollectionScreen(long empuxo);
void showCalibrationScreen();
void updateCalibrationScreen(int segundosRestantes, long leituraAtual);
void showCalibrationDone(long media);
void showTransmissionScreen();
void updateTransmissionRate(int sps);
void showRateScreen(int sps);
void updateRateScreen(int sps);
void showRateSaved(int sps);

#endif
