/*
 * Microphone Test Script for ESP32-S3 Smart Speaker
 * 
 * Этот скрипт тестирует микрофон путем записи аудио и его воспроизведения
 * в реальном времени, так что вы можете слышать то, что подается на микрофон.
 * 
 * Возможности:
 * - Запись аудио с I2S микрофона
 * - Воспроизведение записанного аудио через I2S динамики
 * - Настройка усиления микрофона  
 * - Мониторинг уровня сигнала
 * - Веб-интерфейс для управления
 */

#include <WiFi.h>
#include <WebServer.h>
#include <AudioTools.h>
#include <AudioTools/CoreAudio/AudioI2S/I2SStream.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>

// =======================================================
// КОНФИГУРАЦИЯ ПИНОВ ESP32-S3
// =======================================================
#define I2S_TX_BCLK_PIN 19      // Выходной BCLK (динамики)
#define I2S_TX_LRC_PIN 21       // Выходной LRC (динамики)
#define I2S_TX_DATA_OUT_PIN 10  // Выходные данные (динамики)
#define I2S_RX_BCLK_PIN 17      // Входной BCLK (микрофон)
#define I2S_RX_LRC_PIN 18       // Входной LRC (микрофон)
#define I2S_RX_DATA_IN_PIN 16   // Входные данные (микрофон)
#define LED_PIN 2               // Встроенный LED для индикации

// =======================================================
// НАСТРОЙКИ АУДИО
// =======================================================
const int SAMPLE_RATE = 16000;        // Частота дискретизации
const int BITS_PER_SAMPLE = 16;       // Разрядность
const int CHANNELS_INPUT = 1;         // Моно вход (микрофон)
const int CHANNELS_OUTPUT = 2;        // Стерео выход (динамики)
const int BUFFER_SIZE = 512;          // Размер буфера
const int BUFFER_COUNT = 8;           // Количество буферов

// =======================================================
// НАСТРОЙКИ МИКРОФОНА
// =======================================================
const int MIN_AMPLITUDE = 50;         // Минимальная амплитуда для детекции звука
const int SILENCE_THRESHOLD = 30;     // Порог тишины
const float DEFAULT_GAIN = 2.0;       // Усиление по умолчанию
const int MONITORING_INTERVAL = 100;  // Интервал мониторинга в мс

// =======================================================
// НАСТРОЙКИ WI-FI
// =======================================================
const char* AP_SSID = "ESP32_MicTest";
const char* AP_PASSWORD = "12345678";

// =======================================================
// ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ
// =======================================================
I2SStream i2s_input;                 // Поток ввода (микрофон)
I2SStream i2s_output;                // Поток вывода (динамики)
WebServer server(80);                // Веб-сервер

// Переменные управления
volatile bool isTestActive = false;   // Активен ли тест
volatile bool isMonitoring = true;    // Мониторинг уровня сигнала
volatile float gain = DEFAULT_GAIN;   // Текущее усиление
volatile int currentAmplitude = 0;    // Текущая амплитуда сигнала
volatile int maxAmplitude = 0;        // Максимальная амплитуда
volatile bool ledState = false;       // Состояние LED

// Синхронизация
SemaphoreHandle_t amplitudeMutex;
QueueHandle_t audioQueue;

// Буфер для аудио данных
struct AudioBuffer {
  int16_t data[BUFFER_SIZE];
  size_t size;
};

// =======================================================
// ФУНКЦИИ ИНИЦИАЛИЗАЦИИ
// =======================================================

