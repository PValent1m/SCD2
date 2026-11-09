# Documentação Técnica do Firmware — SCD II (Sistema de Coleta de Dados)

**Projeto:** SCD II — Bancada de Ensaios Estáticos de Motores-Foguete  
**Plataforma de Hardware:** ESP32-WROVER  
**Framework:** Arduino / ESP-IDF (via PlatformIO)  
**Estrutura do Código:** 11 arquivos em `src/` (`config.h`, `sensors.h`, `sensors.cpp`, `hmi.h`, `hmi.cpp`, `storage.h`, `storage.cpp`, `web_page.h`, `web_server.h`, `web_server.cpp`, `main.cpp`)

---

## 1. VISÃO GERAL E ARQUITETURA DO SISTEMA

### 1.1. Propósito do Firmware em Ensaios Estáticos de Motores-Foguete
O **SCD II (Sistema de Coleta de Dados II)** é o sistema embarcado responsável pela instrumentação, aquisição em tempo real, armazenamento local e telemetria pós-ensaio em testes de queima estática de motores-foguete. Durante o disparo de um propulsor na bancada, o firmware cumpre quatro funções críticas:

1. **Aquisição de Empuxo de Alta Resolução:** Lê continuamente o sinal diferencial da célula de carga através de um conversor analógico-digital (ADC) Sigma-Delta de 24 bits (**ADS1256**), permitindo traçar a curva Empuxo $\times$ Tempo ($F(t)$) e calcular o Impulso Total ($I_t = \int F(t)\,dt$) e o Impulso Específico ($I_{sp}$).
2. **Monitoramento Térmico Multicanal:** Registra simultaneamente a evolução térmica da carcaça/revestimento do motor através de **três termistores NTC** (conectados aos canais single-ended do ADS1256) e de **um termopar Tipo K** de alta temperatura condicionado pelo integrado **MAX6675**.
3. **Calibração Local em Bancada:** Executa uma rotina dedicada de 10 segundos para levantamento estatístico (média aritmética e contagem de amostras) do sinal da célula de carga, gravando os relatórios diretamente no cartão MicroSD.
4. **Extração Sem Fio de Dados (Modo Transmissão):** Disponibiliza sob demanda uma rede Wi-Fi própria (SoftAP) com servidor HTTP assíncrono e comunicação em tempo real via WebSocket para listagem, download e exclusão dos arquivos de ensaio, além do ajuste remoto da taxa de amostragem (SPS) sem necessidade de retirar o cartão MicroSD da caixa hermética.

---

### 1.2. Evolução Arquitetural: Do SCD I para o SCD II
O projeto evoluiu tanto em nível de hardware de instrumentação quanto na engenharia de software embarcado:

| Característica | SCD I (Legado) | SCD II (Atual) | Motivação da Mudança |
| :--- | :--- | :--- | :--- |
| **Arquitetura de Código** | **Monolítica:** 1 único arquivo (`main.cpp` com 862 linhas) misturando HTML, drivers, variáveis globais e interface. | **Modularizada:** 11 arquivos divididos por responsabilidade em `src/` (Configuração, Sensores, IHM, Armazenamento, Web e Orquestrador). | Facilitar manutenção pela equipe, isolar bugs de bancada e separar claramente aquisição, interface e rede. |
| **Conversor ADC (Empuxo)** | **HX711** lido por *bit-bang* manual (`digitalWrite`/`digitalRead` em laço), restrito a 10 ou 80 SPS. | **ADS1256 (24 bits via SPI):** hardware dedicado com PGA interno de até $64\times$ e taxa ajustável de **100 a 7500 SPS**. | Aumento drástico da resolução temporal para capturar transientes rápidos de ignição (*ignition peak*) e instabilidades de queima. |
| **Sensores de Temperatura** | Ausentes. | **3x Termistores NTC** (via ADS1256) + **1x Termopar Tipo K** (via MAX6675 no barramento SPI). | Caracterização térmica da parede externa do motor e bocal durante e após a queima (*soakback* térmico). |
| **Interface Humana (IHM)** | Potenciômetro analógico (`analogRead` sujeito a ruído no ADC interno do ESP32) + botão avulso. | **Rotary Encoder incremental** (quadratura digital em `CLK`/`DT` + botão integrado `SW`) + Display LCD I2C 20x4. | Eliminação de instabilidade de cursor por flutuação analógica e seleção precisa de menus e taxas SPS. |
| **Modo de Calibração** | Inexistente no firmware. | **Rotina dedicada de 10 segundos** com acumulador de 64 bits e geração automática de arquivo `/Calibracao_N.txt`. | Padronizar a obtenção da linha de base (tara) e pontos de carga conhecida diretamente no campo de testes. |

---

### 1.3. Diagrama e Fluxo de Dados entre os Módulos

O sistema utiliza os dois núcleos do processador Xtensa Dual-Core LX6 do ESP32-WROVER:
* **Core 1 (`setup()` e `loop()`):** Executa a máquina de estados principal (`main.cpp`), aquisição de sensores (`sensors.cpp`), gravação em cartão SD (`storage.cpp`) e atualização do display/encoder (`hmi.cpp`).
* **Core 0 (`webServerTask`):** Instanciado exclusivamente quando o usuário ativa o **Modo Transmissão**, gerenciando a pilha Wi-Fi TCP/IP, o servidor HTTP e o WebSocket (`web_server.cpp`). O acesso concorrente ao cartão MicroSD entre os dois núcleos é serializado pelo semáforo mutex `sdMutex`.

