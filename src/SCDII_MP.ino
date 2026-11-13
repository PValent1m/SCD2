/*
#include <Arduino.h>
#include "config.h"
#include "sensors.h"
#include "hmi.h"
#include "storage.h"
#include "web_server.h"

// Flags de controle de modo (mesmo estilo do SCD 1)
bool coleta = false;
bool calibrando = false;
bool trans = false;
bool ajustandoTaxa = false;

int taxaEscolhida = 0;   // Taxa que esta sendo escolhida na tela do encoder
int taxaMostrada = -1;   // Ultima taxa mostrada na tela de transmissao

void startCollection() {
  if (!coleta) {
    String caminho = startNewSession();
    if (caminho == "") {
      Serial.println("Coleta ABORTADA: falha no cartao SD.");
      showSDErrorScreen();
      delay(2000);
      printMenu();
      return;
    }

    coleta = true;
    Serial.println("Coleta INICIADA.");
    showCollectionScreen(caminho);
  }
}

void stopCollection() {
  if (coleta) {
    coleta = false;
    stopSession();
    Serial.println("Coleta PARADA.");
    printMenu();
  }
}

void handleCollectionMode() {
  DadosSensores dados;
  readAllSensors(dados);
  saveData(dados);
  updateCollectionScreen(dados.celulaCarga);

  if (checkButtonClick()) {
    stopCollection();
  }
}

void handleCalibrationMode() {
  runCalibrationRoutine();
  calibrando = false;
  printMenu();
}

void handleTransmissionMode() {
  // Se a taxa foi trocada pela pagina web, atualiza o valor no LCD
  if (getSampleRateIndex() != taxaMostrada) {
    taxaMostrada = getSampleRateIndex();
    updateTransmissionRate(getSPSValue(taxaMostrada));
  }

  if (checkButtonClick()) {
    Serial.println("Botao pressionado, saindo do modo de transmissao.");
    trans = false;
    stopWebServer();
    printMenu();
  }
}

void handleRateMode() {
  // Girar o encoder troca a opcao de taxa mostrada na tela
  int giro = readEncoder();
  if (giro != 0) {
    taxaEscolhida = taxaEscolhida + giro;
    if (taxaEscolhida > NUM_TAXAS - 1) taxaEscolhida = NUM_TAXAS - 1;
    if (taxaEscolhida < 0) taxaEscolhida = 0;
    updateRateScreen(getSPSValue(taxaEscolhida));
  }

  // Clicar confirma: aplica no ADS1256, salva na memoria e volta ao menu
  if (checkButtonClick()) {
    setSampleRate(taxaEscolhida);
    saveSampleRate(taxaEscolhida);
    showRateSaved(getSPSValue(taxaEscolhida));
    delay(1000);
    ajustandoTaxa = false;
    printMenu();
  }
}

void setup() {
  Serial.begin(115200);

  // Cria o Mutex que protege o acesso ao SD Card (igual ao SCD 1)
  sdMutex = xSemaphoreCreateMutex();

  initHMI();
  initSensors();
  initStorage();
  initWebServer();

  // Aplica a taxa de amostragem que estava salva na memoria
  setSampleRate(loadSampleRate());

  Serial.println("SCD II inicializado com sucesso.");
  printMenu();
}

void loop() {
  if (trans == true) {
    // ESTADO 1: Transmissao Ativa
    handleTransmissionMode();
    delay(10);
  }
  else if (coleta == true) {
    // ESTADO 2: Coleta Ativa
    handleCollectionMode();
  }
  else if (calibrando == true) {
    // ESTADO 3: Calibracao (10 segundos)
    handleCalibrationMode();
  }
  else if (ajustandoTaxa == true) {
    // ESTADO 4: Escolha da taxa de amostragem
    handleRateMode();
  }
  else {
    // ESTADO 5: Menu Principal
    updateMenuNavigation();

    if (checkButtonClick()) {
      if (selectedMenu == 1) {
        startCollection();
      }
      else if (selectedMenu == 2) {
        trans = true;
        startWebServer();
      }
    }
    /*
    if (checkButtonClick()) {
      if (selectedMenu == 1) {
        startCollection();
      }
      else if (selectedMenu == 2) {
        calibrando = true;
      }
      else if (selectedMenu == 3) {
        trans = true;
        taxaMostrada = -1;  // Forca mostrar a taxa na tela de transmissao
        startWebServer();
      }
      else if (selectedMenu == 4) {
        ajustandoTaxa = true;
        taxaEscolhida = getSampleRateIndex();
        showRateScreen(getSPSValue(taxaEscolhida));
      }
    } 
    */
    /*
  }
}
*/

#include <Arduino.h>
#include "config.h"
#include "sensors.h"
#include "hmi.h"
#include "storage.h"
#include "web_server.h"

// Flags de controle de modo restritas as 2 funcoes principais
bool coleta = false;
bool trans = false;

void startCollection() {
  if (!coleta) {
    String caminho = startNewSession();
    if (caminho == "") {
      Serial.println("Coleta ABORTADA: falha no cartao SD.");
      showSDErrorScreen();
      delay(2000);
      printMenu();
      return;
    }

    coleta = true;
    Serial.println("Coleta INICIADA.");
    showCollectionScreen(caminho);
  }
}

void stopCollection() {
  if (coleta) {
    coleta = false;
    stopSession();
    Serial.println("Coleta PARADA.");
    printMenu();
  }
}

void handleCollectionMode() {
  DadosSensores dados;
  readAllSensors(dados);
  saveData(dados);

  // Sai da coleta ao clicar no botao do encoder mais uma vez
  if (checkButtonClick()) {
    stopCollection();
  }
}

void handleTransmissionMode() {
  // Apenas aguarda o clique do botao para desligar o Wi-Fi e sair do modo
  if (checkButtonClick()) {
    Serial.println("Botao pressionado, saindo do modo de transmissao.");
    trans = false;
    stopWebServer();
    printMenu();
  }
}

void setup() {
  Serial.begin(115200);

  // Cria o Mutex que protege o acesso ao SD Card
  sdMutex = xSemaphoreCreateMutex();

  initHMI();
  initSensors();
  initStorage();
  initWebServer();

  // Aplica a taxa de amostragem que estava salva na memoria
  setSampleRate(loadSampleRate());

  Serial.println("SCD II inicializado com sucesso.");
  printMenu();
}

void loop() {
  if (trans == true) {
    // ESTADO 1: Transmissao Ativa
    handleTransmissionMode();
    delay(10);
  }
  else if (coleta == true) {
    // ESTADO 2: Coleta Ativa
    handleCollectionMode();
  }
  else {
    // ESTADO 3: Menu Principal (Apenas Coleta e Transmissao)
    updateMenuNavigation();

    if (checkButtonClick()) {
      if (selectedMenu == 1) {
        startCollection();
      }
      else if (selectedMenu == 2) {
        trans = true;
        startWebServer();
      }
    }
  }
}