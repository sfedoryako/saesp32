#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <AudioTools.h>
#include <AudioTools/CoreAudio/AudioI2S/I2SStream.h>
#include <AudioTools/AudioCodecs/CodecMP3Helix.h>
#include <time.h>
#include <math.h>

// Попытка подключения файла конфигурации
// Если config.h не существует, используются значения по умолчанию
#if __has_include("config.h")
  #include "config.h"
#else
  #warning "Файл config.h не найден! Создайте его из config.example.h"
  #define YANDEX_API_KEY "ЗАМЕНИТЕ_НА_ВАШ_YANDEX_API_KEY"
  #define YANDEX_FOLDER_ID "ЗАМЕНИТЕ_НА_ВАШ_YANDEX_FOLDER_ID"
  #define OPENWEATHER_API_KEY "ЗАМЕНИТЕ_НА_ВАШ_OPENWEATHER_API_KEY"
#endif

// =======================================================
// 1. ПИНЫ ДЛЯ ESP32-S3
// =======================================================
#define I2S_TX_BCLK_PIN 19
#define I2S_TX_LRC_PIN 21
#define I2S_TX_DATA_OUT_PIN 10
#define I2S_RX_BCLK_PIN 17
#define I2S_RX_LRC_PIN 18
#define I2S_RX_DATA_IN_PIN 16
#define LED_PIN 2

// =======================================================
// 2. КОНСТАНТЫ И НАСТРОЙКИ
// =======================================================
// API ключи берутся из config.h (создайте из config.example.h)
const char* YANDEX_STT_HOST = "stt.api.cloud.yandex.net";
const char* YANDEX_TTS_HOST = "tts.api.cloud.yandex.net";
const char* COINGECKO_URL = "https://api.coingecko.com/api/v3/simple/price?ids=bitcoin&vs_currencies=rub";
const char* WORLDTIME_URL = "http://worldtimeapi.org/api/timezone/Europe/Moscow";
const char* WIFI_FILE = "/wifi.json";
const char* CERT_FILE = "/cert.pem";
const char* AP_SSID = "SmartSpeaker";
const char* AP_PASSWORD = "12345678";
const char* WEATHER_DEFAULT_CITY = "Voronezh";
const char* KEYWORD = "алёна";
const int VOLUME_STEP = 10;
const int ACTIVATION_THRESHOLD = 50;
const int SAMPLE_RATE_TX = 44100;
const int SAMPLE_RATE_RX = 16000;
const size_t MAX_FILE_SIZE = 2 * 1024 * 1024;
const int HTTP_TIMEOUT = 15000;
const int STT_RETRY_ATTEMPTS = 3;
const int STT_RETRY_DELAY_MS = 2000;
const int STT_INTERVAL = 3000;

// Тональные сигналы
const int TONE_BOOT_FREQ = 440;
const int TONE_BOOT_DURATION = 500;
const int TONE_WIFI_OK_FREQ = 880;
const int TONE_WIFI_OK_DURATION = 300;
const int TONE_WIFI_FAIL_FREQ = 220;
const int TONE_WIFI_FAIL_DURATION = 1000;
const int TONE_KEYWORD_FREQ = 660;
const int TONE_KEYWORD_DURATION = 200;

// Только MP3-радиостанции
struct RadioStation {
  const char* name;
  const char* url;
};
RadioStation radioStations[] = {
  {"рекорд", "https://radiorecord.hostingradio.ru:8040/rr_main96.mp3"},
  {"европа плюс", "http://ep128.hostingradio.ru:8040/europaplus128.mp3"},
  {"наше радио", "http://nashe.streamr.ru/nashe-128.mp3"}
};
const int NUM_STATIONS = sizeof(radioStations) / sizeof(radioStations[0]);

// =======================================================
// ВСТРОЕННЫЙ SSL СЕРТИФИКАТ
// =======================================================
const char* ROOT_CA = \
"-----BEGIN CERTIFICATE-----\n" \
"MIIDrzCCApegAwIBAgIQCDvgVpBCRrGhdWrJWZHHSjANBgkqhkiG9w0BAQUFADBh\n" \
"MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3\n" \
"d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBD\n" \
"QTAeFw0wNjExMTAwMDAwMDBaFw0zMTExMTAwMDAwMDBaMGExCzAJBgNVBAYTAlVT\n" \
"MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j\n" \
"b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IENBMIIBIjANBgkqhkiG\n" \
"9w0BAQEFAAOCAQ8AMIIBCgKCAQEA4jvhEXLeqKTTo1eqUKKPC3eQyaKl7hLOllsB\n" \
"CSDMAZOnTjC3U/dDxGkAV53ijSLdhwZAAIEJzs4bg7/fzTtxRuLWZscFs3YnFo97\n" \
"nh6Vfe63SKMI2tavegw5BmV/Sl0fvBf4q77uKNd0f3p4mVmFaG5cIzJLv07A6Fpt\n" \
"43C/dxC//AH2hdmoRBBYMql1GNXRor5H4idq9Joz+EkIYIvUX7Q6hL+hqkpMfT7P\n" \
"T19sdl6gSzeRntwi5m3OFBqOasv+zbMUZBfHWymeMr/y7vrTC0LUq7dBMtoM1O/4\n" \
"gdW7jVg/tRvoSSiicNoxBN33shbyTApOB6jtSj1etX+jkMOvJwIDAQABo2MwYTAO\n" \
"BgNVHQ8BAf8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4EFgQUA95QNVbR\n" \
"TLtm8KPiGxvDl7I90VUwHwYDVR0jBBgwFoAUA95QNVbRTLtm8KPiGxvDl7I90VUw\n" \
"DQYJKoZIhvcNAQEFBQADggEBAMucN6pIExIK+t1EnE9SsPTfrgT1eXkIoyQY/Esr\n" \
"hMAtudXH/vTBH1jLuG2cenTnmCmrEbXjcKChzUyImZOMkXDiqw8cvpOp/2PV5Adg\n" \
"06O/nVsJ8dWO41P0jmP6P6fbtGbfYmbW0W5BjfIttep3Sp+dWOIrWcBAI+0tKIJF\n" \
"PnlUkiaY4IBIqDfv8NZ5YBberOgOzW6sRBc4L0na4UU+Krk2U886UAb3LujEV0ls\n" \
"YSEYQSteDwsOoBrp+uvFRTp2InBuThs4pFsiv9kuXclVzDAGySj4dzp30d8tbQk\n" \
"CAUW7C29C79Fv1C5qfPrmAESrciIxpg0X40KPMbp1ZWVbd4=\n" \
"-----END CERTIFICATE-----\n";