```
+-----------------------------------------------------------------------------------+
|                                HARDWARE DE CAMPO                                  |
|                                                                                   |
|  [Celula Carga]   [NTC 1..3]      [Termopar K]    [Rotary Encoder]   [Cartao SD]  |
|   (AIN6-AIN7)     (AIN0..2)           |            (CLK, DT, SW)     (SPI CS=5)   |
|        \             /                v                  |                |       |
|         v           v             [MAX6675]              |                |       |
|           [ADS1256]               (SPI CS=27)            |                |       |
|        (SPI CS=14, DRDY=34)           |                  |                |       |
+---------------|-----------------------|------------------|----------------|-------+
                |                       |                  |                |
================|=======================|==================|================|========
                v                       v                  v                |
        +---------------------------------------+   +-------------+         |
        |         MODULO DE SENSORES            |   | MODULO IHM  |         |
        |       (sensors.h / sensors.cpp)       |   | (hmi.h/cpp) |         |
        +---------------------------------------+   +-------------+         |
                | DadosSensores                            ^                |
                | (tempoMs, celulaCarga,                   | Eventos de     |
                |  ntc1..3, termopar)                      | Giro / Clique  |
                v                                          v                |
        +---------------------------------------------------------+         |
        |               ORQUESTRADOR PRINCIPAL (Core 1)           |         |
        |                       (src/main.cpp)                    |         |
        |   Maquina de Estados: Menu | Coleta | Calibracao |      |         |
        |                       Transmissao | Taxa SPS            |         |
        +---------------------------------------------------------+         |
                |                                          |                |
                | saveData() /                             | start/stop     |
                | runCalibrationRoutine()                  v                |
                v                               +--------------------+      |
        +-------------------------------+       |  MODULO WEB SERVER |      |
        |    MODULO DE ARMAZENAMENTO    |       |  (Core 0 - Wi-Fi)  |      |
        |   (storage.h / storage.cpp)   |       | (web_server.h/cpp) |      |
        +-------------------------------+       |   + (web_page.h)   |      |
                |               |               +--------------------+      |
                | Preferences   |                          |                |
                v (NVS Flash)   +------------+-------------+                |
          ["storage"]                        |                              |
       (cont, cal_cont, taxa)                v                              |
                                   [ Semáforo: sdMutex ]                    |
                                             |                              |
                                             +------------------------------+
```

---

## 2. MAPEAMENTO DE HARDWARE E CUIDADOS ELÉTRICOS (`config.h`)

### 2.1. Tabela Completa de Pinos do ESP32-WROVER

Todos os pinos do sistema estão centralizados em `src/config.h` para evitar números mágicos espalhados pelo código:

| Constante (`config.h`) | GPIO ESP32 | Direção / Configuração | Barramento / Função | Periférico Conectado |
| :--- | :---: | :--- | :--- | :--- |
| `PIN_SPI_SCK` | **GPIO 18** | `OUTPUT` (Hardware SPI) | VSPI Clock (`SCK`) | Cartão SD, ADS1256 e MAX6675 |
| `PIN_SPI_MISO` | **GPIO 19** | `INPUT` (Hardware SPI) | VSPI Master In Slave Out (`MISO`) | Cartão SD, ADS1256 e MAX6675 |
| `PIN_SPI_MOSI` | **GPIO 23** | `OUTPUT` (Hardware SPI) | VSPI Master Out Slave In (`MOSI`) | Cartão SD e ADS1256 |
| `PIN_SD_CS` | **GPIO 5** | `OUTPUT` (Ativo em `LOW`) | Chip Select do Cartão SD | Módulo MicroSD |
| `PIN_ADS_CS` | **GPIO 14** | `OUTPUT` (Ativo em `LOW`) | Chip Select do Conversor ADC | ADS1256 |
| `PIN_MAX_CS` | **GPIO 27** | `OUTPUT` (Ativo em `LOW`) | Chip Select do Condicionador Termopar | MAX6675 |
| `PIN_ADS_DRDY` | **GPIO 34** | `INPUT` (Ativo em `LOW`) | Data Ready (Conversão Pronta) | Pino $\overline{\text{DRDY}}$ do ADS1256 |
| `PIN_ENC_CLK` | **GPIO 4** | `INPUT_PULLUP` | Canal A (Clock) de Quadratura | Rotary Encoder (`CLK`) |
| `PIN_ENC_DT` | **GPIO 35** | `INPUT` (Sem pull interno) | Canal B (Data) de Quadratura | Rotary Encoder (`DT`) |
| `PIN_ENC_SW` | **GPIO 15** | `INPUT_PULLUP` | Chave Push-Button (Ativo em `LOW`) | Botão do Rotary Encoder (`SW`) |
| `PIN_LCD_SDA` | **GPIO 21** | `I2C Open-Drain` | Dados Seriais I2C (`SDA`) | Display LCD 20x4 (`LCD_ADDR = 0x3F`) |
| `PIN_LCD_SCL` | **GPIO 22** | `I2C Open-Drain` | Clock Serial I2C (`SCL`) | Display LCD 20x4 (`LCD_ADDR = 0x3F`) |

---

### 2.2. Restrições Elétricas e de Arquitetura do ESP32

#### A. Por que o GPIO 35 (`PIN_ENC_DT`) e o GPIO 34 (`PIN_ADS_DRDY`) usam `INPUT` simples?
Na arquitetura de silício do ESP32, os pinos do banco **GPIO 34 a GPIO 39** são ligados exclusivamente à matriz de entrada (*Input-Only GPIOs*). Eles **não possuem transistores de saída nem resistores de pull-up ou pull-down internos** controláveis por software (`INPUT_PULLUP` não tem efeito elétrico nesses pinos).
* No `PIN_ADS_DRDY` (**GPIO 34**), o próprio integrado ADS1256 possui estágio de saída *push-pull* ativo que força o nível lógico `HIGH` ($3{,}3\text{ V}$) ou `LOW` ($0\text{ V}$).
* No `PIN_ENC_DT` (**GPIO 35**), como chaves mecânicas de encoders apenas fecham contato para o `GND`, **é obrigatório que o módulo do Rotary Encoder (como o KY-040) ou a placa de circuito impresso do SCD II possua um resistor de pull-up físico externo** (tipicamente $10\text{ k}\Omega$ para $3{,}3\text{ V}$). Por essa razão, `initHMI()` declara `pinMode(PIN_ENC_DT, INPUT)`.