void setupI2S() {
  Serial.println("🎤 Настройка I2S микрофона...");
  
  // Конфигурация входного I2S (микрофон)
  auto config_input = i2s_input.defaultConfig(RX_MODE);
  config_input.sample_rate = SAMPLE_RATE;
  config_input.bits_per_sample = BITS_PER_SAMPLE;
  config_input.channels = CHANNELS_INPUT;
  config_input.i2s_format = I2S_STD_FORMAT;
  config_input.pin_bck = I2S_RX_BCLK_PIN;
  config_input.pin_ws = I2S_RX_LRC_PIN;
  config_input.pin_data = I2S_RX_DATA_IN_PIN;
  config_input.buffer_count = BUFFER_COUNT;
  config_input.buffer_size = BUFFER_SIZE;
  
  if (!i2s_input.begin(config_input)) {
    Serial.println("❌ Ошибка инициализации I2S микрофона!");
    return;
  }
  Serial.println("✅ I2S микрофон инициализирован");
  
  Serial.println("🔊 Настройка I2S динамиков...");
  
  // Конфигурация выходного I2S (динамики)
  auto config_output = i2s_output.defaultConfig(TX_MODE);
  config_output.sample_rate = SAMPLE_RATE;
  config_output.bits_per_sample = BITS_PER_SAMPLE;
  config_output.channels = CHANNELS_OUTPUT;
  config_output.i2s_format = I2S_STD_FORMAT;
  config_output.pin_bck = I2S_TX_BCLK_PIN;
  config_output.pin_ws = I2S_TX_LRC_PIN;
  config_output.pin_data = I2S_TX_DATA_OUT_PIN;
  config_output.buffer_count = BUFFER_COUNT;
  config_output.buffer_size = BUFFER_SIZE;
  
  if (!i2s_output.begin(config_output)) {
    Serial.println("❌ Ошибка инициализации I2S динамиков!");
    return;
  }
  Serial.println("✅ I2S динамики инициализированы");
}

void setupWiFi() {
  Serial.println("📡 Создание точки доступа...");
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.println("✅ Точка доступа создана:");
  Serial.println("   SSID: " + String(AP_SSID));
  Serial.println("   Password: " + String(AP_PASSWORD));
  Serial.println("   IP: " + WiFi.softAPIP().toString());
}

// =======================================================
// ВЕБРОУТЕРЫ
// =======================================================