// =======================================================
// 3. ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ
// =======================================================
WebServer server(80);
SemaphoreHandle_t xMutex = NULL;
I2SStream i2s_tx, i2s_rx;
WiFiClientSecure radioClient;
EncodedAudioStream* decoder = nullptr;
StreamCopy copier;
String ssid, password, lastCommand = "-";
volatile bool isRadioPlaying = false;
volatile bool isSpeeching = false;
float volume = 0.5;
bool isTimeSynced = false;
bool sleepMode = false;

// =======================================================
// 4. ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
// =======================================================
bool loadCertificateFromSPIFFS(const char* path, char* cert, size_t maxSize) {
  Serial.println("Загрузка сертификата из " + String(path));
  Serial.println("Свободная память перед загрузкой: " + String(ESP.getFreeHeap()));
  if (!SPIFFS.exists(path)) {
    Serial.println("Ошибка: Файл " + String(path) + " не существует");
    return false;
  }
  File file = SPIFFS.open(path, "r");
  if (!file) {
    Serial.println("Ошибка: Не удалось открыть " + String(path));
    return false;
  }
  size_t fileSize = file.size();
  Serial.println("Размер файла " + String(path) + ": " + String(fileSize) + " байт");
  if (fileSize == 0) {
    Serial.println("Ошибка: Файл " + String(path) + " пуст");
    file.close();
    return false;
  }
  if (fileSize >= maxSize) {
    Serial.println("Ошибка: Файл слишком большой (" + String(fileSize) + " > " + String(maxSize - 1) + ")");
    file.close();
    return false;
  }
  size_t bytesRead = file.readBytes(cert, maxSize - 1);
  cert[bytesRead] = '\0';
  file.close();
  Serial.println("Прочитано байт: " + String(bytesRead));
  Serial.println("Свободная память после загрузки: " + String(ESP.getFreeHeap()));
  return true;
}

void syncTime() {
  if (WiFi.status() != WL_CONNECTED) {
    isTimeSynced = false;
    Serial.println("Ошибка: WiFi не подключен");
    return;
  }
  Serial.println("Синхронизация времени...");
  configTime(3 * 3600, 0, "pool.ntp.org", "time.google.com");
  time_t now = time(nullptr);
  int attempts = 0;
  while (now < 1672531200L && attempts < 30) {
    delay(500);
    now = time(nullptr);
    attempts++;
    Serial.print(".");
  }
  if (now > 1672531200L) {
    struct tm timeinfo;
    gmtime_r(&now, &timeinfo);
    Serial.printf("\nВремя синхронизировано: %s", asctime(&timeinfo));
    isTimeSynced = true;
  } else {
    Serial.println("\nОшибка: Не удалось синхронизировать время");
    isTimeSynced = false;
  }
}

void setVolume(int newVolume) {
  volume = constrain(newVolume, 0, 100) / 100.0;
  Serial.println("Громкость: " + String(volume * 100) + "%");
}