#### B. Cuidados com o GPIO 15 (`PIN_ENC_SW`) e GPIO 5 (`PIN_SD_CS`) como *Strapping Pins*
O **GPIO 15** (`MTDO`) e o **GPIO 5** são pinos de configuração de boot (*strapping pins*) lidos pelo bootloader em ROM do ESP32 durante o reset:
* O **GPIO 15** define se o bootloader emite mensagens de log pela UART0 durante a inicialização (padrão esperado no boot: `HIGH`). Como o botão `SW` do encoder fecha contato para o `GND` quando pressionado e está configurado com `INPUT_PULLUP`, seu estado de repouso natural é `HIGH`. **Atenção operacional:** não mantenha o botão do encoder pressionado enquanto liga a chave geral ou pressiona o botão `EN/RST` do ESP32, pois isso colocará o GPIO 15 em `LOW` durante o boot.
* O **GPIO 5** também requer nível alto no boot e opera como Chip Select ativo em `LOW`, portanto seu estado ocioso em `HIGH` é perfeitamente compatível com o requisito de inicialização do ESP32.

#### C. Prevenção de Contenção Elétrica no Barramento SPI (`initSensors()`)
O barramento VSPI (`SCK = 18`, `MISO = 19`, `MOSI = 23`) é compartilhado por **três dispositivos escravos**: o cartão MicroSD, o ADS1256 e o MAX6675. Em um barramento SPI, apenas o dispositivo cujo pino $\overline{\text{CS}}$ estiver em `LOW` pode acionar eletricamente a linha `MISO`; todos os demais devem manter seus pinos `MISO` em alta impedância (*tri-state*), o que só ocorre quando seus respectivos pinos $\overline{\text{CS}}$ estão em `HIGH`.

Se o microcontrolador inicializar o periférico SPI ou tentar calibrar o ADS1256 enquanto o pino `PIN_SD_CS` ou `PIN_MAX_CS` estiver flutuando em nível baixo/indefinido, dois chips tentarão impor níveis lógicos opostos na linha `MISO` (**GPIO 19**), causando **contenção elétrica** (curto-circuito lógico entre saídas) e corrompendo tanto a inicialização do ADS1256 quanto a montagem do cartão SD.

Para eliminar esse risco, as primeiras linhas de `initSensors()` em `src/sensors.cpp` forçam explicitamente todos os três Chip Selects para `HIGH` **antes** de chamar `SPI.begin()` e `ads.InitializeADC()`:

```cpp
pinMode(PIN_SD_CS, OUTPUT);
digitalWrite(PIN_SD_CS, HIGH);
pinMode(PIN_ADS_CS, OUTPUT);
digitalWrite(PIN_ADS_CS, HIGH);
pinMode(PIN_MAX_CS, OUTPUT);
digitalWrite(PIN_MAX_CS, HIGH);
```

---

## 3. DETALHAMENTO MÓDULO A MÓDULO (CÓDIGO, LÓGICA E BIBLIOTECAS)

### 3.1. `src/config.h` — Configurações Globais e Estruturas de Dados

O arquivo `config.h` é incluído por todos os módulos do sistema e concentra:

1. **Estrutura `DadosSensores`:**
   Agrupa em um único pacote todas as grandezas medidas em um ciclo de amostragem:
   ```cpp
   struct DadosSensores {
     unsigned long tempoMs; // Instante da leitura retornado por millis() [ms]
     long celulaCarga;      // Contagem bruta de 24 bits com sinal do ADS1256 (AIN6-AIN7)
     float ntc1;            // Temperatura do NTC 1 (AIN0) [°C]
     float ntc2;            // Temperatura do NTC 2 (AIN1) [°C]
     float ntc3;            // Temperatura do NTC 3 (AIN2) [°C]
     float termopar;        // Temperatura do Termopar K (MAX6675) [°C]
   };
   ```
2. **Constantes de Calibração e Conversão ADC:**
   * `TEMPO_CALIBRACAO` (`10000`): define a janela de $10.000\text{ ms}$ ($10\text{ s}$) para a rotina de calibração da célula de carga.
   * `ADS_VREF` (`2.5`): tensão de referência interna/externa de $2{,}5\text{ V}$ do ADS1256, usada pela biblioteca para converter contagens brutas em Volts.
   * `NUM_TAXAS` (`5`) e `TAXA_PADRAO` (`2`): definem a quantidade de opções de frequência de amostragem e o índice inicial padrão (`2` $\rightarrow$ `1000 SPS`).
3. **Parâmetros Físicos dos Termistores NTC:**
   * `NTC_VCC` (`5.0`): tensão de excitação do divisor resistivo ($5{,}0\text{ V}$).
   * `NTC_R_FIXO` (`10000.0`): resistência do resistor fixo em série no divisor ($10\text{ k}\Omega$).
   * `NTC_R0` (`10000.0`) e `NTC_T0_KELVIN` (`298.15`): resistência nominal do NTC ($10\text{ k}\Omega$) na temperatura de referência de $25\text{ }^\circ\text{C}$ ($298{,}15\text{ K}$).
   * `NTC_BETA` (`3950.0`): constante $\beta$ do termistor em Kelvin.
4. **Credenciais da Rede Sem Fio:**
   * `WIFI_SSID` (`"SCD"`) e `WIFI_PASS` (`"123456789"`): nome e senha da rede Wi-Fi criada pelo ESP32 no Modo Transmissão.

---

### 3.2. `src/sensors.h` e `src/sensors.cpp` — Subsistema de Aquisição Analógica

