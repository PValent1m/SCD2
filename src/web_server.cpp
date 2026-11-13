#include "web_server.h"
#include "config.h"
#include "hmi.h"
#include "sensors.h"
#include "storage.h"
#include "web_page.h"

#include "FS.h"
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <SD.h>
#include <WiFi.h>

// Handle para a tarefa do Core 0 (igual ao SCD 1)
TaskHandle_t webServerTaskHandle = NULL;

// Variaveis para o Web Server (iguais ao SCD 1)
const char *ssid = WIFI_SSID;
const char *password = WIFI_PASS;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// Envia a taxa de amostragem atual para a pagina web (ex: {"sps":1000})
void enviarTaxaAtual() {
  ws.textAll("{\"sps\":" + String(getSPSValue(getSampleRateIndex())) + "}");
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo *)arg;
  if (info->final && info->index == 0 && info->len == len &&
      info->opcode == WS_TEXT) {
    // Copia para um buffer local com espaco garantido para o terminador nulo
    // '\0'
    char msg[128];
    size_t copyLen = (len < sizeof(msg) - 1) ? len : (sizeof(msg) - 1);
    memcpy(msg, data, copyLen);
    msg[copyLen] = '\0';

    // Pagina pediu a taxa atual (enviado quando a pagina conecta)
    if (strcmp(msg, "GET_SPS") == 0) {
      enviarTaxaAtual();
      return;
    }

    // Pagina pediu para trocar a taxa (ex: "SPS:1000")
    if (strncmp(msg, "SPS:", 4) == 0) {
      int valor = atoi(msg + 4);
      int indice = findSPSIndex(valor);
      if (indice >= 0) {
        setSampleRate(indice);
        saveSampleRate(indice);
      }
      enviarTaxaAtual(); // Confirma para a pagina qual taxa ficou valendo
      return;
    }

    // Procura por "Delete/SD:" independentemente do caso
    const char *sd_key = "Delete/SD:";
    char *substring = strcasestr(msg, sd_key);
    if (substring != NULL) {
      char *content = substring + strlen(sd_key);
      Serial.println("Comando para deletar arquivo SD recebido.");
      delete_file(true, content);
      return;
    }

    // Procura por "Delete/LittleFS:"
    const char *lfs_key = "Delete/LittleFS:";
    substring = strcasestr(msg, lfs_key);
    if (substring != NULL) {
      char *content = substring + strlen(lfs_key);
      Serial.println("Comando para deletar arquivo LittleFS recebido.");
      delete_file(false, content);
      return;
    }

    if (strcmp(msg, "SD") == 0) {
      dir(true);
    }
    if (strcmp(msg, "LittleFS") == 0) {
      dir(false);
    }
  }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
             AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch (type) {
  case WS_EVT_CONNECT:
    Serial.printf("WebSocket client #%u connected from %s\n", client->id(),
                  client->remoteIP().toString().c_str());
    break;
  case WS_EVT_DISCONNECT:
    Serial.printf("WebSocket client #%u disconnected\n", client->id());
    break;
  case WS_EVT_DATA:
    handleWebSocketMessage(arg, data, len);
    break;
  case WS_EVT_PONG:
  case WS_EVT_ERROR:
    break;
  }
}

void dir(bool op) {
  if (op == false) {
    ws.textAll(
        "{\"name\":\"Listagem "
        "desativada.\",\"size\":\"\",\"type\":\"Info\",\"loc\":\"LittleFS\"}");
    return;
  }

  File root;
  if (xSemaphoreTake(sdMutex, (TickType_t)10) == pdTRUE) {
    root = SD.open("/");
    xSemaphoreGive(sdMutex);
  } else {
    ws.textAll("{\"name\":\"Falha ao acessar "
               "SD\",\"size\":\"\",\"type\":\"Error\",\"loc\":\"sd\"}");
    return;
  }

  if (!root || !root.isDirectory()) {
    return;
  }

  File file;
  while (true) {
    if (xSemaphoreTake(sdMutex, (TickType_t)10) == pdTRUE) {
      file = root.openNextFile();
      xSemaphoreGive(sdMutex);
    } else {
      break;
    }

    if (!file) {
      break;
    }

    if (xSemaphoreTake(sdMutex, (TickType_t)10) == pdTRUE) {
      String fsize = "";
      int bytes = file.size();
      if (bytes < 1024)
        fsize = String(bytes) + " B";
      else if (bytes < (1024 * 1024))
        fsize = String(bytes / 1024.0, 3) + " KB";
      else
        fsize = String(bytes / 1024.0 / 1024.0, 3) + " MB";

      String location = op ? "sd" : "LittleFS";
      ws.textAll("{\"name\":\"" + String(file.name()) + "\",\"size\":\"" +
                 fsize + "\",\"type\":\"" +
                 (file.isDirectory() ? "Dir" : "File") + "\",\"loc\":\"" +
                 location + "\"}");

      file.close();
      xSemaphoreGive(sdMutex);
    }
    delay(10);
  }

  if (xSemaphoreTake(sdMutex, (TickType_t)10) == pdTRUE) {
    root.close();
    xSemaphoreGive(sdMutex);
  }
}

