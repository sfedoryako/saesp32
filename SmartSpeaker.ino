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

// Подключение файла конфигурации (создайте config.h из config.example.h)
#include "config.h"

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
  xSemaphoreTake(xMutex, portMAX_DELAY);
  if (isRadioPlaying) {
    radioClient.stop();
    isRadioPlaying = false;
    Serial.println("Радио остановлено");
  }
  xSemaphoreGive(xMutex);
}

void playTone(int frequency, int duration) {
  xSemaphoreTake(xMutex, portMAX_DELAY);
  stopRadio();
  isSpeeching = true;
  int16_t buffer[128];
  float phase = 0;
  float phaseIncrement = 2 * PI * frequency / SAMPLE_RATE_TX;
  unsigned long totalSamples = (unsigned long)duration * SAMPLE_RATE_TX / 1000;
  for (size_t i = 0; i < totalSamples; i += 128) {
    for (int j = 0; j < 128; j++) {
      buffer[j] = (int16_t)(sin(phase) * 32767.0 * volume);
      phase += phaseIncrement;
      if (phase > 2 * PI) phase -= 2 * PI;
    }
    i2s_tx.write((uint8_t*)buffer, 128 * sizeof(int16_t));
  }
  isSpeeching = false;
  xSemaphoreGive(xMutex);
}

void speak(const String& text) {
  if (!isTimeSynced || WiFi.status() != WL_CONNECTED) {
    Serial.println("Ошибка: Нет времени или WiFi");
    return;
  }
  xSemaphoreTake(xMutex, portMAX_DELAY);
  stopRadio();
  isSpeeching = true;
  WiFiClientSecure client;
  char cert[1500] = {0};
  if (!loadCertificateFromSPIFFS(CERT_FILE, cert, sizeof(cert))) {
    Serial.println("Ошибка: Не удалось загрузить сертификат для TTS");
    isSpeeching = false;
    xSemaphoreGive(xMutex);
    return;
  }
  client.setCACert(cert);
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
    copier.begin(i2s_tx, *stream);
    while (stream->connected() && stream->available()) {
      size_t bytesCopied = copier.copy();
      if (bytesCopied == 0) break;
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
  xSemaphoreTake(xMutex, portMAX_DELAY);
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
  char cert[1500] = {0};
  if (isHttps && !loadCertificateFromSPIFFS(CERT_FILE, cert, sizeof(cert))) {
    Serial.println("Ошибка: Не удалось загрузить сертификат для радио");
    xSemaphoreGive(xMutex);
    return;
  }
  if (isHttps) radioClient.setCACert(cert);
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
  if (!isTimeSynced || WiFi.status() != WL_CONNECTED || audioSize == 0) return "";
  WiFiClientSecure client;
  char cert[1500] = {0};
  if (!loadCertificateFromSPIFFS(CERT_FILE, cert, sizeof(cert))) {
    Serial.println("Ошибка: Не удалось загрузить сертификат для STT");
    return "";
  }
  client.setCACert(cert);
  client.setTimeout(HTTP_TIMEOUT);
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT);
  String sttUrl = String("https://") + YANDEX_STT_HOST + "/speech/v1/stt:recognize";
  String sttQuery = sttUrl + "?folderId=" + String(YANDEX_FOLDER_ID) +
                    "&lang=ru-RU&format=lpcm&sampleRateHertz=" + String(SAMPLE_RATE_RX);
  http.begin(client, sttQuery);
  http.addHeader("Authorization", "Api-Key " + String(YANDEX_API_KEY));
  http.addHeader("Content-Type", "application/octet-stream");
  String result = "";
  for (int attempt = 0; attempt < STT_RETRY_ATTEMPTS; attempt++) {
    Serial.println("Свободная память перед STT: " + String(ESP.getFreeHeap()));
    int httpCode = http.POST(const_cast<uint8_t*>(audioData), audioSize);
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      StaticJsonDocument<1024> responseDoc;
      DeserializationError error = deserializeJson(responseDoc, payload);
      if (!error && responseDoc.containsKey("result")) {
        result = responseDoc["result"].as<String>();
        break;
      } else {
        Serial.println("STT: Ошибка парсинга: " + payload);
      }
    } else {
      Serial.printf("STT: Ошибка %d, %s\n", httpCode, http.errorToString(httpCode).c_str());
      Serial.println("Ответ: " + http.getString());
    }
    delay(STT_RETRY_DELAY_MS);
  }
  http.end();
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