#### Bibliotecas Utilizadas
* `<SPI.h>`: driver nativo do barramento SPI de hardware do ESP32.
* `<ADS1256.h>` (*Curious Scientist ADS1256 Library*): comunicação com o ADC de 24 bits.
* `<math.h>`: fornece a função logarítmica `log()` para a Equação Beta e a macro `NAN` (*Not-a-Number*) para indicar falha de termopar aberto.

#### Instanciação do ADS1256 e Tabela de Taxas (`SPS`)
O objeto global `ads` é instanciado passando os pinos de controle e desabilitando `RESET` e `SYNC/PDWN` com `ADS1256::PIN_UNUSED` (pois no hardware do SCD II esses pinos são mantidos em nível lógico alto diretamente na placa):
```cpp
ADS1256 ads(PIN_ADS_DRDY, ADS1256::PIN_UNUSED, ADS1256::PIN_UNUSED, PIN_ADS_CS, ADS_VREF);
```
As taxas de amostragem disponíveis são mapeadas em dois vetores paralelos indexados de `0` a `4`:
* `valoresSPS[5] = {100, 500, 1000, 2000, 7500}`: valores numéricos para exibição no LCD, página web e gravação no relatório de calibração.
* `codigosDRATE[5] = {DRATE_100SPS, DRATE_500SPS, DRATE_1000SPS, DRATE_2000SPS, DRATE_7500SPS}`: bytes de configuração enviados ao registrador `DRATE` do ADS1256 por `setSampleRate(int indice)`.

Quando `setSampleRate(indice)` é chamada (seja pelo encoder ou via WebSocket), ela grava o novo byte no registrador `DRATE` do chip (`ads.setDRATE(...)`) e invalida o cache de canal (`canalAtual = -1`), forçando a próxima leitura a aguardar o assentamento completo do filtro digital na nova velocidade. As funções auxiliares `getSampleRateIndex()`, `getSPSValue(indice)` e `findSPSIndex(valorSPS)` traduzem índices ($0\dots 4$) para valores em SPS ($100\dots 7500$) e vice-versa.

#### Lógica de Multiplexação e Ganho: `selecionarCanal(int canal)`
O ADS1256 possui um único núcleo conversor precedido por um multiplexador analógico (`MUX`) e um amplificador de ganho programável (`PGA`):
* **Para a Célula de Carga (`canal == CANAL_CELULA`, código `6`):** Células de carga em ponte de Wheatstone fornecem sinais diferenciais da ordem de poucos milivolts (tipicamente $2\text{ mV/V}$). Por isso, a função configura o amplificador no ganho máximo de **$64\times$ (`PGA_64`)**, resultando em um fundo de escala de $\pm 78{,}125\text{ mV}$, e seleciona o par diferencial **`DIFF_6_7`** (pino `AIN6` como positivo e `AIN7` como negativo).
* **Para os Termistores NTC (`canal` `0`, `1` ou `2`):** Os divisores resistivos operam em excursão ampla de tensão ($0\text{ V}$ a $5\text{ V}$). Por isso, a função reduz o ganho para **$1\times$ (`PGA_1`)**, cujo fundo de escala é $\pm 2 \cdot V_{\text{REF}} = \pm 5{,}0\text{ V}$, e seleciona o respectivo canal single-ended contra o pino comum `AINCOM`: **`SING_0`** (`AIN0`), **`SING_1`** (`AIN1`) ou **`SING_2`** (`AIN2`).

**Por que existe um `ads.readSingle()` de descarte após trocar o canal?**  
O filtro digital interno *sinc* do ADS1256 opera de forma contínua (pipeline). Quando o firmware escreve nos registradores `PGA` e `MUX`, a conversão que estava em andamento no integrador ainda contém a carga elétrica do canal anterior. Se o código lesse o dado imediatamente como válido, haveria *crosstalk* entre os canais (a leitura da célula de carga seria contaminada pela tensão do NTC e vice-versa). A chamada de descarte `ads.readSingle()` dentro de `selecionarCanal()` consome e joga fora essa amostra residual, garantindo que a chamada subsequente em `readLoadCell()` ou `readNTC()` capture uma amostra 100% estabilizada do novo canal. Além disso, a guarda `if (canal == canalAtual) return;` evita reconfigurar o chip e descartar amostras quando o mesmo canal é lido repetidamente (como ocorre durante os 10 segundos do Modo Calibração, que roda na velocidade máxima do ADC sem trocar de canal).

#### Funcionamento de `readLoadCell()`
Seleciona o canal `CANAL_CELULA` (`DIFF_6_7` com `PGA_64`) e retorna o inteiro assinado de 24 bits (`long`) obtido por `ads.readSingle()`, preservando total compatibilidade com o formato de contagens brutas usado nos scripts da equipe desde o SCD I.

#### Funcionamento de `readNTC(int channel)`
1. Valida se o canal está entre `0` e `2` e chama `selecionarCanal(channel)`.
2. Obtém a leitura bruta de 24 bits (`ads.readSingle()`) e a converte em tensão elétrica em Volts através de `ads.convertToVoltage(bruto)`.
3. **Proteção contra divisão por zero / sensor desconectado:** Se o cabo de um NTC romper ou entrar em curto-circuito com `GND` ou `VCC`, a tensão lida irá para $0{,}0\text{ V}$ ou $5{,}0\text{ V}$. Na equação do divisor de tensão, $V = V_{\text{CC}}$ causaria divisão por zero (`NTC_VCC - tensao == 0`), e $V \le 0$ causaria erro de domínio no logaritmo (`log(0)` $\rightarrow -\infty$). A condição `if (tensao <= 0.05 || tensao >= (NTC_VCC - 0.05)) return 0.0;` protege o cálculo e retorna `0.0` de forma segura.
4. **Cálculo de Resistência e Temperatura (Equação Beta):**
   Calcula a resistência instantânea do termistor $R_{\text{NTC}}$ e aplica a equação termométrica de Steinhart-Hart simplificada (Beta):
   $$R_{\text{NTC}} = R_{\text{fixo}} \cdot \frac{V_{\text{lida}}}{V_{\text{CC}} - V_{\text{lida}}}$$
   $$T_{[\text{K}]} = \frac{1}{\frac{1}{T_0} + \frac{1}{\beta}\ln\left(\frac{R_{\text{NTC}}}{R_0}\right)}, \quad T_{[{}^\circ\text{C}]} = T_{[\text{K}]} - 273{,}15$$

