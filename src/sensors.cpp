#include "sensors.h"
#include <SPI.h>
#include <ADS1256.h>
#include <math.h>

// Instancia do ADS1256: (pino DRDY, pino RESET, pino SYNC, pino CS, tensao VREF)
// RESET e SYNC nao estao ligados ao ESP32 no esquematico, por isso PIN_UNUSED
ADS1256 ads(PIN_ADS_DRDY, ADS1256::PIN_UNUSED, ADS1256::PIN_UNUSED, PIN_ADS_CS, ADS_VREF);

// Tabela das taxas de amostragem disponiveis (mesma ordem dos indices do config.h)
const int valoresSPS[NUM_TAXAS] = {100, 500, 1000, 2000, 7500};
const byte codigosDRATE[NUM_TAXAS] = {DRATE_100SPS, DRATE_500SPS, DRATE_1000SPS, DRATE_2000SPS, DRATE_7500SPS};
int taxaAtual = TAXA_PADRAO;

// Entradas do ADS1256
#define CANAL_CELULA  6                               // Par diferencial AIN6-AIN7
const byte muxNTC[3] = {SING_0, SING_1, SING_2};      // NTC 1, 2 e 3 (AIN0, AIN1, AIN2)
int canalAtual = -1;                                  // -1 = nenhum canal selecionado ainda

unsigned long ultimaLeituraNTC = 0;
float ultimoValorNTC[3] = {0.0, 0.0, 0.0};
bool primeiraLeituraNTC = true;

unsigned long ultimaLeituraTermopar = 0;
float ultimoValorTermopar = NAN;

void initSensors() {
  // Coloca todos os pinos CS em HIGH para nao dar conflito no barramento SPI
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);
  pinMode(PIN_ADS_CS, OUTPUT);
  digitalWrite(PIN_ADS_CS, HIGH);
  pinMode(PIN_MAX_CS, OUTPUT);
  digitalWrite(PIN_MAX_CS, HIGH);

  pinMode(PIN_ADS_DRDY, INPUT);

  // Inicia o barramento SPI nos pinos do SCD II
  SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, -1);
  delay(50);

  // Inicia o ADS1256 e aplica a taxa de amostragem padrao
  ads.InitializeADC();
  ads.setDRATE(codigosDRATE[taxaAtual]);
  Serial.println("Sensores (ADS1256 e MAX6675) inicializados.");
}

// --- Taxa de amostragem ---

void setSampleRate(int indice) {
  if (indice < 0 || indice >= NUM_TAXAS) {
    return;
  }

  taxaAtual = indice;
  ads.setDRATE(codigosDRATE[taxaAtual]);

  // Forca a proxima leitura a ser descartada (ainda pode ter sido feita na taxa antiga)
  canalAtual = -1;

  Serial.print("Taxa de amostragem alterada para ");
  Serial.print(valoresSPS[taxaAtual]);
  Serial.println(" SPS");
}

int getSampleRateIndex() {
  return taxaAtual;
}

// Retorna o valor em SPS de um indice (ex: indice 2 -> 1000)
int getSPSValue(int indice) {
  if (indice < 0 || indice >= NUM_TAXAS) {
    return 0;
  }
  return valoresSPS[indice];
}

// Procura o indice de um valor em SPS (ex: 1000 -> indice 2). Retorna -1 se nao existir
int findSPSIndex(int valorSPS) {
  for (int i = 0; i < NUM_TAXAS; i++) {
    if (valoresSPS[i] == valorSPS) {
      return i;
    }
  }
  return -1;
}

// --- Leituras ---

// Seleciona a entrada do ADS1256 (so troca se for diferente da ultima usada)
void selecionarCanal(int canal) {
  if (canal == canalAtual) {
    return;
  }

  if (canal == CANAL_CELULA) {
    ads.setPGA(PGA_64);         // Ganho alto (64x) para o sinal baixo da celula
    ads.setMUX(DIFF_6_7);       // Diferencial entre AIN6 e AIN7
  } else {
    // So reconfigura o PGA se o canal anterior nao era um NTC
    if (canalAtual == CANAL_CELULA || canalAtual == -1) {
      ads.setPGA(PGA_1);        // Ganho 1x para ler de 0V ate 5V
    }
    ads.setMUX(muxNTC[canal]);  // Single-ended (AINx contra AINCOM)
  }

  // A primeira leitura depois da troca ainda pode ter o valor do canal anterior: descarta
  ads.readSingle();
  canalAtual = canal;
}

// Leitura diferencial da celula de carga nos pinos AIN6 e AIN7
long readLoadCell() {
  selecionarCanal(CANAL_CELULA);
  return ads.readSingle();  // Valor bruto de 24 bits (mesma ideia do ReadCount do SCD 1)
}

// Leitura single-ended dos NTCs nos canais AIN0 (0), AIN1 (1) e AIN2 (2)
float readNTC(int channel) {
  if (channel < 0 || channel > 2) {
    return 0.0;
  }

  selecionarCanal(channel);
  long bruto = ads.readSingle();
  float tensao = ads.convertToVoltage(bruto);

  // Evita divisao por zero caso o sensor esteja desconectado
  if (tensao <= 0.05 || tensao >= (NTC_VCC - 0.05)) {
    return 0.0;
  }

  // Calculo da resistencia do NTC pelo divisor de tensao e equacao Beta
  float resistencia = (NTC_R_FIXO * tensao) / (NTC_VCC - tensao);
  float tempK = 1.0 / ((1.0 / NTC_T0_KELVIN) + (log(resistencia / NTC_R0) / NTC_BETA));
  float tempC = tempK - 273.15;

  return tempC;
}

// Leitura do termopar tipo K pelo MAX6675 (usando o SPI do ESP32, compartilhado com o SD e o ADS)
float readThermocouple() {
  // O MAX6675 demora cerca de 220ms para fazer uma nova conversao
  if (millis() - ultimaLeituraTermopar >= 250) {
    ultimaLeituraTermopar = millis();

    SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    digitalWrite(PIN_MAX_CS, LOW);
    delayMicroseconds(1);
    unsigned int leitura = SPI.transfer16(0);  // O MAX6675 envia 16 bits
    digitalWrite(PIN_MAX_CS, HIGH);
    SPI.endTransaction();

    if (leitura & 0x04) {
      ultimoValorTermopar = NAN;                  // Bit 2 = 1 -> termopar desconectado
    } else {
      ultimoValorTermopar = (leitura >> 3) * 0.25;  // Bits 14..3 = temperatura (0,25 C por unidade)
    }
  }
  return ultimoValorTermopar;
}

// Preenche a struct com a leitura de todos os sensores
void readAllSensors(DadosSensores &dados) {
  dados.tempoMs     = millis();
  dados.celulaCarga = readLoadCell();

  // Atualiza os NTCs a cada 250ms (mesma logica do termopar) para nao chavear o MUX
  // do ADS1256 a cada amostra e manter a alta taxa de amostragem no canal de empuxo
  if (primeiraLeituraNTC || (millis() - ultimaLeituraNTC >= 250)) {
    ultimaLeituraNTC   = millis();
    primeiraLeituraNTC = false;
    ultimoValorNTC[0]  = readNTC(0);
    ultimoValorNTC[1]  = readNTC(1);
    ultimoValorNTC[2]  = readNTC(2);
  }

  dados.ntc1     = ultimoValorNTC[0];
  dados.ntc2     = ultimoValorNTC[1];
  dados.ntc3     = ultimoValorNTC[2];
  dados.termopar = readThermocouple();
}