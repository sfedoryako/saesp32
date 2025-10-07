/*
 * ESP32-S3 Smart Speaker v3.0 - Полная перезаписка
 * Исправлено: зависания, упрощена инициализация, добавлены звуковые сигналы
 * Все настройки встроены в код, config.h не требуется
 */

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

// =======================================================
// ВСТРОЕННАЯ КОНФИГУРАЦИЯ (замените на свои ключи!)
// =======================================================
#define YANDEX_API_KEY "ЗАМЕНИТЕ_НА_ВАШ_YANDEX_API_KEY"
#define YANDEX_FOLDER_ID "ЗАМЕНИТЕ_НА_ВАШ_YANDEX_FOLDER_ID"
#define OPENWEATHER_API_KEY "ЗАМЕНИТЕ_НА_ВАШ_OPENWEATHER_API_KEY"

// =======================================================
// ПИНЫ ESP32-S3
// =======================================================
#define I2S_TX_BCLK_PIN 19
#define I2S_TX_LRC_PIN 21
#define I2S_TX_DATA_OUT_PIN 10
#define I2S_RX_BCLK_PIN 17
#define I2S_RX_LRC_PIN 18
#define I2S_RX_DATA_IN_PIN 16
#define LED_PIN 2

// =======================================================
// КОНСТАНТЫ
// =======================================================
const char* AP_SSID = "SmartSpeaker";
const char* AP_PASSWORD = "12345678";
const char* KEYWORD = "алёна";
const char* WEATHER_DEFAULT_CITY = "Voronezh";

const int SAMPLE_RATE_TX = 44100;
const int SAMPLE_RATE_RX = 16000;
const int ACTIVATION_THRESHOLD = 100;
const int HTTP_TIMEOUT = 10000;

// Звуковые сигналы
const int TONE_BOOT = 440;          // Загрузка
const int TONE_WIFI_OK = 880;       // WiFi подключен  
const int TONE_WIFI_FAIL = 220;     // WiFi ошибка
const int TONE_KEYWORD = 660;       // Ключевое слово
const int TONE_COMMAND_START = 523; // Команда начата (До)
const int TONE_COMMAND_SUCCESS = 659; // Команда успешна (Ми)
const int TONE_COMMAND_ERROR = 349;   // Ошибка команды (Фа)
const int TONE_DURATION = 300;

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
// ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ
// =======================================================
WebServer server(80);
I2SStream i2s_tx, i2s_rx;
SemaphoreHandle_t xMutex = NULL;

String ssid = "";
String password = "";
String lastCommand = "-";
bool isTimeSynced = false;
bool isRecording = false;
bool isSpeaking = false;
float volume = 0.7;

// =======================================================
// БАЗОВЫЕ ФУНКЦИИ
// =======================================================

void playTone(int frequency, int duration) {
  Serial.println("🔊 Тон: " + String(frequency) + " Гц");
  
  int16_t buffer[256]; // 128 сэмплов * 2 канала (стерео)
  float phase = 0;
  float phaseIncrement = 2 * PI * frequency / SAMPLE_RATE_TX;
  unsigned long totalSamples = (unsigned long)duration * SAMPLE_RATE_TX / 1000;
  
  for (size_t i = 0; i < totalSamples; i += 128) {
    for (int j = 0; j < 128; j++) {
      int16_t sample = (int16_t)(sin(phase) * 8000.0 * volume);
      buffer[j * 2] = sample;     // левый канал
      buffer[j * 2 + 1] = sample; // правый канал
      phase += phaseIncrement;
      if (phase > 2 * PI) phase -= 2 * PI;
    }
    i2s_tx.write((uint8_t*)buffer, 256 * sizeof(int16_t));
  }
}

void speak(const String& text) {
  if (text.isEmpty()) return;
  
  Serial.println("🗣️ Говорю: " + text);
  isSpeaking = true;
  
  // Пока что заменяем TTS простым тоном
  playTone(TONE_COMMAND_SUCCESS, 500);
  
  isSpeaking = false;
}

// =======================================================
// API ФУНКЦИИ (упрощённые версии)
// =======================================================