> **⚠️ NOTA TÉCNICA DE BANCADA (CALIBRAÇÃO DOS NTCs):**  
> Antes da campanha de ensaios, meça com um multímetro a tensão real da linha de alimentação dos divisores e o valor real dos resistores fixos soldados na placa do SCD II, ajustando `NTC_VCC` e `NTC_R_FIXO` em `src/config.h`.  
> Além disso, verifique a topologia física do divisor na PCB: a equação atual `resistencia = (NTC_R_FIXO * tensao) / (NTC_VCC - tensao)` assume a configuração **NTC no lado baixo (Low-Side)** (`VCC -> R_FIXO -> Pino AINx -> NTC -> GND`). Caso na placa o NTC esteja ligado ao `VCC` e o resistor fixo ao `GND` (**High-Side**), basta inverter a fração em `readNTC()` para:  
> `float resistencia = NTC_R_FIXO * (NTC_VCC - tensao) / tensao;`

#### Funcionamento de `readThermocouple()` (Driver SPI Direto para o MAX6675)
Diferentemente de bibliotecas prontas de Arduino para o MAX6675 que reconfiguram os pinos SPI como GPIOs comuns para fazer *bit-bang* (o que corromperia o barramento SPI de hardware compartilhado com o ADS1256 e o cartão MicroSD), `readThermocouple()` realiza a leitura usando uma **transação nativa do hardware SPI**:

1. **Temporização Não-Bloqueante de 250 ms:** O conversor interno do MAX6675 leva até $220\text{ ms}$ para concluir uma conversão analógico-digital, e essa conversão só ocorre enquanto seu pino $\overline{\text{CS}}$ está em `HIGH`. Se o microcontrolador abaixar o pino `PIN_MAX_CS` a cada ciclo de loop (ex.: a cada $2\text{ ms}$), o MAX6675 abortará a conversão em andamento e repetirá sempre o mesmo valor congelado. Por isso, o bloco `if (millis() - ultimaLeituraTermopar >= 250)` garante que o chip só seja acessado a cada $250\text{ ms}$; nas chamadas intermediárias, a função retorna instantaneamente `ultimoValorTermopar` sem ocupar o barramento SPI.
2. **Transação SPI (`4 MHz`, `MSBFIRST`, `SPI_MODE0`):**
   Abre a transação com `SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0))`, coloca `PIN_MAX_CS` em `LOW`, aguarda $1\text{ }\mu\text{s}$ de tempo de estabelecimento, lê o quadro de 16 bits com `SPI.transfer16(0)`, sobe `PIN_MAX_CS` para `HIGH` e encerra a transação.
3. **Decodificação do Quadro de 16 bits do MAX6675:**
   * **Bit 2 (`leitura & 0x04`):** É o bit de diagnóstico de falha do MAX6675. Se for `1`, o termopar Tipo K está rompido ou desconectado do borne; a função atribui `NAN` a `ultimoValorTermopar`.
   * **Bits 14 a 3 (`(leitura >> 3) * 0.25`):** Os 3 bits menos significativos (bits 2, 1 e 0) são descartados com o deslocamento `>> 3`. Os 12 bits restantes representam a temperatura sem sinal com resolução de $0{,}25\text{ }^\circ\text{C/LSB}$ (faixa de $0{,}00\text{ }^\circ\text{C}$ a $1023{,}75\text{ }^\circ\text{C}$).

---

### 3.3. `src/hmi.h` e `src/hmi.cpp` — Interface Homem-Máquina (Encoder e LCD 20x4)

#### Bibliotecas Utilizadas
* `<Wire.h>`: comunicação I2C nativa nos pinos 21 (`SDA`) e 22 (`SCL`).
* `<LiquidCrystal_I2C.h>`: controle do display alfanumérico 20 colunas $\times$ 4 linhas com expansor I2C PCF8574 no endereço `0x3F`.

#### Decodificação de Quadratura em `readEncoder()`
O Rotary Encoder possui duas chaves internas defasadas em $90^\circ$ elétricos (`CLK` e `DT`). A função `readEncoder()` detecta a **borda de descida** do sinal `PIN_ENC_CLK` (transição de `HIGH` para `LOW` comparando `currentClk` com `lastClkState`):
* No exato instante em que `CLK` cai para `LOW`, o estado lógico do pino `PIN_ENC_DT` determina o sentido de rotação do eixo:
  * Se `digitalRead(PIN_ENC_DT) == HIGH` $\rightarrow$ o canal `CLK` adiantou-se ao `DT` (**sentido horário**), retornando `+1`.
  * Se `digitalRead(PIN_ENC_DT) == LOW` $\rightarrow$ o canal `DT` adiantou-se ao `CLK` (**sentido anti-horário**), retornando `-1`.
* Se não houve borda de descida, retorna `0`.

#### Debounce por Software em `checkButtonClick()`
O botão `PIN_ENC_SW` opera com resistor de pull-up interno (`INPUT_PULLUP`), mantendo nível `HIGH` quando solto e indo a `LOW` quando pressionado. Para evitar que a trepidação mecânica da chave (*contact bounce*) dispare múltiplos cliques falsos (por exemplo, iniciar e parar a coleta no mesmo aperto), `checkButtonClick()` verifica duas condições:
1. Ocorreu uma borda de descida (`lastButtonState == HIGH && currentButtonState == LOW`).
2. Já transcorreram mais de **$50\text{ ms}$** desde o último clique válido (`millis() - lastDebounceTime > 50`).