String getWeather(const String& city) {
  if (!isTimeSynced) return "Время не синхронизировано";
  WiFiClientSecure client;
  char cert[1500] = {0};
  if (!loadCertificateFromSPIFFS(CERT_FILE, cert, sizeof(cert))) {
    Serial.println("Ошибка: Не удалось загрузить сертификат для погоды");
    return "Ошибка получения погоды.";
  }
  client.setCACert(cert);
  HTTPClient http;
  String url = "https://api.openweathermap.org/data/2.5/weather?q=" + city +
               "&appid=" + OPENWEATHER_API_KEY + "&units=metric&lang=ru";
  http.begin(client, url);
  int httpCode = http.GET();
  String response = "Ошибка получения погоды.";
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    StaticJsonDocument<2048> doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (!error) {
      float temp = doc["main"]["temp"];
      String desc = doc["weather"][0]["description"];
      response = "В " + city + " температура " + String(temp, 0) + " градусов, " + desc + ".";
    } else {
      Serial.println("Ошибка парсинга погоды: " + payload);
    }
  } else {
    Serial.printf("Ошибка погоды: HTTP %d\n", httpCode);
    Serial.println("Ответ: " + http.getString());
  }
  http.end();
  return response;
}

String getBitcoinPrice() {
  if (!isTimeSynced) return "Время не синхронизировано";
  WiFiClientSecure client;
  char cert[1500] = {0};
  if (!loadCertificateFromSPIFFS(CERT_FILE, cert, sizeof(cert))) {
    Serial.println("Ошибка: Не удалось загрузить сертификат для курса");
    return "Ошибка получения цены биткоина.";
  }
  client.setCACert(cert);
  HTTPClient http;
  http.begin(client, COINGECKO_URL);
  int httpCode = http.GET();
  String response = "Ошибка получения цены биткоина.";
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (!error) {
      float price = doc["bitcoin"]["rub"];
      response = "Цена биткоина: " + String(price, 2) + " рублей.";
    } else {
      Serial.println("Ошибка парсинга курса: " + payload);
    }
  } else {
    Serial.printf("Ошибка курса: HTTP %d\n", httpCode);
    Serial.println("Ответ: " + http.getString());
  }
  http.end();
  return response;
}

