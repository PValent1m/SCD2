#include "hmi.h"
#include <LiquidCrystal_I2C.h>
#include <Ticker.h>
#include <Wire.h>

LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);
Ticker buttonTicker;

int selectedMenu = 1; 
int lastClkState = HIGH;
unsigned long fz_lcd = 0;

// Filtro digital de 5ms com histerese (integrador 0..8) exclusivo do Timer
volatile bool eventoCliquePendente = false;
uint8_t contadorFiltroBotao = 0;
bool botaoEstavelPressionado = false;
unsigned long ultimoCliqueValidoMs = 0;

void amostragemBotao5ms() {
  int leitura = digitalRead(PIN_ENC_SW);

  if (leitura == LOW) {
    if (contadorFiltroBotao < 8) {
      contadorFiltroBotao++;
    }
  } else {
    if (contadorFiltroBotao > 0) {
      contadorFiltroBotao--;
    }
  }

  // Atingiu 8 amostras liquidas em LOW (~40ms): confirma pressao
  if (contadorFiltroBotao == 8 && !botaoEstavelPressionado) {
    botaoEstavelPressionado = true;
    unsigned long agora = millis();
    if (agora - ultimoCliqueValidoMs >= 300) {
      eventoCliquePendente = true;
      ultimoCliqueValidoMs = agora;
    }
  }
  // Retornou a 0 amostras (totalmente solto em HIGH por ~40ms): rearma para o proximo clique
  else if (contadorFiltroBotao == 0 && botaoEstavelPressionado) {
    botaoEstavelPressionado = false;
  }
}

void initHMI() {
  pinMode(PIN_ENC_CLK, INPUT_PULLUP);
  pinMode(PIN_ENC_DT, INPUT_PULLUP); 
  pinMode(PIN_ENC_SW, INPUT_PULLUP); 

  lastClkState = digitalRead(PIN_ENC_CLK);
  botaoEstavelPressionado = (digitalRead(PIN_ENC_SW) == LOW);
  contadorFiltroBotao = botaoEstavelPressionado ? 8 : 0;
  eventoCliquePendente = false;

  // Inicia amostragem periodica do botao a cada 5ms em background
  buttonTicker.attach_ms(5, amostragemBotao5ms);

  Wire.begin(PIN_LCD_SDA, PIN_LCD_SCL);
  delay(250); // Tempo para o I2C do display estabilizar
  
  lcd.init();
  lcd.backlight();
  lcd.clear();
}

// Menu ultrassimples: Sem titulo, apenas as 2 opcoes na tela
void printMenu() {
  lcd.clear();
  if (selectedMenu == 1) {
    lcd.setCursor(0, 0); lcd.print("> INICIAR COLETA");
    lcd.setCursor(0, 1); lcd.print("  TRANSMISSAO");
  } else {
    lcd.setCursor(0, 0); lcd.print("  INICIAR COLETA");
    lcd.setCursor(0, 1); lcd.print("> TRANSMISSAO");
  }
}

int readEncoder() {
  int giro = 0;
  int currentClk = digitalRead(PIN_ENC_CLK);

  if (lastClkState == HIGH && currentClk == LOW) {
    if (digitalRead(PIN_ENC_DT) == HIGH) {
      giro = 1; 
    } else {
      giro = -1; 
    }
  }
  lastClkState = currentClk;
  return giro;
}

void updateMenuNavigation() {
  int giro = readEncoder();
  if (giro == 0) return;

  int prev_selectedMenu = selectedMenu;
  selectedMenu = selectedMenu + giro;

  // Trava a selecao entre as opcoes 1 e 2
  if (selectedMenu > 2) selectedMenu = 2;
  if (selectedMenu < 1) selectedMenu = 1;

  if (prev_selectedMenu != selectedMenu) {
    printMenu(); // Atualiza a posicao do cursor >
  }
}

bool checkButtonClick() {
  if (eventoCliquePendente) {
    eventoCliquePendente = false;
    return true;
  }
  return false;
}

// --- Telas de Operacao ---

void showCollectionScreen(String pathSave) {
  lcd.clear();
  lcd.setCursor(0, 0);
  
  String txt = pathSave;
  txt.replace("/", "");
  txt.replace(".txt", "");
  
  lcd.print("REC:");
  lcd.print(txt); 
  
  // Tela estatica durante a coleta para nao travar o barramento I2C
  lcd.setCursor(0, 1);
  lcd.print("Aperte p/ VOLTAR");
}

void updateCollectionScreen(long empuxo) {
  // Desativado durante a coleta para evitar bloqueio do I2C (~20ms) e perda de amostras
}

void showSDErrorScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("ERRO NO CARTAO!");
  lcd.setCursor(0, 1);
  lcd.print("Verifique o SD.");
}

void showTransmissionScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("IP:192.168.4.1");
  lcd.setCursor(0, 1);
  lcd.print("Aperte p/ VOLTAR");
}

// --- Funcoes Ocultas (Dummies) para satisfazer o compilador ---
void updateTransmissionRate(int sps) {}
void showCalibrationScreen() {}
void updateCalibrationScreen(int segundosRestantes, long leituraAtual) {}
void showCalibrationDone(long media, bool salvoNoSD) {}
void showRateScreen(int sps) {}
void updateRateScreen(int sps) {}
void showRateSaved(int sps) {}