void delete_file(bool op, String filename) {
  if (xSemaphoreTake(sdMutex, (TickType_t)10) == pdTRUE) {
    if (op) {
      Serial.println("Deletando do SD: /" + filename);
      SD.remove("/" + filename);
    } else {
      Serial.println("Deletando do LittleFS: /" + filename);
      LittleFS.remove("/" + filename);
    }
    xSemaphoreGive(sdMutex);
  } else {
    Serial.println("Nao foi possivel obter mutex para deletar arquivo.");
  }
}

// Tarefa executada no Core 0 (exatamente como no SCD 1)
void webServerTask(void *pvParameters) {
  Serial.println("Tarefa do Web Server iniciada no Core 0.");

  WiFi.softAP(ssid, password);
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());

  ws.onEvent(onEvent);
  server.addHandler(&ws);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (xSemaphoreTake(sdMutex, (TickType_t)10) == pdTRUE) {
      request->send_P(200, "text/html", index_html);
      xSemaphoreGive(sdMutex);
    } else {
      request->send(503, "text/plain",
                    "SD Card ocupado, tente novamente mais tarde :p.");
    }
  });

  server.on("/download", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (xSemaphoreTake(sdMutex, (TickType_t)10) == pdTRUE) {
      if (request->hasParam("file") && request->hasParam("loc")) {
        String fileName = request->getParam("file")->value();
        String location = request->getParam("loc")->value();
        String path = "/" + fileName;

        FS *filesystem;

        if (location == "sd") {
          filesystem = &SD;
        } else if (location == "LittleFS") {
          filesystem = &LittleFS;
        } else {
          request->send(400, "text/plain", "Localizacao invalida.");
          xSemaphoreGive(sdMutex);
          return;
        }

        if (filesystem->exists(path)) {
          Serial.println("Enviando arquivo para download: " + path +
                         " com o nome: " + fileName);
          request->send(*filesystem, path, "application/octet-stream", true);
        } else {
          Serial.println("Arquivo nao encontrado: " + path);
          request->send(404, "text/plain", "Arquivo nao encontrado.");
        }
      } else {
        request->send(400, "text/plain", "Parametros 'file' e 'loc' faltando.");
      }
      xSemaphoreGive(sdMutex);
    } else {
      request->send(503, "text/plain", "Recurso ocupado, tente novamente.");
    }
  });

  server.begin();
  for (;;) {
    ws.cleanupClients();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void initWebServer() { WiFi.mode(WIFI_OFF); }

void startWebServer() {
  if (webServerTaskHandle == NULL) {
    Serial.println("Ligando a transmissao... Criando tarefa no Core 0.");
    showTransmissionScreen();

    xTaskCreatePinnedToCore(webServerTask,   // Funcao da tarefa
                            "WebServerTask", // Nome
                            5000,            // Tamanho da Pilha (Stack Size)
                            NULL,            // Parametros
                            1,               // Prioridade
                            &webServerTaskHandle, // Handle da Tarefa
                            0);                   // Fixar no Core 0
  } else {
    Serial.println("Transmissao ja esta ligada.");
  }
}

void stopWebServer() {
  if (webServerTaskHandle != NULL) {
    Serial.println("Desligando a transmissao... Deletando tarefa do Core 0.");

    ws.closeAll();
    server.end();

    vTaskDelete(webServerTaskHandle);
    webServerTaskHandle = NULL;

    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    Serial.println("WiFi e Servidor desligados.");
  } else {
    Serial.println("Transmissao ja esta desligada.");
  }
}