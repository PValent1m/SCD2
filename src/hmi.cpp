#include "hmi.h"
#include <LiquidCrystal_I2C.h>
#include <Wire.h>

// Display LCD 20x4 no endereco 0x3F (mesmo do SCD 1)
LiquidCrystal_I2C lcd(LCD_ADDR, 20, 4);

// Opcoes do menu principal do SCD II (uma por linha do LCD)
const char *opcoesMenu[4] = {"1. Coleta", "2. Calibracao", "3. Transmissao",
                             "4. Taxa SPS"};

int selectedMenu = 1; // Opcao selecionada (1 a 4)
int lastClkState = HIGH;
int buttonState = HIGH;       // Estado estavel (debounced) do botao
int lastButtonReading = HIGH; // Ultima leitura bruta do pino do botao
unsigned long lastDebounceTime = 0;
unsigned long fz_lcd = 0;

void initHMI() {
  pinMode(PIN_ENC_CLK, INPUT_PULLUP);
  pinMode(PIN_ENC_DT, INPUT); // GPIO 35 é somente entrada
  pinMode(PIN_ENC_SW,
          INPUT_PULLUP); // Botao do encoder (LOW quando pressionado)

  lastClkState = digitalRead(PIN_ENC_CLK);
  buttonState = digitalRead(PIN_ENC_SW);
  lastButtonReading = buttonState;

  Wire.begin(PIN_LCD_SDA, PIN_LCD_SCL);
  lcd.init();
  lcd.backlight();
}

// Desenha o menu principal no LCD (mesma logica do printMenu do SCD 1)
// Como agora sao 4 opcoes, cada uma ocupa uma linha e o titulo fica no canto da
// primeira
void printMenu() {
  lcd.clear();
  for (int i = 0; i < 4; i++) {
    lcd.setCursor(1, i);
    lcd.print(opcoesMenu[i]);
  }
  lcd.setCursor(13, 0);
  lcd.print("SCD-PRD");

  lcd.setCursor(0, selectedMenu - 1);
  lcd.print(">");
}

// Le o Rotary Encoder: retorna 1 (horario), -1 (anti-horario) ou 0 (parado)
int readEncoder() {
  int giro = 0;
  int currentClk = digitalRead(PIN_ENC_CLK);

  // Se o pino CLK mudou de HIGH para LOW, o encoder foi girado
  if (lastClkState == HIGH && currentClk == LOW) {
    if (digitalRead(PIN_ENC_DT) == HIGH) {
      giro = 1; // Sentido horario
    } else {
      giro = -1; // Sentido anti-horario
    }
  }

  lastClkState = currentClk;
  return giro;
}

// Substitui o menuSelect() antigo: usa o giro do Rotary Encoder
void updateMenuNavigation() {
  int giro = readEncoder();
  if (giro == 0) {
    return;
  }

  int prev_selectedMenu = selectedMenu;
  selectedMenu = selectedMenu + giro;

  // Mantem o cursor dentro das 4 opcoes
  if (selectedMenu > 4)
    selectedMenu = 4;
  if (selectedMenu < 1)
    selectedMenu = 1;

  // Se mudou de opcao, move apenas o cursor '>' no LCD
  if (prev_selectedMenu != selectedMenu) {
    lcd.setCursor(0, prev_selectedMenu - 1);
    lcd.print(" ");
    lcd.setCursor(0, selectedMenu - 1);
    lcd.print(">");
  }
}

// Substitui o click() antigo: retorna true quando o botao do encoder e
// pressionado (com debounce completo tanto na descida quanto na subida)
bool checkButtonClick() {
  int reading = digitalRead(PIN_ENC_SW);
  bool clicou = false;

  // Reinicia o temporizador sempre que o sinal bruto oscilar (ruido ou bounce)
  if (reading != lastButtonReading) {
    lastDebounceTime = millis();
  }
  lastButtonReading = reading;

  // So atualiza o estado logico do botao se o nivel permanecer estavel por > 50ms
  if ((millis() - lastDebounceTime) > 50) {
    if (reading != buttonState) {
      buttonState = reading;
      // Registra o clique apenas na transicao estavel para pressionado (LOW)
      if (buttonState == LOW) {
        clicou = true;
      }
    }
  }

  return clicou;
}