String getWeather(const String& city) {
  Serial.println("🌤️ Получение погоды для " + city);
  playTone(TONE_COMMAND_START, TONE_DURATION);
  
  if (WiFi.status() != WL_CONNECTED) {
    playTone(TONE_COMMAND_ERROR, TONE_DURATION);
    return "Нет подключения к интернету";
  }
  
  HTTPClient http;
  WiFiClientSecure client;
  client.setCACert(ROOT_CA);
  client.setInsecure(); // Для тестирования
  
  String url = "https://api.openweathermap.org/data/2.5/weather?q=" + city + 
               "&appid=" + String(OPENWEATHER_API_KEY) + "&units=metric&lang=ru";
  
  http.begin(client, url);
  http.setTimeout(HTTP_TIMEOUT);
  int httpCode = http.GET();
  
  String result = "Ошибка получения погоды";
  
  if (httpCode == 200) {
    String payload = http.getString();
    StaticJsonDocument<1024> doc;
    
    if (deserializeJson(doc, payload) == DeserializationError::Ok) {
      String description = doc["weather"][0]["description"];
      float temp = doc["main"]["temp"];
      
      result = "Погода в городе " + city + ": " + description + 
               ". Температура " + String((int)temp) + " градусов.";
      
      playTone(TONE_COMMAND_SUCCESS, TONE_DURATION);
    } else {
      playTone(TONE_COMMAND_ERROR, TONE_DURATION);
    }
  } else {
    Serial.println("❌ HTTP Error: " + String(httpCode));
    playTone(TONE_COMMAND_ERROR, TONE_DURATION);
  }
  
  http.end();
  return result;
}

String getTime() {
  Serial.println("🕐 Получение времени");
  playTone(TONE_COMMAND_START, TONE_DURATION);
  
  time_t now = time(nullptr);
  if (now < 1000000000) { // Время не синхронизировано
    playTone(TONE_COMMAND_ERROR, TONE_DURATION);
    return "Время не синхронизировано";
  }
  
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  
  char timeStr[64];
  strftime(timeStr, sizeof(timeStr), "%H:%M", &timeinfo);
  
  playTone(TONE_COMMAND_SUCCESS, TONE_DURATION);
  return String("Текущее время: ") + timeStr;
}

String getBitcoinPrice() {
  Serial.println("₿ Получение курса биткоина");
  playTone(TONE_COMMAND_START, TONE_DURATION);
  
  if (WiFi.status() != WL_CONNECTED) {
    playTone(TONE_COMMAND_ERROR, TONE_DURATION);
    return "Нет подключения к интернету";
  }
  
  // Упрощённая версия - возвращаем тестовые данные
  playTone(TONE_COMMAND_SUCCESS, TONE_DURATION);
  return "Курс биткоина: примерно 2 миллиона рублей";
}

// =======================================================
// ОБРАБОТКА КОМАНД
// =======================================================

void processCommand(const String& command) {
  Serial.println("🎯 Обработка команды: " + command);
  lastCommand = command;
  
  String lowerCommand = command;
  lowerCommand.toLowerCase();
  
  if (lowerCommand.indexOf("погода") != -1) {
    String weather = getWeather(WEATHER_DEFAULT_CITY);
    speak(weather);
  } 
  else if (lowerCommand.indexOf("время") != -1) {
    String time = getTime();
    speak(time);
  }
  else if (lowerCommand.indexOf("курс") != -1 || lowerCommand.indexOf("биткоин") != -1) {
    String price = getBitcoinPrice();
    speak(price);
  }
  else if (lowerCommand.indexOf("громче") != -1) {
    volume = min(1.0f, volume + 0.1f);
    speak("Громкость " + String((int)(volume * 100)) + " процентов");
  }
  else if (lowerCommand.indexOf("тише") != -1) {
    volume = max(0.1f, volume - 0.1f);
    speak("Громкость " + String((int)(volume * 100)) + " процентов");
  }
  else if (lowerCommand.indexOf("стоп") != -1) {
    speak("Останавливаю");
  }
  else {
    playTone(TONE_COMMAND_ERROR, TONE_DURATION * 2);
    speak("Извините, не поняла команду");
  }
}

// =======================================================
// WEB СЕРВЕР
// =======================================================