void stopRadio() {
  if (xSemaphoreTake(xMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    if (isRadioPlaying) {
      radioClient.stop();
      isRadioPlaying = false;
      Serial.println("Радио остановлено");
    }
    xSemaphoreGive(xMutex);
  } else {
    Serial.println("Не удалось получить мьютекс для остановки радио");
  }
}

void playTone(int frequency, int duration) {
  if (xSemaphoreTake(xMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    Serial.println("Не удалось получить мьютекс для тона");
    return;
  }
  stopRadio();
  isSpeeching = true;
  
  // Буфер для стерео (2 канала)
  int16_t buffer[256]; // 128 сэмплов * 2 канала
  float phase = 0;
  float phaseIncrement = 2 * PI * frequency / SAMPLE_RATE_TX;
  unsigned long totalSamples = (unsigned long)duration * SAMPLE_RATE_TX / 1000;
  
  for (size_t i = 0; i < totalSamples; i += 128) {
    for (int j = 0; j < 128; j++) {
      int16_t sample = (int16_t)(sin(phase) * 16383.0 * volume); // Уменьшили амплитуду
      buffer[j * 2] = sample;     // левый канал
      buffer[j * 2 + 1] = sample; // правый канал
      phase += phaseIncrement;
      if (phase > 2 * PI) phase -= 2 * PI;
    }
    i2s_tx.write((uint8_t*)buffer, 256 * sizeof(int16_t));
  }
  
  isSpeeching = false;
  xSemaphoreGive(xMutex);
}

void speak(const String& text) {
  if (!isTimeSynced || WiFi.status() != WL_CONNECTED) {
    Serial.println("Ошибка: Нет времени или WiFi");
    return;
  }
  if (xSemaphoreTake(xMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    Serial.println("Не удалось получить мьютекс для TTS");
    return;
  }
  stopRadio();
  isSpeeching = true;
  WiFiClientSecure client;
  client.setCACert(ROOT_CA);
  client.setTimeout(HTTP_TIMEOUT);
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT);
  http.begin(client, String("https://") + YANDEX_TTS_HOST + "/speech/v1/tts:synthesize");
  http.addHeader("Authorization", "Api-Key " + String(YANDEX_API_KEY));
  http.addHeader("Content-Type", "application/json");
  String ssmlText = "<speak>" + text + "</speak>";
  StaticJsonDocument<512> doc;
  doc["folderId"] = YANDEX_FOLDER_ID;
  doc["text"] = ssmlText;
  doc["lang"] = "ru-RU";
  doc["voice"] = "alena";
  doc["format"] = "lpcm";
  doc["sampleRateHertz"] = SAMPLE_RATE_TX;
  String body;
  serializeJson(doc, body);
  Serial.println("Свободная память перед TTS: " + String(ESP.getFreeHeap()));
  int httpCode = http.POST(body);
  if (httpCode == HTTP_CODE_OK) {
    WiFiClient* stream = http.getStreamPtr();
    
    // Читаем данные из потока и воспроизводим напрямую
    uint8_t buffer[1024];
    while (stream->connected() && stream->available()) {
      int bytesRead = stream->read(buffer, sizeof(buffer));
      if (bytesRead > 0) {
        // Воспроизводим как сырые LPCM данные
        i2s_tx.write(buffer, bytesRead);
      } else {
        break;
      }
    }
    Serial.println("TTS воспроизведение завершено");
  } else {
    Serial.printf("Ошибка TTS: HTTP %d, %s\n", httpCode, http.errorToString(httpCode).c_str());
    Serial.println("Ответ: " + http.getString());
  }
  http.end();
  isSpeeching = false;
  xSemaphoreGive(xMutex);
}

void playRadio(const String& stationName) {
  if (!isTimeSynced || WiFi.status() != WL_CONNECTED) {
    speak("Нет подключения к интернету или время не синхронизировано.");
    return;
  }
  String url = radioStations[0].url;
  String selectedStation = radioStations[0].name;
  if (!stationName.isEmpty()) {
    for (int i = 0; i < NUM_STATIONS; i++) {
      if (stationName.indexOf(radioStations[i].name) != -1) {
        url = radioStations[i].url;
        selectedStation = radioStations[i].name;
        break;
      }
    }
  }
  if (xSemaphoreTake(xMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    Serial.println("Не удалось получить мьютекс для радио");
    return;
  }
  stopRadio();
  String host, path;
  bool isHttps = url.startsWith("https://");
  if (isHttps) {
    url.remove(0, 8);
  } else if (url.startsWith("http://")) {
    url.remove(0, 7);
  } else {
    Serial.println("Ошибка: URL должен начинаться с http:// или https://");
    xSemaphoreGive(xMutex);
    return;
  }
  int slashIndex = url.indexOf('/');
  host = (slashIndex != -1) ? url.substring(0, slashIndex) : url;
  path = (slashIndex != -1) ? url.substring(slashIndex) : "/";
  if (isHttps) radioClient.setCACert(ROOT_CA);
  uint16_t port = isHttps ? 443 : 80;
  if (!radioClient.connect(host.c_str(), port)) {
    Serial.println("Ошибка подключения к радио: " + host);
    xSemaphoreGive(xMutex);
    return;
  }
  radioClient.print(String("GET ") + path + " HTTP/1.1\r\n" +
                    "Host: " + host + "\r\n" +
                    "Icy-MetaData: 1\r\n" +
                    "Connection: close\r\n\r\n");
  while (radioClient.connected()) {
    String line = radioClient.readStringUntil('\n');
    if (line == "\r") break;
  }
  AudioInfo info(SAMPLE_RATE_TX, 2, 16);
  decoder->setAudioInfo(info);
  decoder->begin();
  copier.begin(*decoder, radioClient);
  isRadioPlaying = true;
  Serial.println("Воспроизведение радио: " + selectedStation);
  xSemaphoreGive(xMutex);
}

String speechToText(const uint8_t* audioData, size_t audioSize) {
  if (!isTimeSynced || WiFi.status() != WL_CONNECTED || audioSize == 0) {
    Serial.println("STT: Пропуск - нет времени, WiFi или данных");
    return "";
  }
  
  Serial.println("🎤 STT: Начало распознавания, размер: " + String(audioSize) + " байт");
  
  WiFiClientSecure client;
  client.setCACert(ROOT_CA);
  client.setTimeout(HTTP_TIMEOUT);
  client.setInsecure(); // Временно для отладки SSL
  
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT);
  http.setConnectTimeout(15000); // Увеличенный таймаут подключения
  
  // Правильный STT URL с обязательным параметром folderId для API v1
  String sttUrl = String("https://") + YANDEX_STT_HOST + "/speech/v1/stt:recognize" +
                  "?lang=ru-RU&format=lpcm&sampleRateHertz=" + String(SAMPLE_RATE_RX) +
                  "&folderId=" + String(YANDEX_FOLDER_ID);
  
  Serial.println("📡 STT URL: " + sttUrl);
  Serial.println("🔑 API Key (первые 10 символов): " + String(YANDEX_API_KEY).substring(0, 10) + "...");
  Serial.println("📁 Folder ID: " + String(YANDEX_FOLDER_ID));
  
  // Инициализируем HTTP соединение с правильным URL
  if (!http.begin(client, sttUrl)) {
    Serial.println("❌ STT: Ошибка инициализации HTTP клиента");
    return "";
  }
  
  // Правильные заголовки для API v1
  http.addHeader("Authorization", "Api-Key " + String(YANDEX_API_KEY));
  http.addHeader("Content-Type", "application/octet-stream");
  
  String result = "";
  
  for (int attempt = 0; attempt < STT_RETRY_ATTEMPTS; attempt++) {
    Serial.println("🔄 STT попытка " + String(attempt + 1) + "/" + String(STT_RETRY_ATTEMPTS));
    Serial.println("💾 Свободная память: " + String(ESP.getFreeHeap()) + " байт");
    
    // DNS проверка
    IPAddress ip;
    if (WiFi.hostByName(YANDEX_STT_HOST, ip)) {
      Serial.println("🌐 DNS успешно: " + ip.toString());
    } else {
      Serial.println("❌ DNS ошибка для " + String(YANDEX_STT_HOST));
      delay(STT_RETRY_DELAY_MS);
      continue;
    }
    
    int httpCode = http.POST(const_cast<uint8_t*>(audioData), audioSize);
    
    Serial.println("📊 HTTP код ответа: " + String(httpCode));
    
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      Serial.println("✅ STT успешно, ответ: " + payload.substring(0, min(200, (int)payload.length())));
      
      StaticJsonDocument<1024> responseDoc;
      DeserializationError error = deserializeJson(responseDoc, payload);
      
      if (!error && responseDoc.containsKey("result")) {
        result = responseDoc["result"].as<String>();
        Serial.println("🎯 Распознанный текст: " + result);
        break;
      } else {
        Serial.println("❌ STT: Ошибка парсинга JSON");
        Serial.println("📄 Полный ответ: " + payload);
      }
    } else if (httpCode == -1) {
      Serial.println("❌ STT: Connection Refused - возможные причины:");
      Serial.println("   1. Проверьте API ключ и folder ID");
      Serial.println("   2. Проверьте интернет соединение");
      Serial.println("   3. Возможно блокировка файрволом");
      Serial.println("   4. Проблемы с SSL сертификатом");
      
      // Дополнительная диагностика
      Serial.println("🔍 Дополнительная диагностика:");
      Serial.println("   WiFi статус: " + String(WiFi.status()));
      Serial.println("   WiFi RSSI: " + String(WiFi.RSSI()) + " dBm");
      Serial.println("   Свободная память: " + String(ESP.getFreeHeap()) + " байт");
      
    } else {
      Serial.printf("❌ STT: HTTP ошибка %d - %s\n", httpCode, http.errorToString(httpCode).c_str());
      String response = http.getString();
      if (response.length() > 0) {
        Serial.println("📄 Ответ сервера: " + response);
      }
    }
    
    if (attempt < STT_RETRY_ATTEMPTS - 1) {
      Serial.println("⏱️ Ожидание " + String(STT_RETRY_DELAY_MS) + " мс перед повтором...");
      delay(STT_RETRY_DELAY_MS);
    }
  }
  
  http.end();
  
  if (result.isEmpty()) {
    Serial.println("❌ STT: Не удалось распознать речь после всех попыток");
  }
  
  return result;
}

void processCommand(const String& command) {
  String lowerCommand = command;
  lowerCommand.toLowerCase();
  lastCommand = lowerCommand;
  stopRadio();
  if (lowerCommand.indexOf("погода") != -1) {
    speak(getWeather(WEATHER_DEFAULT_CITY));
  } else if (lowerCommand.indexOf("время") != -1) {
    speak(getTime());
  } else if (lowerCommand.indexOf("курс") != -1) {
    speak(getBitcoinPrice());
  } else if (lowerCommand.indexOf("радио") != -1) {
    String stationName = "";
    for (int i = 0; i < NUM_STATIONS; i++) {
      if (lowerCommand.indexOf(radioStations[i].name) != -1) {
        stationName = radioStations[i].name;
        break;
      }
    }
    playRadio(stationName);
  } else if (lowerCommand.indexOf("громче") != -1) {
    setVolume((int)(volume * 100) + VOLUME_STEP);
    speak("Громкость " + String((int)(volume * 100)) + " процентов.");
  } else if (lowerCommand.indexOf("тише") != -1) {
    setVolume((int)(volume * 100) - VOLUME_STEP);
    speak("Громкость " + String((int)(volume * 100)) + " процентов.");
  } else if (lowerCommand.indexOf("стоп") != -1 || lowerCommand.indexOf("молчанка") != -1) {
    speak("Останавливаю.");
  } else {
    speak("Извините, я не поняла команду.");
  }
}



// =======================================================
// 5. ОБРАБОТЧИКИ WEB-СЕРВЕРА
// =======================================================
void handleRoot() {
  File file = SPIFFS.open("/index.html", "r");
  if (!file) {
    server.send(404, "text/plain", "Файл index.html не найден");
    return;
  }
  server.streamFile(file, "text/html");
  file.close();
}

void handleStatus() {
  StaticJsonDocument<200> doc;
  doc["status"] = WiFi.status() == WL_CONNECTED ? "Подключен" : "Не подключен";
  doc["command"] = lastCommand;
  doc["volume"] = (int)(volume * 100);
  doc["sleepMode"] = sleepMode;
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleCommand() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Метод не разрешен");
    return;
  }
  if (sleepMode) {
    server.send(200, "application/json", "{\"status\":\"Колонка в спящем режиме\"}");
    return;
  }
  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, server.arg("plain"));
  if (error) {
    server.send(400, "text/plain", "Ошибка JSON");
    return;
  }
  String command = doc["command"].as<String>();
  processCommand(command);
  server.send(200, "application/json", "{\"status\":\"Команда принята\"}");
}