#### Técnicas de Renderização Rápida no LCD I2C
A escrita via barramento I2C a $100\text{ kHz}$ através do expansor PCF8574 é uma operação lenta em sistemas embarcados: um único `lcd.clear()` seguido da reescrita das 4 linhas pode consumir de $15\text{ ms}$ a $25\text{ ms}$, o que derrubaria drasticamente a taxa de amostragem do motor-foguete. Para evitar esse gargalo, `hmi.cpp` adota duas técnicas fundamentais:

1. **Atualização Diferencial do Cursor em `updateMenuNavigation()`:**
   Em vez de apagar e redesenhar todo o menu a cada giro do encoder, a função move o cursor apenas na coluna `0` para apagar o caractere `>` da linha anterior (`prev_selectedMenu - 1`) com um espaço `" "` e desenhar `>` na nova linha (`selectedMenu - 1`).
2. **Limitação de Taxa de Atualização (*Throttling* de 500 ms via `fz_lcd`):**
   Durante os modos de **Coleta** e **Calibração**, o loop principal roda na máxima velocidade permitida pelo ADC e pelo cartão SD. As funções `updateCollectionScreen(long empuxo)` e `updateCalibrationScreen(int segundosRestantes, long leituraAtual)` usam o temporizador não-bloqueante `if (millis() - fz_lcd > 500)` para que o barramento I2C só seja acionado **duas vezes por segundo**, atualizando apenas os dígitos numéricos sem redesenhar os rótulos fixos da tela.

---

### 3.4. `src/storage.h` e `src/storage.cpp` — Persistência em SD, NVS e Calibração

#### Bibliotecas Utilizadas
* `<SD.h>` e `<SPI.h>`: sistema de arquivos FAT para cartões MicroSD via SPI.
* `<Preferences.h>`: API nativa do ESP32 para gravação de pares chave-valor na partição **NVS (Non-Volatile Storage)** da memória Flash interna.

#### Gerenciamento da Memória NVS (`Preferences` — Namespace `"storage"`)
A memória NVS mantém os dados salvos mesmo quando a bateria da bancada é desconectada. O módulo utiliza três chaves inteiras dentro do namespace `"storage"`:

| Chave NVS | Tipo | Valor Padrão | Função no Sistema |
| :--- | :---: | :---: | :--- |
| `"cont"` | `int` | `0` | Contador sequencial de ensaios. Incrementado em `startNewSession()` para gerar nomes únicos `/Coleta_1.txt`, `/Coleta_2.txt`, etc., impedindo que um novo disparo sobrescreva o ensaio anterior. |
| `"cal_cont"` | `int` | `0` | Contador sequencial de calibrações. Incrementado ao final de `runCalibrationRoutine()` para gerar `/Calibracao_1.txt`, `/Calibracao_2.txt`, etc. |
| `"taxa"` | `int` | `TAXA_PADRAO` (`2`) | Índice da taxa de amostragem do ADS1256 (`0` a `4`). Gravado por `saveSampleRate(indice)` e lido no boot por `loadSampleRate()`, garantindo que a frequência configurada persista após desligar o SCD II. |

#### Funcionamento de `saveData(DadosSensores &dados)`
Chamada a cada ciclo do modo de coleta:
1. Solicita a posse do semáforo mutex do cartão SD com timeout de 10 ticks (`xSemaphoreTake(sdMutex, (TickType_t)10) == pdTRUE`).
2. Abre o arquivo da sessão atual (`pathSave`, ex.: `/Coleta_5.txt`) em modo de anexação (`FILE_APPEND`).
3. Formata os 6 campos da estrutura `DadosSensores` em um buffer de caracteres usando `sprintf`:
   ```cpp
   sprintf(dataBuffer, "%lu %ld %.2f %.2f %.2f %.2f",
           dados.tempoMs,
           dados.celulaCarga,
           dados.ntc1,
           dados.ntc2,
           dados.ntc3,
           dados.termopar);
   ```
4. Escreve a linha com quebra de linha (`fileToSave.println(dataBuffer)`), fecha o arquivo (`fileToSave.close()`, forçando o *flush* da tabela FAT no cartão para não perder dados em caso de desligamento abrupto) e libera imediatamente o mutex (`xSemaphoreGive(sdMutex)`).

#### Funcionamento de `runCalibrationRoutine()`
Executa o levantamento automático de calibração de 10 segundos:
1. Exibe a tela de calibração no LCD (`showCalibrationScreen()`) e registra o instante inicial `tempoInicial = millis()`.
2. **Acumulador de 64 bits contra Overflow (`long long somaLeituras`):**
   Cada leitura do ADS1256 em 24 bits pode chegar a $\pm 8.388.607$ ($2^{23}-1$). Em 10 segundos a $7500\text{ SPS}$, dezenas de milhares de amostras são somadas. Se o acumulador fosse um `long` comum de 32 bits (limite de $\approx 2{,}14 \times 10^9$), a soma sofreria *integer overflow* em menos de 300 amostras, gerando uma média incorreta ou negativa. O uso de `long long somaLeituras` (64 bits com sinal, limite de $\approx 9{,}22 \times 10^{18}$) elimina qualquer possibilidade de estouro aritmético.
3. Durante o laço `while (millis() - tempoInicial < TEMPO_CALIBRACAO)`, lê continuamente `readLoadCell()`, incrementa `somaLeituras` e `quantidadeAmostras`, e atualiza a contagem regressiva no display.
4. Ao término dos 10 segundos, calcula a média inteira `media = (long)(somaLeituras / quantidadeAmostras)`.
5. Incrementa a chave `"cal_cont"` na NVS, adquire o `sdMutex` e grava o arquivo `/Calibracao_N.txt` contendo a média da célula de carga, o total de amostras lidas no período e a taxa configurada em SPS.
6. Exibe o valor médio no LCD por 2 segundos (`showCalibrationDone(media)`) antes de retornar ao menu principal.