void handleRoot() {
  String html = R"(
<!DOCTYPE html>
<html>
<head>
    <meta charset='UTF-8'>
    <title>Smart Speaker v3</title>
    <style>
        body { font-family: Arial; margin: 20px; background: #f0f0f0; }
        .container { max-width: 600px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; }
        button { padding: 10px 20px; margin: 5px; font-size: 16px; border: none; border-radius: 5px; cursor: pointer; }
        .success { background: #4CAF50; color: white; }
        .info { background: #2196F3; color: white; }
        .warning { background: #ff9800; color: white; }
        input { padding: 10px; margin: 5px; width: 200px; }
        #status { margin: 20px 0; padding: 10px; background: #e7e7e7; border-radius: 5px; }
    </style>
</head>
<body>
    <div class='container'>
        <h1>🎙️ Smart Speaker v3.0</h1>
        
        <div id='status'>Статус: Загрузка...</div>
        
        <h3>Голосовые команды:</h3>
        <button class='info' onclick='sendCommand("погода")'>🌤️ Погода</button>
        <button class='info' onclick='sendCommand("время")'>🕐 Время</button>
        <button class='info' onclick='sendCommand("курс")'>₿ Биткоин</button>
        <br>
        <button class='success' onclick='sendCommand("громче")'>🔊 Громче</button>
        <button class='warning' onclick='sendCommand("тише")'>🔉 Тише</button>
        <button class='warning' onclick='sendCommand("стоп")'>⏹️ Стоп</button>
        
        <h3>WiFi настройки:</h3>
        <input type='text' id='ssid' placeholder='WiFi SSID'>
        <input type='password' id='password' placeholder='Пароль'>
        <button class='success' onclick='connectWiFi()'>📶 Подключить</button>
        
        <h3>Сеть:</h3>
        <button class='info' onclick='scanNetworks()'>🔍 Сканировать WiFi</button>
        <div id='networks'></div>
        
        <script>
            function sendCommand(cmd) {
                fetch('/command', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({command: cmd})
                }).then(r => r.json()).then(data => {
                    document.getElementById('status').innerText = 'Команда: ' + cmd + ' - ' + data.status;
                });
            }
            
            function connectWiFi() {
                const ssid = document.getElementById('ssid').value;
                const password = document.getElementById('password').value;
                fetch('/connect', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({ssid: ssid, password: password})
                }).then(r => r.json()).then(data => {
                    document.getElementById('status').innerText = data.status;
                });
            }
            
            function scanNetworks() {
                fetch('/networks').then(r => r.json()).then(data => {
                    let html = '<h4>Найденные сети:</h4>';
                    data.forEach(net => {
                        html += '<div>' + net.ssid + ' (сигнал: ' + net.rssi + ' dBm)</div>';
                    });
                    document.getElementById('networks').innerHTML = html;
                });
            }
            
            // Обновление статуса каждые 3 секунды
            setInterval(() => {
                fetch('/status').then(r => r.json()).then(data => {
                    document.getElementById('status').innerText = 
                        'WiFi: ' + data.status + ' | Команда: ' + data.command + ' | Громкость: ' + data.volume + '%';
                });
            }, 3000);
        </script>
    </div>
</body>
</html>
  )";
  
  server.send(200, "text/html", html);
}

void handleStatus() {
  StaticJsonDocument<200> doc;
  doc["status"] = WiFi.status() == WL_CONNECTED ? "Подключен" : "Отключен";
  doc["command"] = lastCommand;
  doc["volume"] = (int)(volume * 100);
  doc["time"] = isTimeSynced ? "OK" : "Не синхронизировано";
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleCommand() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Только POST");
    return;
  }
  
  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, server.arg("plain"));
  
  if (error) {
    server.send(400, "text/plain", "Ошибка JSON");
    return;
  }
  
  String command = doc["command"];
  processCommand(command);
  
  server.send(200, "application/json", "{\"status\":\"Команда принята\"}");
}