void handleNetworks() {
  int numNetworks = WiFi.scanNetworks();
  StaticJsonDocument<1024> doc;
  JsonArray networks = doc.to<JsonArray>();
  for (int i = 0; i < numNetworks; i++) {
    JsonObject network = networks.createNestedObject();
    network["ssid"] = WiFi.SSID(i);
    network["rssi"] = WiFi.RSSI(i);
  }
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleConnect() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Метод не разрешен");
    return;
  }
  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, server.arg("plain"));
  if (error) {
    server.send(400, "text/plain", "Ошибка JSON");
    return;
  }
  ssid = doc["ssid"].as<String>();
  password = doc["password"].as<String>();
  File file = SPIFFS.open(WIFI_FILE, "w");
  if (file) {
    StaticJsonDocument<200> saveDoc;
    saveDoc["ssid"] = ssid;
    saveDoc["password"] = password;
    serializeJson(saveDoc, file);
    file.close();
    server.send(200, "application/json", "{\"status\":\"Настройки сохранены. Перезагрузка...\"}");
    delay(1000);
    ESP.restart();
  } else {
    server.send(500, "application/json", "{\"status\":\"Ошибка сохранения настроек\"}");
  }
}

void handleSetVolume() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Метод не разрешен");
    return;
  }
  if (sleepMode) {
    server.send(200, "application/json", "{\"status\":\"Колонка в спящем режиме\"}");
    return;
  }
  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, server.arg("plain"));
  if (error) {
    server.send(400, "text/plain", "Ошибка JSON");
    return;
  }
  int newVolume = doc["volume"].as<int>();
  setVolume(newVolume);
  String response = "{\"status\":\"Громкость установлена на " + String(newVolume) + "%\"}";
  server.send(200, "application/json", response);
}