// --- Telas de cada modo ---

void showCollectionScreen(String pathSave) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("COLETA ATIVA...");
  lcd.setCursor(0, 1);
  lcd.print(pathSave);
  lcd.setCursor(0, 2);
  lcd.print("Empuxo: ");
  lcd.setCursor(0, 3);
  lcd.print("Pressione para Parar");
}

void updateCollectionScreen(long empuxo) {
  if (millis() - fz_lcd > 500) {
    fz_lcd = millis();
    lcd.setCursor(8, 2);
    lcd.print("            ");
    lcd.setCursor(8, 2);
    lcd.print(empuxo);
  }
}

void showSDErrorScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("ERRO NO CARTAO SD!");
  lcd.setCursor(0, 1);
  lcd.print("Falha ao gravar.");
  lcd.setCursor(0, 2);
  lcd.print("Verifique o cartao");
  lcd.setCursor(0, 3);
  lcd.print("e tente novamente.");
}

void showCalibrationScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("CALIBRANDO (10s)...");
  lcd.setCursor(0, 1);
  lcd.print("Tempo rest.: 10s");
  lcd.setCursor(0, 2);
  lcd.print("Leitura: ");
}

void updateCalibrationScreen(int segundosRestantes, long leituraAtual) {
  if (millis() - fz_lcd > 500) {
    fz_lcd = millis();
    lcd.setCursor(13, 1);
    lcd.print("     ");
    lcd.setCursor(13, 1);
    lcd.print(segundosRestantes);
    lcd.print("s");

    lcd.setCursor(9, 2);
    lcd.print("           ");
    lcd.setCursor(9, 2);
    lcd.print(leituraAtual);
  }
}

void showCalibrationDone(long media, bool salvoNoSD) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("CALIBRACAO CONCLUIDA");
  lcd.setCursor(0, 1);
  lcd.print("Media: ");
  lcd.print(media);
  lcd.setCursor(0, 3);
  if (salvoNoSD) {
    lcd.print("Salvo no Cartao SD!");
  } else {
    lcd.print("ERRO ao salvar SD!");
  }
}

void showTransmissionScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Rede Wi-Fi Ativa");
  lcd.setCursor(0, 1);
  lcd.print("IP: 192.168.4.1");
  lcd.setCursor(0, 3);
  lcd.print("Pressione para Sair");
}

// Mostra a taxa atual na tela de transmissao (muda quando alterada pela pagina
// web)
void updateTransmissionRate(int sps) {
  lcd.setCursor(0, 2);
  lcd.print("                    ");
  lcd.setCursor(0, 2);
  lcd.print("Taxa: ");
  lcd.print(sps);
  lcd.print(" SPS");
}

// --- Tela de escolha da taxa de amostragem ---

void showRateScreen(int sps) {
  lcd.clear();
  lcd.setCursor(1, 0);
  lcd.print("TAXA DE AMOSTRAGEM");
  lcd.setCursor(0, 1);
  lcd.print("Gire para escolher:");
  updateRateScreen(sps);
  lcd.setCursor(0, 3);
  lcd.print("Clique p/ confirmar");
}

void updateRateScreen(int sps) {
  lcd.setCursor(0, 2);
  lcd.print("                    ");
  lcd.setCursor(5, 2);
  lcd.print("< ");
  lcd.print(sps);
  lcd.print(" SPS >");
}

void showRateSaved(int sps) {
  lcd.clear();
  lcd.setCursor(0, 1);
  lcd.print("Taxa salva:");
  lcd.setCursor(0, 2);
  lcd.print(sps);
  lcd.print(" SPS");
}