void handleNetworks() {
  int numNetworks = WiFi.scanNetworks();
  StaticJsonDocument<1024> doc;
  JsonArray networks = doc.to<JsonArray>();
  
  for (int i = 0; i < min(numNetworks, 10); i++) {
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
    server.send(405, "text/plain", "Только POST");
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
  
  server.send(200, "application/json", "{\"status\":\"Перезагрузка для подключения...\"}");
  
  delay(1000);
  ESP.restart();
}

// =======================================================
// ЗАДАЧА ЗАПИСИ (упрощённая)
// =======================================================

void recordTask(void* parameter) {
  int16_t buffer[128];
  
  while (true) {
    if (isSpeaking) {
      vTaskDelay(100 / portTICK_PERIOD_MS);
      continue;
    }
    
    size_t bytesRead = i2s_rx.readBytes((uint8_t*)buffer, sizeof(buffer));
    
    if (bytesRead > 0) {
      // Проверяем амплитуду
      int maxAmplitude = 0;
      for (size_t i = 0; i < bytesRead / 2; i++) {
        maxAmplitude = max(maxAmplitude, abs(buffer[i]));
      }
      
      if (maxAmplitude > ACTIVATION_THRESHOLD && !isRecording) {
        digitalWrite(LED_PIN, LOW);
        Serial.println("🎤 Активация по звуку: " + String(maxAmplitude));
        playTone(TONE_KEYWORD, TONE_DURATION);
        
        // Упрощённая версия - обрабатываем тестовую команду
        processCommand("время");
        
        digitalWrite(LED_PIN, HIGH);
      }
    }
    
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

// =======================================================
// ОСНОВНЫЕ ФУНКЦИИ
// =======================================================

void setup() {
  Serial.begin(115200);
  delay(3000); // Больше времени для стабилизации
  
  Serial.println("\n🚀 Smart Speaker v3.0 - Упрощённая версия");
  Serial.println("=======================================");
  
  // Пины
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  
  // I2S упрощённая инициализация
  Serial.print("🎵 I2S TX... ");
  auto config_tx = i2s_tx.defaultConfig(TX_MODE);
  config_tx.sample_rate = SAMPLE_RATE_TX;
  config_tx.bits_per_sample = 16;
  config_tx.channels = 2;
  config_tx.pin_bck = I2S_TX_BCLK_PIN;
  config_tx.pin_ws = I2S_TX_LRC_PIN;
  config_tx.pin_data = I2S_TX_DATA_OUT_PIN;
  
  if (i2s_tx.begin(config_tx)) {
    Serial.println("OK");
    playTone(TONE_BOOT, TONE_DURATION);
  } else {
    Serial.println("ОШИБКА!");
  }
  
  Serial.print("🎤 I2S RX... ");
  auto config_rx = i2s_rx.defaultConfig(RX_MODE);
  config_rx.sample_rate = SAMPLE_RATE_RX;
  config_rx.bits_per_sample = 16;
  config_rx.channels = 1;
  config_rx.pin_bck = I2S_RX_BCLK_PIN;
  config_rx.pin_ws = I2S_RX_LRC_PIN;
  config_rx.pin_data = I2S_RX_DATA_IN_PIN;
  
  if (i2s_rx.begin(config_rx)) {
    Serial.println("OK");
  } else {
    Serial.println("ОШИБКА!");
  }
  
  // Мьютекс
  xMutex = xSemaphoreCreateMutex();
  
  // WiFi подключение
  Serial.print("📡 WiFi... ");
  WiFi.begin("", ""); // Пустые данные для начала AP mode
  
  delay(5000); // Ждём подключения
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Подключен!");
    Serial.println("IP: " + WiFi.localIP().toString());
    playTone(TONE_WIFI_OK, TONE_DURATION);
    
    // Синхронизация времени
    configTime(3 * 3600, 0, "pool.ntp.org");
    isTimeSynced = true;
  } else {
    Serial.println("AP Mode");
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    Serial.println("SSID: " + String(AP_SSID));
    Serial.println("IP: 192.168.4.1");
    playTone(TONE_WIFI_FAIL, TONE_DURATION);
  }
  
  // Веб-сервер
  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/command", HTTP_POST, handleCommand);
  server.on("/networks", handleNetworks);
  server.on("/connect", HTTP_POST, handleConnect);
  server.begin();
  
  Serial.println("🌐 Веб-сервер запущен");
  
  // Задача записи
  xTaskCreatePinnedToCore(recordTask, "RecordTask", 8192, NULL, 1, NULL, 0);
  
  Serial.println("✅ Инициализация завершена!");
  Serial.println("💡 Скажите что-то громко для активации");
  Serial.println("🌐 Откройте веб-интерфейс для управления");
  
  // Финальный сигнал готовности
  playTone(TONE_COMMAND_SUCCESS, TONE_DURATION);
  delay(200);
  playTone(TONE_COMMAND_SUCCESS, TONE_DURATION);
}

void loop() {
  server.handleClient();
  delay(10);
}