---

### 3.5. `src/web_page.h`, `src/web_server.h` e `src/web_server.cpp` — Rede e Telemetria Web

#### Bibliotecas Utilizadas
* `<WiFi.h>`: controle do rádio Wi-Fi 802.11 b/g/n do ESP32.
* `<ESPAsyncWebServer.h>` e `<AsyncTCP.h>`: servidor HTTP assíncrono e WebSocket de alto desempenho.

#### Arquitetura Dual-Core e Isolamento de RF
* **Desligamento de Rádio fora de uso:** Na inicialização (`initWebServer()`) e ao sair da transmissão (`stopWebServer()`), o firmware executa `WiFi.softAPdisconnect(true)` e `WiFi.mode(WIFI_OFF)`. Isso traz dois benefícios cruciais para ensaios estáticos: reduz o consumo de corrente em cerca de $120\text{ mA}$ e **elimina picos de interferência eletromagnética (EMI de $2{,}4\text{ GHz}$) e quedas de tensão na linha de $3{,}3\text{ V}$** que poderiam introduzir ruído nas leituras de microvolts do ADS1256 durante a queima do motor.
* **Execução no Core 0:** Ao entrar no Modo Transmissão, `startWebServer()` instancia a tarefa `webServerTask` fixada no **Core 0** (`xTaskCreatePinnedToCore(..., 0)`), enquanto o loop de verificação do botão de saída continua rodando de forma responsiva no **Core 1**.

#### Rotas HTTP e Protocolo WebSocket (`/ws`)
1. **Rota `GET /`:** Toma o `sdMutex`, lê a página HTML/CSS/JS armazenada em memória Flash (`index_html[] PROGMEM` definida em `src/web_page.h`) e a envia ao navegador.
2. **Rota `GET /download`:** Recebe os parâmetros de URL `?file=<nome>&loc=sd`, verifica a existência do arquivo sob proteção do `sdMutex` e faz o *streaming* binário (`application/octet-stream`) com cabeçalho `Content-Disposition: attachment` para download direto no celular ou notebook.
3. **Protocolo de Comandos via WebSocket (`handleWebSocketMessage`):**
   * **`SD` / `LittleFS`:** Aciona `dir(bool op)`. Para não bloquear o cartão SD por muito tempo caso existam dezenas de arquivos, `dir()` **adquire e libera o `sdMutex` de forma granular a cada arquivo iterado** (`openNextFile()`, leitura de nome/tamanho e envio do JSON `{"name":"...","size":"...","type":"File","loc":"sd"}`).
   * **`Delete/SD:<nome>`:** Aciona `delete_file(true, content)`, adquirindo o `sdMutex` e removendo o arquivo do cartão via `SD.remove("/" + filename)`.
   * **`GET_SPS`:** Enviado automaticamente pelo navegador no evento `onOpen` do WebSocket. O ESP32 responde com `enviarTaxaAtual()`, transmitindo o JSON `{"sps":1000}` para posicionar o seletor `<select id="sel_sps">` da página web na taxa atual do hardware.
   * **`SPS:<valor>` (ex.: `SPS:2000`):** Enviado quando o operador altera o seletor de taxa na página web. O firmware converte o valor com `atoi()`, localiza o índice correspondente via `findSPSIndex()`, aplica imediatamente no ADS1256 com `setSampleRate(indice)`, salva na memória NVS com `saveSampleRate(indice)` e transmite a confirmação JSON para todos os clientes conectados. Simultaneamente, o `handleTransmissionMode()` no Core 1 detecta a mudança e atualiza o LCD local com `updateTransmissionRate()`.

---

### 3.6. `src/main.cpp` — Orquestrador e Máquina de Estados

O arquivo `main.cpp` coordena todos os subsistemas no Core 1 através de quatro flags booleanas de estado (`trans`, `coleta`, `calibrando` e `ajustandoTaxa`):

| Estado Operacional | Condição no `loop()` | Função Handler | Comportamento e Transição de Saída |
| :--- | :--- | :--- | :--- |
| **1. Transmissão Wi-Fi** | `trans == true` | `handleTransmissionMode()` | Monitora se a taxa SPS foi alterada pela página web para atualizar o LCD (`updateTransmissionRate`). Quando o botão do encoder é pressionado (`checkButtonClick()`), chama `stopWebServer()`, faz `trans = false` e redesenha o menu (`printMenu()`). |
| **2. Coleta de Ensaio** | `coleta == true` | `handleCollectionMode()` | Executa ciclicamente `readAllSensors(dados)`, grava a linha no SD com `saveData(dados)` e atualiza o empuxo no display a cada $500\text{ ms}$. Ao detectar clique no botão, chama `stopCollection()`, faz `coleta = false` e retorna ao menu. |
| **3. Calibração (10 s)** | `calibrando == true` | `handleCalibrationMode()` | Chama `runCalibrationRoutine()` (amostragem de 10 s + gravação de `/Calibracao_N.txt`), limpa a flag `calibrando = false` e retorna automaticamente ao menu principal. |
| **4. Ajuste de Taxa SPS** | `ajustandoTaxa == true` | `handleRateMode()` | Lê giros do encoder (`readEncoder()`) para percorrer as 5 taxas (`100` a `7500 SPS`) atualizando o visor. Ao clicar no botão, aplica a taxa no ADS1256 (`setSampleRate`), grava na NVS (`saveSampleRate`), exibe confirmação por 1 s e retorna ao menu. |
| **5. Menu Principal** | Todas as flags `false` | Bloco `else` do `loop()` | Atualiza a posição do cursor `>` conforme giros do encoder (`updateMenuNavigation()`) e, ao detectar clique (`checkButtonClick()`), entra no modo correspondente à linha selecionada (`selectedMenu` de `1` a `4`). |

