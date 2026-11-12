#include "hmi.h"
#include <LiquidCrystal_I2C.h>
#include <Wire.h>

LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);

int selectedMenu = 1; 
int lastClkState = HIGH;
int buttonState = HIGH;       
int lastButtonReading = HIGH; 
unsigned long lastDebounceTime = 0;
unsigned long fz_lcd = 0;

void initHMI() {
  pinMode(PIN_ENC_CLK, INPUT_PULLUP);
  pinMode(PIN_ENC_DT, INPUT_PULLUP); 
  pinMode(PIN_ENC_SW, INPUT_PULLUP); 

  lastClkState = digitalRead(PIN_ENC_CLK);
  buttonState = digitalRead(PIN_ENC_SW);
  lastButtonReading = buttonState;

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
  int reading = digitalRead(PIN_ENC_SW);
  bool clicou = false;

  if (reading != lastButtonReading) {
    lastDebounceTime = millis();
  }
  lastButtonReading = reading;

  if ((millis() - lastDebounceTime) > 50) {
    if (reading != buttonState) {
      buttonState = reading;
      if (buttonState == LOW) {
        clicou = true;
      }
    }
  }
  return clicou;
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
  
  // Imprime o [VOLTAR] no canto direito da segunda linha
  lcd.setCursor(0, 1);
  lcd.print("E:      [VOLTAR]");
}

void updateCollectionScreen(long empuxo) {
  if (millis() - fz_lcd > 500) {
    fz_lcd = millis();
    lcd.setCursor(2, 1);
    lcd.print("      "); // Apaga o empuxo anterior (6 espacos max)
    lcd.setCursor(2, 1);
    lcd.print(empuxo);   // Escreve o novo empuxo sem apagar o [VOLTAR]
  }
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