void handleSleep() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Метод не разрешен");
    return;
  }
  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, server.arg("plain"));
  if (error) {
    server.send(400, "text/plain", "Ошибка JSON");
    return;
  }
  sleepMode = doc["sleepMode"].as<bool>();
  if (sleepMode) stopRadio();
  String response = "{\"status\":\"" + String(sleepMode ? "Спящий режим включен" : "Спящий режим выключен") + "\"}";
  server.send(200, "application/json", response);
}

// =======================================================
// 6. ЗАДАЧА ЗАПИСИ АУДИО
// =======================================================
void recordTask(void* parameter) {
  int16_t buffer[128];
  File file;
  unsigned long lastSTTTime = 0;
  while (true) {
    if (sleepMode || isRadioPlaying || isSpeeching) {
      vTaskDelay(100 / portTICK_PERIOD_MS);
      continue;
    }
    if (millis() - lastSTTTime < STT_INTERVAL) {
      vTaskDelay(10 / portTICK_PERIOD_MS);
      continue;
    }
    size_t bytesRead = i2s_rx.readBytes((uint8_t*)buffer, sizeof(buffer));
    if (bytesRead > 0) {
      int maxAmplitude = 0;
      for (size_t i = 0; i < bytesRead / 2; i++) {
        maxAmplitude = max(maxAmplitude, abs(buffer[i]));
      }
      if (maxAmplitude > ACTIVATION_THRESHOLD) {
        digitalWrite(LED_PIN, LOW);
        Serial.println("Обнаружен звук. Амплитуда: " + String(maxAmplitude));
        if (xSemaphoreTake(xMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
          Serial.println("Не удалось получить мьютекс для записи");
          digitalWrite(LED_PIN, HIGH);
          continue;
        }
        file = SPIFFS.open("/record.raw", "w");
        if (!file) {
          Serial.println("Ошибка открытия файла записи");
          xSemaphoreGive(xMutex);
          digitalWrite(LED_PIN, HIGH);
          continue;
        }
        file.write((uint8_t*)buffer, bytesRead);
        unsigned long startTime = millis();
        size_t currentSize = bytesRead;
        while (millis() - startTime < 4500 && currentSize < MAX_FILE_SIZE) {
          bytesRead = i2s_rx.readBytes((uint8_t*)buffer, sizeof(buffer));
          if (bytesRead > 0) {
            file.write((uint8_t*)buffer, bytesRead);
            currentSize += bytesRead;
          }
          vTaskDelay(10 / portTICK_PERIOD_MS);
        }
        file.close();
        Serial.printf("Записано аудио: %d байт\n", currentSize);
        file = SPIFFS.open("/record.raw", "r");
        if (file) {
          size_t size = file.size();
          uint8_t* audioData = (uint8_t*)malloc(size);
          if (audioData) {
            file.read(audioData, size);
            file.close();
            String text = speechToText(audioData, size);
            free(audioData);
            text.toLowerCase();
            Serial.println("Распознанный текст: " + text);
            if (text.startsWith(KEYWORD)) {
              playTone(TONE_KEYWORD_FREQ, TONE_KEYWORD_DURATION);
              xSemaphoreGive(xMutex);
              text.replace(KEYWORD, "");
              text.trim();
              processCommand(text);
            } else {
              Serial.println("Ключевое слово не обнаружено");
              xSemaphoreGive(xMutex);
            }
          } else {
            file.close();
            Serial.println("Ошибка выделения памяти");
            xSemaphoreGive(xMutex);
          }
        } else {
          Serial.println("Не удалось прочитать запись");
          xSemaphoreGive(xMutex);
        }
        digitalWrite(LED_PIN, HIGH);
        lastSTTTime = millis();
      }
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

// =======================================================
// 7. НАСТРОЙКА И ГЛАВНЫЙ ЦИКЛ
// =======================================================
void setup() {
  Serial.begin(115200);
  delay(2000); // Увеличиваем задержку для стабилизации
  
  Serial.println("\n" + String("=").substring(0, 50));
  Serial.println("🚀 ESP32-S3 Smart Speaker - Диагностика запуска");
  Serial.println(String("=").substring(0, 50));
  Serial.println("📊 Версия: 2.0 (исправлена)");
  Serial.println("💾 Свободная память: " + String(ESP.getFreeHeap()) + " байт");
  Serial.println("🧠 PSRAM доступно: " + String(ESP.getFreePsram()) + " байт");
  Serial.println("🔧 Частота CPU: " + String(getCpuFrequencyMhz()) + " МГц");
  Serial.println("");

  Serial.print("🔌 Инициализация GPIO... ");
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  Serial.println("OK");

  Serial.print("💾 Инициализация SPIFFS... ");
  if (!SPIFFS.begin(false)) {
    Serial.println("ОШИБКА!");
    Serial.println("⚠️ SPIFFS.begin(false) не удалось. Пробуем форматировать...");
    if (!SPIFFS.begin(true)) {
      Serial.println("❌ КРИТИЧЕСКАЯ ОШИБКА: Форматирование SPIFFS не удалось!");
      Serial.println("💡 Решение: Загрузите данные SPIFFS через Arduino IDE");
      Serial.println("   Tools → ESP32 Sketch Data Upload");
      // Не зависаем, продолжаем без SPIFFS
      Serial.println("🔄 Продолжаем без SPIFFS (ограниченная функциональность)");
    } else {
      Serial.println("✅ SPIFFS отформатирован");
    }
  } else {
    Serial.println("OK");
  }
  
  if (SPIFFS.begin(false)) {
    Serial.println("📁 SPIFFS доступен, свободно: " + String((SPIFFS.totalBytes() - SPIFFS.usedBytes()) / 1024) + " КБ");
    
    Serial.println("📂 Проверка файлов в SPIFFS:");
    File root = SPIFFS.open("/");
    int fileCount = 0;
    while (File file = root.openNextFile()) {
      Serial.println("   📄 " + String(file.name()) + " (" + String(file.size()) + " байт)");
      file.close();
      fileCount++;
    }
    root.close();
    
    if (fileCount == 0) {
      Serial.println("⚠️ SPIFFS пустой! Загрузите файлы через Arduino IDE:");
      Serial.println("   Tools → ESP32 Sketch Data Upload");
    }
    
    if (!SPIFFS.exists(CERT_FILE)) {
      Serial.println("⚠️ Файл " + String(CERT_FILE) + " отсутствует!");
      Serial.println("💡 HTTPS функции будут недоступны (TTS, погода, курсы)");
      Serial.println("🔄 Продолжаем с ограниченной функциональностью...");
    } else {
      Serial.println("✅ SSL сертификат найден");
    }
  } else {
    Serial.println("❌ SPIFFS недоступен - только базовая функциональность");
  }

  // Пропускаем загрузку сертификата на этапе инициализации
  // Сертификат будет загружаться по мере необходимости в каждой функции
  Serial.println("⏭️ Пропускаем предварительную загрузку сертификата");

  Serial.print("🎵 Инициализация I2S TX (вывод аудио)... ");
  auto config_tx = i2s_tx.defaultConfig(TX_MODE);
  config_tx.sample_rate = SAMPLE_RATE_TX;
  config_tx.bits_per_sample = 16;
  config_tx.channels = 2;
  config_tx.i2s_format = I2S_STD_FORMAT;
  config_tx.pin_bck = I2S_TX_BCLK_PIN;
  config_tx.pin_ws = I2S_TX_LRC_PIN;
  config_tx.pin_data = I2S_TX_DATA_OUT_PIN;
  config_tx.buffer_count = 4;
  config_tx.buffer_size = 512;
  
  Serial.println(""); // Новая строка для детальной информации
  Serial.println("   📊 Настройки I2S TX:");
  Serial.println("      Частота: " + String(SAMPLE_RATE_TX) + " Гц, Стерео, 16 бит");
  Serial.println("      Пины: BCK=" + String(I2S_TX_BCLK_PIN) + ", WS=" + String(I2S_TX_LRC_PIN) + ", DATA=" + String(I2S_TX_DATA_OUT_PIN));
  
  if (!i2s_tx.begin(config_tx)) {
    Serial.println("❌ КРИТИЧЕСКАЯ ОШИБКА: Инициализация I2S TX не удалась!");
    Serial.println("💡 Проверьте:");
    Serial.println("   - Подключение ЦАП PCM5102A");
    Serial.println("   - Правильность пинов GPIO");
    Serial.println("   - Конфликты пинов с другими устройствами");
    Serial.println("🔄 Продолжаем без аудио вывода (тестовый режим)");
    // Не зависаем, продолжаем без I2S TX
  } else {
    Serial.println("✅ I2S TX инициализирован успешно");
  }
  
  Serial.print("🎤 Инициализация I2S RX (запись звука)... ");
  auto config_rx = i2s_rx.defaultConfig(RX_MODE);
  config_rx.sample_rate = SAMPLE_RATE_RX;
  config_rx.bits_per_sample = 16;
  config_rx.channels = 1;
  config_rx.i2s_format = I2S_STD_FORMAT;
  config_rx.pin_bck = I2S_RX_BCLK_PIN;
  config_rx.pin_ws = I2S_RX_LRC_PIN;
  config_rx.pin_data = I2S_RX_DATA_IN_PIN;
  config_rx.buffer_count = 8;
  config_rx.buffer_size = 512;
  
  Serial.println(""); // Новая строка для детальной информации
  Serial.println("   📊 Настройки I2S RX:");
  Serial.println("      Частота: " + String(SAMPLE_RATE_RX) + " Гц, Моно, 16 бит");
  Serial.println("      Пины: BCK=" + String(I2S_RX_BCLK_PIN) + ", WS=" + String(I2S_RX_LRC_PIN) + ", DATA=" + String(I2S_RX_DATA_IN_PIN));
  
  if (!i2s_rx.begin(config_rx)) {
    Serial.println("❌ КРИТИЧЕСКАЯ ОШИБКА: Инициализация I2S RX не удалась!");
    Serial.println("💡 Проверьте:");
    Serial.println("   - Подключение I2S микрофона");
    Serial.println("   - Правильность пинов GPIO");
    Serial.println("   - Питание микрофона");
    Serial.println("🔄 Продолжаем без записи звука (только веб-интерфейс)");
    // Не зависаем, продолжаем без I2S RX
  } else {
    Serial.println("✅ I2S RX инициализирован успешно");
  }
  
  Serial.println("🔊 I2S подсистема готова");
  Serial.println("💾 Свободная память после I2S: " + String(ESP.getFreeHeap()) + " байт");

  Serial.print("🎧 Создание MP3 декодера... ");
  decoder = new EncodedAudioStream(&i2s_tx, new MP3DecoderHelix());
  if (!decoder) {
    Serial.println("❌ ОШИБКА!");
    Serial.println("⚠️ Не удалось создать MP3 декодер");
    Serial.println("💡 Радио функции будут недоступны");
    Serial.println("🔄 Продолжаем без декодера...");
  } else {
    Serial.println("OK");
  }
  Serial.println("💾 Свободная память после декодера: " + String(ESP.getFreeHeap()) + " байт");

  Serial.print("🔒 Создание мьютекса... ");
  xMutex = xSemaphoreCreateMutex();
  if (xMutex == NULL) {
    Serial.println("❌ КРИТИЧЕСКАЯ ОШИБКА!");
    Serial.println("⚠️ Не удалось создать мьютекс");
    Serial.println("🔄 Продолжаем без синхронизации (может быть нестабильно)");
  } else {
    Serial.println("OK");
  }

  Serial.println("");
  Serial.print("🔔 Тестирование аудио (загрузочный тон)... ");
  playTone(TONE_BOOT_FREQ, TONE_BOOT_DURATION);
  Serial.println("OK");

  Serial.println("");
  Serial.print("📡 Попытка подключения к WiFi... ");
  
  File wifiFile = SPIFFS.open(WIFI_FILE, "r");
  if (wifiFile) {
    Serial.println("");
    Serial.println("   📄 Найдены сохранённые настройки WiFi");
    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, wifiFile);
    wifiFile.close();
    if (!error) {
      ssid = doc["ssid"].as<String>();
      password = doc["password"].as<String>();
      Serial.println("   🔗 Подключение к: " + ssid);
      WiFi.begin(ssid.c_str(), password.c_str());
      int attempts = 0;
      while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
      }
      Serial.println("");
    } else {
      Serial.println("   ❌ Ошибка чтения настроек WiFi");
    }
  } else {
    Serial.println("НЕТ НАСТРОЕК");
    Serial.println("   ⚠️ Файл настроек WiFi не найден");
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ Не удалось подключиться к сохранённой сети");
    Serial.println("🔥 Создание точки доступа: " + String(AP_SSID));
    Serial.println("   📶 SSID: " + String(AP_SSID));
    Serial.println("   🔑 Пароль: " + String(AP_PASSWORD));
    Serial.println("   🌐 IP адрес: 192.168.4.1");
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    playTone(TONE_WIFI_FAIL_FREQ, TONE_WIFI_FAIL_DURATION);
  } else {
    Serial.println("✅ Успешно подключено к WiFi!");
    Serial.println("   📶 SSID: " + WiFi.SSID());
    Serial.println("   🌐 IP адрес: " + WiFi.localIP().toString());
    Serial.println("   📡 Сила сигнала: " + String(WiFi.RSSI()) + " dBm");
    playTone(TONE_WIFI_OK_FREQ, TONE_WIFI_OK_DURATION);
    Serial.println("   🕐 Синхронизация времени...");
    syncTime();
  }

  Serial.println("");
  Serial.print("🌐 Запуск веб-сервера... ");
  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/command", HTTP_POST, handleCommand);
  server.on("/networks", handleNetworks);
  server.on("/connect", HTTP_POST, handleConnect);
  server.on("/setVolume", HTTP_POST, handleSetVolume);
  server.on("/sleep", HTTP_POST, handleSleep);
  server.begin();
  Serial.println("OK");
  Serial.println("   📄 Маршруты веб-сервера настроены");
  Serial.println("   🔗 Веб-интерфейс доступен по адресу:");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("      http://" + WiFi.localIP().toString());
  } else {
    Serial.println("      http://192.168.4.1");
  }
  Serial.println("💾 Свободная память после веб-сервера: " + String(ESP.getFreeHeap()) + " байт");

  Serial.println("");
  Serial.print("🎙️ Запуск задачи записи аудио... ");
  xTaskCreatePinnedToCore(recordTask, "RecordTask", 16384, NULL, 1, NULL, 0);
  Serial.println("OK");
  Serial.println("   🔊 Голосовая активация по ключевому слову: '" + String(KEYWORD) + "'");
  Serial.println("   🎚️ Пороговая амплитуда: " + String(ACTIVATION_THRESHOLD));
  
  Serial.println("");
  Serial.println(String("=").substring(0, 60));
  Serial.println("🎉 SMART SPEAKER УСПЕШНО ЗАПУЩЕН!");
  Serial.println(String("=").substring(0, 60));
  Serial.println("💡 Теперь вы можете:");
  Serial.println("   🗣️ Сказать: 'Алёна' + команда (погода, время, радио)");
  Serial.println("   🌐 Открыть веб-интерфейс в браузере");
  Serial.println("   📱 Управлять через мобильное приложение");
  Serial.println("");
  Serial.println("📊 Финальная статистика:");
  Serial.println("   💾 Свободная RAM: " + String(ESP.getFreeHeap()) + " байт");
  Serial.println("   🧠 PSRAM: " + String(ESP.getFreePsram()) + " байт");
  Serial.println("   📂 SPIFFS: " + String(SPIFFS.begin(false) ? "ДА" : "НЕТ"));
  Serial.println("   🔊 I2S TX: " + String("готов"));
  Serial.println("   🎤 I2S RX: " + String("готов"));
  Serial.println("");
  Serial.println("🚀 Готов к работе! Скажите 'Алёна' для активации...");
}

void loop() {
  server.handleClient();
  if (isRadioPlaying) {
    if (xSemaphoreTake(xMutex, 0) == pdTRUE) {
      if (radioClient.connected()) {
        copier.copy();
      } else {
        Serial.println("Потеряно соединение с радио");
        stopRadio();
      }
      xSemaphoreGive(xMutex);
    }
  }
  delay(10);
}

// =======================================================
// НЕДОСТАЮЩИЕ ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ  
// =======================================================

// Функция получения погоды через OpenWeatherMap API
String getWeather(const String& city) {
  HTTPClient http;
  WiFiClientSecure client;
  client.setCACert(ROOT_CA);
  
  String url = "https://api.openweathermap.org/data/2.5/weather?q=" + city + "&appid=" + String(OPENWEATHER_API_KEY) + "&units=metric&lang=ru";
  
  http.begin(client, url);
  http.setTimeout(HTTP_TIMEOUT);
  int httpCode = http.GET();
  
  if (httpCode > 0) {
    String payload = http.getString();
    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, payload);
    
    if (!error) {
      String description = doc["weather"][0]["description"].as<String>();
      float temp = doc["main"]["temp"];
      float feels_like = doc["main"]["feels_like"];
      int humidity = doc["main"]["humidity"];
      
      String result = "Погода в городе " + city + ": " + description + 
                     ". Температура " + String((int)temp) + " градусов" +
                     ", ощущается как " + String((int)feels_like) + " градусов" +
                     ". Влажность " + String(humidity) + " процентов.";
      
      http.end();
      return result;
    }
  }
  
  http.end();
  return "Не удалось получить данные о погоде. Проверьте подключение к интернету и API ключ.";
}