---

## 4. FORMATO DOS ARQUIVOS GERADOS NO CARTÃO SD

Esta seção especifica a estrutura exata dos arquivos gravados na raiz do cartão MicroSD para orientar o desenvolvimento dos scripts de pós-processamento em **Python (Pandas/NumPy/Matplotlib)** ou **MATLAB**.

### 4.1. Arquivos de Ensaio: `/Coleta_N.txt`
A cada vez que o **Modo 1 (Coleta)** é iniciado, o firmware cria um arquivo sequencial (`/Coleta_1.txt`, `/Coleta_2.txt`, etc.) em formato **texto tabular separado por espaços simples (`' '`)**, sem linha de cabeçalho textual, com ponto (`.`) como separador decimal.

#### Exemplo de Conteúdo de `/Coleta_1.txt`
```text
14520 84210 26.45 26.38 26.51 28.25
14528 84235 26.45 26.39 26.50 28.25
14536 125490 26.46 26.40 26.51 28.25
14544 489320 26.48 26.41 26.52 31.50
14552 488910 26.52 26.43 26.54 45.75
```
*(Nota: caso o termopar Tipo K esteja desconectado durante o ensaio, a 6ª coluna registrará `nan`).*

#### Dicionário de Colunas de `/Coleta_N.txt`

| Índice (0-based) | Nome do Campo | Formato `sprintf` | Tipo / Unidade | Descrição para Pós-Processamento |
| :---: | :--- | :---: | :---: | :--- |
| **Coluna 0** | `tempoMs` | `%lu` | Inteiro sem sinal (`ms`) | Tempo em milissegundos desde que o ESP32 foi ligado (`millis()`). Para obter o vetor de tempo relativo do ensaio a partir de $t = 0\text{ s}$: $t_i = (\text{tempoMs}_i - \text{tempoMs}_0) / 1000$. |
| **Coluna 1** | `celulaCarga` | `%ld` | Inteiro com sinal (`contagens`) | Leitura diferencial bruta de 24 bits do ADS1256 (`AIN6-AIN7`, ganho $64\times$). Para converter em Força ($\text{N}$), subtraia a média da tara ($\text{Offset}$) e multiplique pelo fator de calibração da bancada: $F = (\text{celulaCarga} - \text{Offset}) \cdot K_{\text{cal}}$. |
| **Coluna 2** | `ntc1` | `%.2f` | Ponto flutuante (`°C`) | Temperatura do Termistor NTC 1 (`AIN0`), com 2 casas decimais. Retorna `0.00` se desconectado. |
| **Coluna 3** | `ntc2` | `%.2f` | Ponto flutuante (`°C`) | Temperatura do Termistor NTC 2 (`AIN1`), com 2 casas decimais. Retorna `0.00` se desconectado. |
| **Coluna 4** | `ntc3` | `%.2f` | Ponto flutuante (`°C`) | Temperatura do Termistor NTC 3 (`AIN2`), com 2 casas decimais. Retorna `0.00` se desconectado. |
| **Coluna 5** | `termopar` | `%.2f` | Ponto flutuante (`°C`) | Temperatura do Termopar Tipo K lido pelo MAX6675 (resolução física de $0{,}25\text{ }^\circ\text{C}$, atualizada a cada $250\text{ ms}$). Registra `nan` se o termopar estiver aberto. |

#### Exemplo de Leitura Rápida em Python (`pandas`)
```python
import pandas as pd

colunas = ["tempo_ms", "celula_raw", "ntc1_c", "ntc2_c", "ntc3_c", "termopar_c"]
df = pd.read_csv("Coleta_1.txt", sep=r"\s+", names=colunas, na_values=["nan", "NAN"])

# Tempo relativo em segundos a partir do início da gravação
df["tempo_s"] = (df["tempo_ms"] - df["tempo_ms"].iloc[0]) / 1000.0
```

---

### 4.2. Arquivos de Calibração: `/Calibracao_N.txt`
A cada execução do **Modo 2 (Calibração)**, o firmware amostra exclusivamente a célula de carga durante $10\text{ segundos}$ e gera um arquivo de relatório (`/Calibracao_1.txt`, `/Calibracao_2.txt`, etc.) contendo 3 linhas descritivas no formato `Chave: Valor`:

#### Exemplo de Conteúdo de `/Calibracao_1.txt`
```text
Media da Celula de Carga (10s): 84215
Amostras lidas: 9842
Taxa de amostragem: 1000 SPS
```

#### Significado dos Campos de `/Calibracao_N.txt`
1. **`Media da Celula de Carga (10s)`:** Média aritmética inteira (`long`) de todas as leituras brutas de 24 bits coletadas nos 10 segundos.
   * Quando executada **sem peso na bancada**, fornece o valor de **Tara ($\text{Offset}_0$)**.
   * Quando executada **com uma massa padrão conhecida $m_{\text{cal}}$**, permite determinar a constante de calibração linear da célula de carga:
     $$K_{\text{cal}} = \frac{m_{\text{cal}} \cdot g}{\text{Média}_{\text{com carga}} - \text{Média}_{\text{sem carga}}} \quad [\text{N / contagem}]$$
2. **`Amostras lidas`:** Quantidade total de conversões efetivamente lidas do ADS1256 durante a janela de $10\text{ s}$, útil para auditar a taxa efetiva de aquisição do barramento SPI.
3. **`Taxa de amostragem`:** Valor nominal em `SPS` que estava configurado no registrador `DRATE` do ADS1256 durante a calibração (`100`, `500`, `1000`, `2000` ou `7500 SPS`).
