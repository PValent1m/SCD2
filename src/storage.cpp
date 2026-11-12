#include "storage.h"
#include "sensors.h"
#include "hmi.h"
#include <SPI.h>
#include <SD.h>
#include <Preferences.h>

SemaphoreHandle_t sdMutex;
Preferences preferences;
String pathSave;
File sessionFile;
unsigned long ultimoFlushSD = 0;
bool sdMontado = false;

void initStorage() {
  sdMontado = SD.begin(PIN_SD_CS);
  if (!sdMontado) {
    Serial.println("Falha ao inicializar o cartao SD.");
  }

  preferences.begin("storage", false);
  pathSave = "/Coleta_" + String(preferences.getInt("cont", 0)) + ".txt";
}

// Garante que o cartao SD esta montado (tenta inicializar novamente se necessario)
static bool garantirSDMontado() {
  if (!sdMontado) {
    sdMontado = SD.begin(PIN_SD_CS);
  }
  return sdMontado;
}

// Incrementa o contador na memoria, abre o arquivo /Coleta_N.txt e mantem aberto durante a sessao.
// Retorna "" se houver falha no cartao SD.
String startNewSession() {
  if (xSemaphoreTake(sdMutex, (TickType_t)100) != pdTRUE) {
    Serial.println("Falha ao obter mutex do SD para iniciar sessao.");
    return "";
  }

  if (!garantirSDMontado()) {
    xSemaphoreGive(sdMutex);
    Serial.println("Cartao SD ausente ou com falha ao iniciar coleta.");
    return "";
  }

  if (sessionFile) {
    sessionFile.close();
  }

  int proximoContador = preferences.getInt("cont", 0) + 1;
  String novoCaminho = "/Coleta_" + String(proximoContador) + ".txt";

  sessionFile = SD.open(novoCaminho, FILE_APPEND);
  if (!sessionFile) {
    // Tenta remontar o SD caso o cartao tenha sido removido e reinserido
    SD.end();
    sdMontado = SD.begin(PIN_SD_CS);
    if (sdMontado) {
      sessionFile = SD.open(novoCaminho, FILE_APPEND);
    }
  }

  if (!sessionFile) {
    xSemaphoreGive(sdMutex);
    Serial.println("Falha ao criar arquivo de coleta no SD!");
    return "";
  }

  preferences.putInt("cont", proximoContador);
  pathSave = novoCaminho;
  ultimoFlushSD = millis();
  xSemaphoreGive(sdMutex);

  Serial.println("Salvando em: " + pathSave);
  return pathSave;
}

// Fecha o arquivo de coleta ao encerrar a sessao, garantindo a gravacao da tabela FAT
void stopSession() {
  if (xSemaphoreTake(sdMutex, (TickType_t)100) == pdTRUE) {
    if (sessionFile) {
      sessionFile.flush();
      sessionFile.close();
    }
    xSemaphoreGive(sdMutex);
  }
}

// Guarda a taxa de amostragem escolhida para ela continuar valendo depois de desligar
void saveSampleRate(int indice) {
  preferences.putInt("taxa", indice);
}

// Le a taxa de amostragem salva (se nunca foi salva, usa a TAXA_PADRAO)
int loadSampleRate() {
  return preferences.getInt("taxa", TAXA_PADRAO);
}

// Grava todos os sensores do SCD 2 no arquivo ja aberto (com flush periodico no SD)
void saveData(DadosSensores &dados) {
  if (xSemaphoreTake(sdMutex, (TickType_t)10) == pdTRUE) {
    if (sessionFile) {
      char dataBuffer[100];
      snprintf(dataBuffer, sizeof(dataBuffer), "%lu %ld %.2f %.2f %.2f %.2f",
               dados.tempoMs,
               dados.celulaCarga,
               dados.ntc1,
               dados.ntc2,
               dados.ntc3,
               dados.termopar);
      sessionFile.println(dataBuffer);

      // Faz flush periodico a cada 500ms para proteger os dados sem travar cada amostra
      if (millis() - ultimoFlushSD >= 500) {
        ultimoFlushSD = millis();
        sessionFile.flush();
      }
    } else {
      Serial.println("Falha: arquivo de sessao nao esta aberto!");
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

  bool salvoNoSD = false;
  int contCal = preferences.getInt("cal_cont", 0) + 1;
  String pathCal = "/Calibracao_" + String(contCal) + ".txt";

  if (xSemaphoreTake(sdMutex, (TickType_t)100) == pdTRUE) {
    if (garantirSDMontado()) {
      File calFile = SD.open(pathCal, FILE_APPEND);
      if (!calFile) {
        SD.end();
        sdMontado = SD.begin(PIN_SD_CS);
        if (sdMontado) {
          calFile = SD.open(pathCal, FILE_APPEND);
        }
      }

      if (calFile) {
        size_t bytesEscritos = 0;
        bytesEscritos += calFile.print("Media da Celula de Carga (10s): ");
        bytesEscritos += calFile.println(media);
        bytesEscritos += calFile.print("Amostras lidas: ");
        bytesEscritos += calFile.println(quantidadeAmostras);
        bytesEscritos += calFile.print("Taxa de amostragem: ");
        bytesEscritos += calFile.print(getSPSValue(getSampleRateIndex()));
        bytesEscritos += calFile.println(" SPS");
        calFile.flush();
        calFile.close();

        if (bytesEscritos > 0) {
          salvoNoSD = true;
          preferences.putInt("cal_cont", contCal);
          Serial.println("Calibracao salva em: " + pathCal);
        }
      }
    }

    if (!salvoNoSD) {
      Serial.println("Falha ao salvar arquivo de calibracao!");
    }
    xSemaphoreGive(sdMutex);
  }

  showCalibrationDone(media, salvoNoSD);
  delay(2000); // Mostra o resultado por 2 segundos antes de voltar ao menu
}