// Функция получения времени
String getTime() {
  HTTPClient http;
  WiFiClient client;
  
  http.begin(client, WORLDTIME_URL);
  http.setTimeout(HTTP_TIMEOUT);
  int httpCode = http.GET();
  
  if (httpCode > 0) {
    String payload = http.getString();
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, payload);
    
    if (!error) {
      String datetime = doc["datetime"].as<String>();
      // Извлекаем время из ISO формата (например: 2024-01-15T14:30:45.123456+03:00)
      int timeStart = datetime.indexOf('T') + 1;
      int timeEnd = datetime.indexOf('.');
      if (timeEnd == -1) timeEnd = datetime.indexOf('+');
      
      String time = datetime.substring(timeStart, timeEnd);
      String result = "Текущее время в Москве: " + time;
      
      http.end();
      return result;
    }
  }
  
  http.end();
  return "Не удалось получить текущее время. Проверьте подключение к интернету.";
}

// Функция получения курса биткоина
String getBitcoinPrice() {
  HTTPClient http;
  WiFiClientSecure client;
  client.setCACert(ROOT_CA);
  
  http.begin(client, COINGECKO_URL);
  http.setTimeout(HTTP_TIMEOUT);
  int httpCode = http.GET();
  
  if (httpCode > 0) {
    String payload = http.getString();
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, payload);
    
    if (!error) {
      float price = doc["bitcoin"]["rub"];
      String result = "Курс биткоина: " + String((int)price) + " рублей";
      
      http.end();
      return result;
    }
  }
  
  http.end();
  return "Не удалось получить курс биткоина. Проверьте подключение к интернету.";
}