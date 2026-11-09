#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// --- Pinos do Barramento SPI ---
#define PIN_SPI_SCK   18
#define PIN_SPI_MISO  19
#define PIN_SPI_MOSI  23

// --- Pinos de Chip Select (CS) ---
#define PIN_SD_CS     5
#define PIN_ADS_CS    14
#define PIN_MAX_CS    27

// --- Pino de Interrupcao / Data Ready do ADS1256 ---
#define PIN_ADS_DRDY  34

// --- Pinos do Rotary Encoder ---
#define PIN_ENC_CLK   4
#define PIN_ENC_DT    35
#define PIN_ENC_SW    15

// --- Pinos do LCD I2C ---
#define PIN_LCD_SDA   21
#define PIN_LCD_SCL   22
#define LCD_ADDR      0x3F   // ou 0x27 dependendo do modulo I2C

// --- Constantes de Calibracao e Sensores ---
#define TEMPO_CALIBRACAO  10000  // 10 segundos em milissegundos
#define ADS_VREF          2.5    // Tensao de referencia do ADS1256

// --- Taxa de amostragem do ADS1256 (pode ser trocada com o sistema ligado) ---
// Opcoes (indice): 0 = 100 SPS, 1 = 500 SPS, 2 = 1000 SPS, 3 = 2000 SPS, 4 = 7500 SPS
#define NUM_TAXAS         5
#define TAXA_PADRAO       2      // 1000 SPS (usada enquanto nada foi salvo na memoria)

// Parametros dos NTCs (divisor de tensao de 10k e NTC de 10k)
#define NTC_VCC           5.0
#define NTC_R_FIXO        10000.0
#define NTC_R0            10000.0
#define NTC_T0_KELVIN     298.15 // 25 graus Celsius em Kelvin
#define NTC_BETA          3950.0

// --- Configuracoes do Wi-Fi (Access Point) ---
#define WIFI_SSID         "SCD"
#define WIFI_PASS         "123456789"

// Struct para guardar todas as leituras dos sensores
struct DadosSensores {
  unsigned long tempoMs;
  long celulaCarga;
  float ntc1;
  float ntc2;
  float ntc3;
  float termopar;
};

#endif