String getTime() {
  time_t now = time(nullptr);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  char timeStr[64];
  strftime(timeStr, sizeof(timeStr), "%H:%M", &timeinfo);
  return String("Текущее время: ") + timeStr + " по московскому времени.";
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
        if (xSemaphoreTake(xMutex, 10 / portTICK_PERIOD_MS) != pdTRUE) {
          Serial.println("Не удалось получить мьютекс");
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
  delay(1000);
  Serial.println("Диагностика ESP32-S3 начата...");
  Serial.println("Свободная память на старте: " + String(ESP.getFreeHeap()));
  Serial.println("PSRAM доступно: " + String(ESP.getFreePsram()));

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  Serial.println("Инициализация SPIFFS...");
  if (!SPIFFS.begin(false)) {
    Serial.println("Ошибка: SPIFFS.begin(false) не удалось. Пробуем форматировать...");
    if (!SPIFFS.begin(true)) {
      Serial.println("Ошибка: Форматирование SPIFFS не удалось");
      while (true);
    }
  }
  Serial.println("SPIFFS инициализирован, свободно: " + String((SPIFFS.totalBytes() - SPIFFS.usedBytes()) / 1024) + " КБ");

  Serial.println("Проверка файлов в SPIFFS...");
  File root = SPIFFS.open("/");
  while (File file = root.openNextFile()) {
    Serial.println("Файл: " + String(file.name()) + ", размер: " + String(file.size()) + " байт");
    file.close();
  }
  root.close();

  if (!SPIFFS.exists(CERT_FILE)) {
    Serial.println("Ошибка: Файл " + String(CERT_FILE) + " отсутствует");
    while (true);
  }

  char cert[1500] = {0};
  if (!loadCertificateFromSPIFFS(CERT_FILE, cert, sizeof(cert))) {
    Serial.println("Ошибка загрузки сертификата");
    while (true);
  }

  Serial.println("Инициализация I2S TX...");
  I2SConfig cfg_tx = i2s_tx.defaultConfig(TX_MODE);
  cfg_tx.sample_rate = SAMPLE_RATE_TX;
  cfg_tx.bits_per_sample = 16;
  cfg_tx.i2s_format = I2S_STD_FORMAT;
  cfg_tx.pin_bck = I2S_TX_BCLK_PIN;
  cfg_tx.pin_ws = I2S_TX_LRC_PIN;
  cfg_tx.pin_data = I2S_TX_DATA_OUT_PIN;
  cfg_tx.buffer_count = 4;
  cfg_tx.buffer_size = 128;
  if (!i2s_tx.begin(cfg_tx)) {
    Serial.println("Ошибка инициализации I2S TX");
    while (true);
  }
  Serial.println("Инициализация I2S RX...");
  I2SConfig cfg_rx = i2s_rx.defaultConfig(RX_MODE);
  cfg_rx.sample_rate = SAMPLE_RATE_RX;
  cfg_rx.bits_per_sample = 16;
  cfg_rx.i2s_format = I2S_STD_FORMAT;
  cfg_rx.pin_bck = I2S_RX_BCLK_PIN;
  cfg_rx.pin_ws = I2S_RX_LRC_PIN;
  cfg_rx.pin_data = I2S_RX_DATA_IN_PIN;
  cfg_rx.buffer_count = 4;
  cfg_rx.buffer_size = 128;
  if (!i2s_rx.begin(cfg_rx)) {
    Serial.println("Ошибка инициализации I2S RX");
    while (true);
  }
  Serial.println("I2S инициализирован");
  Serial.println("Свободная память после I2S: " + String(ESP.getFreeHeap()));

  decoder = new EncodedAudioStream(&i2s_tx, new MP3DecoderHelix());
  if (!decoder) {
    Serial.println("Ошибка создания декодера");
    while (true);
  }
  Serial.println("Декодер инициализирован");
  Serial.println("Свободная память после декодера: " + String(ESP.getFreeHeap()));

  xMutex = xSemaphoreCreateMutex();
  if (xMutex == NULL) {
    Serial.println("Ошибка создания мьютекса");
    while (true);
  }

  playTone(TONE_BOOT_FREQ, TONE_BOOT_DURATION);

  Serial.println("Попытка подключения к WiFi...");
  File wifiFile = SPIFFS.open(WIFI_FILE, "r");
  if (wifiFile) {
    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, wifiFile);
    wifiFile.close();
    if (!error) {
      ssid = doc["ssid"].as<String>();
      password = doc["password"].as<String>();
      WiFi.begin(ssid.c_str(), password.c_str());
      int attempts = 0;
      while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
      }
    }
  }
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    Serial.println("Режим точки доступа: " + String(AP_SSID));
    playTone(TONE_WIFI_FAIL_FREQ, TONE_WIFI_FAIL_DURATION);
  } else {
    Serial.println("Подключено к WiFi: " + WiFi.localIP().toString());
    Serial.println("RSSI: " + String(WiFi.RSSI()));
    playTone(TONE_WIFI_OK_FREQ, TONE_WIFI_OK_DURATION);
    syncTime();
  }

  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/command", HTTP_POST, handleCommand);
  server.on("/networks", handleNetworks);
  server.on("/connect", HTTP_POST, handleConnect);
  server.on("/setVolume", HTTP_POST, handleSetVolume);
  server.on("/sleep", HTTP_POST, handleSleep);
  server.begin();
  Serial.println("WebServer запущен");
  Serial.println("Свободная память после WebServer: " + String(ESP.getFreeHeap()));

  xTaskCreatePinnedToCore(recordTask, "RecordTask", 16384, NULL, 1, NULL, 0);
  Serial.println("Задача записи запущена");
  Serial.println("Скрипт готов...");
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