void handleRoot() {
  String html = R"(
<!DOCTYPE html>
<html>
<head>
    <title>ESP32 Microphone Test</title>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background: #f0f2f5; }
        .container { max-width: 600px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
        h1 { color: #333; text-align: center; margin-bottom: 30px; }
        .status-panel { background: #e8f5e8; padding: 15px; border-radius: 8px; margin-bottom: 20px; }
        .control-panel { background: #f8f9fa; padding: 15px; border-radius: 8px; margin-bottom: 20px; }
        .meter { width: 100%; height: 30px; background: #ddd; border-radius: 15px; overflow: hidden; margin: 10px 0; }
        .meter-fill { height: 100%; background: linear-gradient(90deg, #4CAF50, #FFC107, #FF5722); transition: width 0.1s; }
        button { background: #007bff; color: white; border: none; padding: 12px 24px; border-radius: 6px; cursor: pointer; margin: 5px; font-size: 16px; }
        button:hover { background: #0056b3; }
        button.stop { background: #dc3545; }
        button.stop:hover { background: #c82333; }
        .slider-container { margin: 15px 0; }
        .slider { width: 100%; }
        .info { background: #d1ecf1; padding: 10px; border-radius: 5px; margin: 10px 0; font-size: 14px; }
        .amplitude { font-size: 24px; font-weight: bold; color: #007bff; }
        .status-active { color: #28a745; font-weight: bold; }
        .status-inactive { color: #6c757d; }
    </style>
</head>
<body>
    <div class="container">
        <h1>🎤 Тест микрофона ESP32-S3</h1>
        
        <div class="status-panel">
            <h3>Статус: <span id="status" class="status-inactive">Остановлен</span></h3>
            <div>Текущая амплитуда: <span id="amplitude" class="amplitude">0</span></div>
            <div>Максимальная амплитуда: <span id="maxAmplitude">0</span></div>
            <div class="meter">
                <div id="meter-fill" class="meter-fill" style="width: 0%"></div>
            </div>
        </div>
        
        <div class="control-panel">
            <h3>Управление</h3>
            <button id="startBtn" onclick="startTest()">▶ Начать тест</button>
            <button id="stopBtn" onclick="stopTest()" class="stop">⏹ Остановить тест</button>
            <br>
            <div class="slider-container">
                <label for="gainSlider">Усиление: <span id="gainValue">2.0</span>x</label>
                <input type="range" id="gainSlider" class="slider" min="0.1" max="10" step="0.1" value="2.0" onchange="setGain(this.value)">
            </div>
        </div>
        
        <div class="info">
            <h4>ℹ️ Инструкции:</h4>
            <p>• Нажмите <strong>"Начать тест"</strong> чтобы записывать звук с микрофона и воспроизводить его через динамики</p>
            <p>• Говорите в микрофон - вы должны услышать свой голос через динамики</p>
            <p>• Регулируйте усиление если звук слишком тихий или громкий</p>
            <p>• Следите за уровнем сигнала на индикаторе</p>
            <p>• Нажмите <strong>"Остановить тест"</strong> для завершения</p>
        </div>
        
        <div class="info">
            <h4>🔧 Технические характеристики:</h4>
            <p>• Частота дискретизации: 16000 Гц</p>
            <p>• Разрядность: 16 бит</p>
            <p>• Вход: Моно (микрофон)</p>
            <p>• Выход: Стерео (динамики)</p>
        </div>
    </div>

    <script>
        function updateStatus() {
            fetch('/status')
                .then(response => response.json())
                .then(data => {
                    document.getElementById('status').textContent = data.active ? 'Активен' : 'Остановлен';
                    document.getElementById('status').className = data.active ? 'status-active' : 'status-inactive';
                    document.getElementById('amplitude').textContent = data.amplitude;
                    document.getElementById('maxAmplitude').textContent = data.maxAmplitude;
                    
                    // Обновляем индикатор уровня
                    const percentage = Math.min((data.amplitude / 10000) * 100, 100);
                    document.getElementById('meter-fill').style.width = percentage + '%';
                    
                    document.getElementById('gainValue').textContent = data.gain;
                    document.getElementById('gainSlider').value = data.gain;
                })
                .catch(error => console.error('Error:', error));
        }
        
        function startTest() {
            fetch('/start', { method: 'POST' })
                .then(response => response.json())
                .then(data => {
                    console.log('Test started:', data);
                    updateStatus();
                });
        }
        
        function stopTest() {
            fetch('/stop', { method: 'POST' })
                .then(response => response.json())
                .then(data => {
                    console.log('Test stopped:', data);
                    updateStatus();
                });
        }
        
        function setGain(value) {
            fetch('/gain', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ gain: parseFloat(value) })
            })
            .then(response => response.json())
            .then(data => {
                console.log('Gain set:', data);
                updateStatus();
            });
        }
        
        // Обновляем статус каждые 200ms
        setInterval(updateStatus, 200);
        updateStatus();
    </script>
</body>
</html>
)";
  server.send(200, "text/html", html);
}

void handleStatus() {
  xSemaphoreTake(amplitudeMutex, portMAX_DELAY);
  int amp = currentAmplitude;
  int maxAmp = maxAmplitude;
  xSemaphoreGive(amplitudeMutex);
  
  String response = "{";
  response += "\"active\":" + String(isTestActive ? "true" : "false") + ",";
  response += "\"amplitude\":" + String(amp) + ",";
  response += "\"maxAmplitude\":" + String(maxAmp) + ",";
  response += "\"gain\":" + String(gain, 1);
  response += "}";
  
  server.send(200, "application/json", response);
}

void handleStart() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method not allowed");
    return;
  }
  
  isTestActive = true;
  Serial.println("🎤 Тест микрофона ЗАПУЩЕН");
  
  server.send(200, "application/json", "{\"status\":\"started\"}");
}

void handleStop() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method not allowed");
    return;
  }
  
  isTestActive = false;
  Serial.println("🎤 Тест микрофона ОСТАНОВЛЕН");
  
  server.send(200, "application/json", "{\"status\":\"stopped\"}");
}

void handleGain() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method not allowed");
    return;
  }
  
  // Парсим JSON запрос
  String body = server.arg("plain");
  if (body.length() > 0) {
    int startIndex = body.indexOf("\"gain\":") + 7;
    int endIndex = body.indexOf("}", startIndex);
    if (startIndex > 6 && endIndex > startIndex) {
      String gainStr = body.substring(startIndex, endIndex);
      float newGain = gainStr.toFloat();
      if (newGain >= 0.1 && newGain <= 10.0) {
        gain = newGain;
        Serial.println("🔊 Усиление установлено: " + String(gain, 1) + "x");
      }
    }
  }
  
  server.send(200, "application/json", "{\"status\":\"gain_set\", \"gain\":" + String(gain, 1) + "}");
}

void setupWebServer() {
  Serial.println("🌐 Настройка веб-сервера...");
  
  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/start", HTTP_POST, handleStart);
  server.on("/stop", HTTP_POST, handleStop);
  server.on("/gain", HTTP_POST, handleGain);
  
  server.begin();
  Serial.println("✅ Веб-сервер запущен на http://" + WiFi.softAPIP().toString());
}

// =======================================================
// ЗАДАЧИ РЕАЛЬНОГО ВРЕМЕНИ
// =======================================================

// Задача мониторинга микрофона
void microphoneTask(void* parameter) {
  int16_t inputBuffer[BUFFER_SIZE];
  int16_t outputBuffer[BUFFER_SIZE * 2]; // Стерео буфер
  
  Serial.println("🎤 Задача микрофона запущена");
  
  while (true) {
    if (isTestActive) {
      // Читаем данные с микрофона
      size_t bytesRead = i2s_input.readBytes((uint8_t*)inputBuffer, sizeof(inputBuffer));
      
      if (bytesRead > 0) {
        size_t samplesRead = bytesRead / sizeof(int16_t);
        
        // Анализируем амплитуду
        int amplitude = 0;
        for (size_t i = 0; i < samplesRead; i++) {
          amplitude = max(amplitude, abs(inputBuffer[i]));
        }
        
        // Обновляем статистику амплитуды
        xSemaphoreTake(amplitudeMutex, portMAX_DELAY);
        currentAmplitude = amplitude;
        maxAmplitude = max(maxAmplitude, amplitude);
        xSemaphoreGive(amplitudeMutex);
        
        // Применяем усиление и конвертируем в стерео
        for (size_t i = 0; i < samplesRead; i++) {
          int32_t amplifiedSample = (int32_t)(inputBuffer[i] * gain);
          // Ограничиваем значение чтобы избежать клиппинга
          amplifiedSample = constrain(amplifiedSample, -32767, 32767);
          
          // Дублируем в оба канала для стерео
          outputBuffer[i * 2] = (int16_t)amplifiedSample;     // Левый канал
          outputBuffer[i * 2 + 1] = (int16_t)amplifiedSample; // Правый канал
        }
        
        // Выводим звук через динамики
        i2s_output.write((uint8_t*)outputBuffer, samplesRead * 2 * sizeof(int16_t));
        
        // Мигаем LED при активности
        if (amplitude > MIN_AMPLITUDE) {
          ledState = !ledState;
          digitalWrite(LED_PIN, ledState ? LOW : HIGH);
        }
      }
    } else {
      // Пауза когда тест неактивен
      digitalWrite(LED_PIN, HIGH); // LED выключен
      vTaskDelay(pdMS_TO_TICKS(50));
    }
    
    // Небольшая задержка для других задач
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

// Задача мониторинга (обновление статистики)
void monitoringTask(void* parameter) {
  Serial.println("📊 Задача мониторинга запущена");
  
  while (true) {
    if (isMonitoring && isTestActive) {
      xSemaphoreTake(amplitudeMutex, portMAX_DELAY);
      int amp = currentAmplitude;
      int maxAmp = maxAmplitude;
      xSemaphoreGive(amplitudeMutex);
      
      // Выводим информацию в Serial Monitor каждые секунду
      static unsigned long lastPrint = 0;
      if (millis() - lastPrint > 1000) {
        Serial.printf("🎚️ Амплитуда: %d, Макс: %d, Усиление: %.1fx\n", amp, maxAmp, gain);
        lastPrint = millis();
      }
    }
    
    vTaskDelay(pdMS_TO_TICKS(MONITORING_INTERVAL));
  }
}

// =======================================================
// ОСНОВНЫЕ ФУНКЦИИ
// =======================================================

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n" + String('=', 50));
  Serial.println("🎤 ESP32-S3 MICROPHONE TEST");
  Serial.println(String('=', 50));
  Serial.println("Версия: 1.0");
  Serial.println("Свободная память: " + String(ESP.getFreeHeap()) + " байт");
  Serial.println();
  
  // Инициализация LED
  Serial.print("💡 Инициализация LED... ");
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH); // LED выключен (инвертированная логика)
  Serial.println("OK");
  
  // Создание мьютекса
  Serial.print("🔒 Создание мьютекса... ");
  amplitudeMutex = xSemaphoreCreateMutex();
  if (amplitudeMutex == NULL) {
    Serial.println("ОШИБКА!");
    while(1);
  }
  Serial.println("OK");
  
  // Создание очереди
  Serial.print("📦 Создание очереди... ");
  audioQueue = xQueueCreate(10, sizeof(AudioBuffer));
  if (audioQueue == NULL) {
    Serial.println("ОШИБКА!");
    while(1);
  }
  Serial.println("OK");
  
  // Настройка I2S
  setupI2S();
  
  // Настройка WiFi
  setupWiFi();
  
  // Настройка веб-сервера
  setupWebServer();
  
  // Запуск задач
  Serial.println("🚀 Запуск задач...");
  
  xTaskCreatePinnedToCore(
    microphoneTask,    // Функция задачи
    "MicrophoneTask",  // Имя задачи
    8192,              // Размер стека
    NULL,              // Параметр
    2,                 // Приоритет
    NULL,              // Хэндл задачи
    1                  // Ядро (1)
  );
  
  xTaskCreatePinnedToCore(
    monitoringTask,    // Функция задачи
    "MonitoringTask",  // Имя задачи
    2048,              // Размер стека
    NULL,              // Параметр
    1,                 // Приоритет
    NULL,              // Хэндл задачи
    0                  // Ядро (0)
  );
  
  Serial.println();
  Serial.println(String('=', 60));
  Serial.println("✅ МИКРОФОН ТЕСТ ГОТОВ К РАБОТЕ!");
  Serial.println(String('=', 60));
  Serial.println("📱 Откройте в браузере: http://" + WiFi.softAPIP().toString());
  Serial.println("🎤 Нажмите 'Начать тест' чтобы услышать микрофон");
  Serial.println("🔊 Говорите в микрофон - вы услышите свой голос");
  Serial.println();
  Serial.println("💡 Советы:");
  Serial.println("   • Если звук слишком тихий - увеличьте усиление");
  Serial.println("   • Если есть искажения - уменьшите усиление");
  Serial.println("   • Следите за уровнем сигнала на веб-странице");
  Serial.println();
}

void loop() {
  // Обработка веб-сервера
  server.handleClient();
  
  // Сброс максимальной амплитуды каждые 10 секунд
  static unsigned long lastReset = 0;
  if (millis() - lastReset > 10000) {
    xSemaphoreTake(amplitudeMutex, portMAX_DELAY);
    maxAmplitude = 0;
    xSemaphoreGive(amplitudeMutex);
    lastReset = millis();
  }
  
  delay(10);
}