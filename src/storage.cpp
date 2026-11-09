#include "storage.h"
#include "sensors.h"
#include "hmi.h"
#include <SPI.h>
#include <SD.h>
#include <Preferences.h>

SemaphoreHandle_t sdMutex;
Preferences preferences;
String pathSave;

void initStorage() {
  if (!SD.begin(PIN_SD_CS)) {
    Serial.println("Falha ao inicializar o cartao SD.");
  }

  preferences.begin("storage", false);
  pathSave = "/Coleta_" + String(preferences.getInt("cont", 0)) + ".txt";
}

// Incrementa o contador na memoria e gera o nome /Coleta_N.txt (igual ao SCD 1)
String startNewSession() {
  int contador = preferences.getInt("cont", 0) + 1;
  preferences.putInt("cont", contador);
  pathSave = "/Coleta_" + String(contador) + ".txt";
  Serial.println("Salvando em: " + pathSave);
  return pathSave;
}

// Guarda a taxa de amostragem escolhida para ela continuar valendo depois de desligar
void saveSampleRate(int indice) {
  preferences.putInt("taxa", indice);
}

// Le a taxa de amostragem salva (se nunca foi salva, usa a TAXA_PADRAO)
int loadSampleRate() {
  return preferences.getInt("taxa", TAXA_PADRAO);
}

// Adapta o save() original do SCD 1 para gravar todos os sensores do SCD 2
void saveData(DadosSensores &dados) {
  if (xSemaphoreTake(sdMutex, (TickType_t)10) == pdTRUE) {
    File fileToSave = SD.open(pathSave, FILE_APPEND);

    if (fileToSave) {
      char dataBuffer[100];
      sprintf(dataBuffer, "%lu %ld %.2f %.2f %.2f %.2f",
              dados.tempoMs,
              dados.celulaCarga,
              dados.ntc1,
              dados.ntc2,
              dados.ntc3,
              dados.termopar);
      fileToSave.println(dataBuffer);
      fileToSave.close();
    } else {
      Serial.println("Falha ao abrir arquivo para salvar!");
    }
    xSemaphoreGive(sdMutex);
  } else {
    Serial.println("Falha ao obter mutex do SD para salvar dados.");
  }
}

// Rotina de calibracao: le a celula de carga por 10 segundos e salva a media no SD
void runCalibrationRoutine() {
  Serial.println("Iniciando calibracao de 10 segundos...");
  showCalibrationScreen();

  unsigned long tempoInicial = millis();
  long long somaLeituras = 0;
  long quantidadeAmostras = 0;

  while (millis() - tempoInicial < TEMPO_CALIBRACAO) {
    long leitura = readLoadCell();
    somaLeituras += leitura;
    quantidadeAmostras++;

    int segRestantes = (TEMPO_CALIBRACAO - (millis() - tempoInicial)) / 1000;
    updateCalibrationScreen(segRestantes, leitura);
  }

  long media = 0;
  if (quantidadeAmostras > 0) {
    media = (long)(somaLeituras / quantidadeAmostras);
  }

  // Gera o arquivo /Calibracao_N.txt para salvar o valor medio
  int contCal = preferences.getInt("cal_cont", 0) + 1;
  preferences.putInt("cal_cont", contCal);
  String pathCal = "/Calibracao_" + String(contCal) + ".txt";

  if (xSemaphoreTake(sdMutex, (TickType_t)10) == pdTRUE) {
    File calFile = SD.open(pathCal, FILE_APPEND);
    if (calFile) {
      calFile.print("Media da Celula de Carga (10s): ");
      calFile.println(media);
      calFile.print("Amostras lidas: ");
      calFile.println(quantidadeAmostras);
      calFile.print("Taxa de amostragem: ");
      calFile.print(getSPSValue(getSampleRateIndex()));
      calFile.println(" SPS");
      calFile.close();
      Serial.println("Calibracao salva em: " + pathCal);
    } else {
      Serial.println("Falha ao salvar arquivo de calibracao!");
    }
    xSemaphoreGive(sdMutex);
  }

  showCalibrationDone(media);
  delay(2000); // Mostra o resultado por 2 segundos antes de voltar ao menu
}
