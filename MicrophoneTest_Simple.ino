/*
 * Simple Microphone Test for ESP32-S3 Smart Speaker
 * 
 * This script tests the microphone by recording audio and playing it back
 * in real-time. Minimal version for maximum compatibility.
 */

#include <WiFi.h>
#include <WebServer.h>
#include <AudioTools.h>
#include <AudioTools/CoreAudio/AudioI2S/I2SStream.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// =======================================================
// PIN CONFIGURATION
// =======================================================
#define I2S_TX_BCLK_PIN 19      // Output BCLK (speakers)
#define I2S_TX_LRC_PIN 21       // Output LRC (speakers)
#define I2S_TX_DATA_OUT_PIN 10  // Output data (speakers)
#define I2S_RX_BCLK_PIN 17      // Input BCLK (microphone)
#define I2S_RX_LRC_PIN 18       // Input LRC (microphone)
#define I2S_RX_DATA_IN_PIN 16   // Input data (microphone)
#define LED_PIN 2               // Built-in LED

// =======================================================
// SETTINGS
// =======================================================
const int SAMPLE_RATE = 16000;
const int BITS_PER_SAMPLE = 16;
const int CHANNELS_INPUT = 1;
const int CHANNELS_OUTPUT = 2;
const int BUFFER_SIZE = 512;
const int BUFFER_COUNT = 8;
const int MIN_AMPLITUDE = 50;
const float DEFAULT_GAIN = 2.0;

const char* AP_SSID = \"ESP32_MicTest\";
const char* AP_PASSWORD = \"12345678\";

// =======================================================
// GLOBAL VARIABLES
// =======================================================
I2SStream i2s_input;
I2SStream i2s_output;
WebServer server(80);

volatile bool isTestActive = false;
volatile float gain = DEFAULT_GAIN;
volatile int currentAmplitude = 0;
volatile int maxAmplitude = 0;
volatile bool ledState = false;

SemaphoreHandle_t amplitudeMutex;

// =======================================================
// FUNCTIONS
// =======================================================

void setupI2S() {
  Serial.println(\"Setting up I2S...\");
  
  // Microphone input
  auto config_input = i2s_input.defaultConfig(RX_MODE);
  config_input.sample_rate = SAMPLE_RATE;
  config_input.bits_per_sample = BITS_PER_SAMPLE;
  config_input.channels = CHANNELS_INPUT;
  config_input.pin_bck = I2S_RX_BCLK_PIN;
  config_input.pin_ws = I2S_RX_LRC_PIN;
  config_input.pin_data = I2S_RX_DATA_IN_PIN;
  config_input.buffer_count = BUFFER_COUNT;
  config_input.buffer_size = BUFFER_SIZE;
  
  if (!i2s_input.begin(config_input)) {
    Serial.println(\"ERROR: I2S microphone failed!\");
    return;
  }
  Serial.println(\"Microphone OK\");
  
  // Speaker output
  auto config_output = i2s_output.defaultConfig(TX_MODE);
  config_output.sample_rate = SAMPLE_RATE;
  config_output.bits_per_sample = BITS_PER_SAMPLE;
  config_output.channels = CHANNELS_OUTPUT;
  config_output.pin_bck = I2S_TX_BCLK_PIN;
  config_output.pin_ws = I2S_TX_LRC_PIN;
  config_output.pin_data = I2S_TX_DATA_OUT_PIN;
  config_output.buffer_count = BUFFER_COUNT;
  config_output.buffer_size = BUFFER_SIZE;
  
  if (!i2s_output.begin(config_output)) {
    Serial.println(\"ERROR: I2S speakers failed!\");
    return;
  }
  Serial.println(\"Speakers OK\");
}

void setupWiFi() {
  Serial.println(\"Creating WiFi AP...\");
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.println(\"WiFi: \" + String(AP_SSID));
  Serial.println(\"Password: \" + String(AP_PASSWORD));
  Serial.println(\"IP: \" + WiFi.softAPIP().toString());
}

void handleRoot() {
  String html = \"<html><head><title>Microphone Test</title></head><body>\";
  html += \"<h1>ESP32 Microphone Test</h1>\";
  html += \"<p>Status: <b id='status'>\";
  html += isTestActive ? \"ACTIVE\" : \"STOPPED\";
  html += \"</b></p>\";
  html += \"<p>Amplitude: <span id='amp'>\" + String(currentAmplitude) + \"</span></p>\";
  html += \"<p>Max Amplitude: <span id='maxamp'>\" + String(maxAmplitude) + \"</span></p>\";
  html += \"<p>Gain: <span id='gain'>\" + String(gain, 1) + \"</span>x</p>\";
  html += \"<button onclick='start()'>START TEST</button> \";\n  html += \"<button onclick='stop()'>STOP TEST</button><br><br>\";\n  html += \"Gain: <input type='range' min='0.1' max='10' step='0.1' value='\" + String(gain, 1) + \"' onchange='setGain(this.value)'><br><br>\";\n  html += \"<script>\";\n  html += \"function start() { fetch('/start', {method:'POST'}); }\";\n  html += \"function stop() { fetch('/stop', {method:'POST'}); }\";\n  html += \"function setGain(v) { fetch('/gain?v='+v); }\";\n  html += \"setInterval(function() {\";\n  html += \"  fetch('/status').then(r=>r.json()).then(d=>{\";\n  html += \"    document.getElementById('status').innerHTML = d.active ? 'ACTIVE' : 'STOPPED';\";\n  html += \"    document.getElementById('amp').innerHTML = d.amplitude;\";\n  html += \"    document.getElementById('maxamp').innerHTML = d.maxAmplitude;\";\n  html += \"    document.getElementById('gain').innerHTML = d.gain + 'x';\";\n  html += \"  });\";\n  html += \"}, 500);\";\n  html += \"</script></body></html>\";\n  \n  server.send(200, \"text/html\", html);\n}\n\nvoid handleStatus() {\n  xSemaphoreTake(amplitudeMutex, portMAX_DELAY);\n  int amp = currentAmplitude;\n  int maxAmp = maxAmplitude;\n  xSemaphoreGive(amplitudeMutex);\n  \n  String json = \"{\\\"active\\\":\";\n  json += isTestActive ? \"true\" : \"false\";\n  json += \",\\\"amplitude\\\":\" + String(amp);\n  json += \",\\\"maxAmplitude\\\":\" + String(maxAmp);\n  json += \",\\\"gain\\\":\" + String(gain, 1);\n  json += \"}\";\n  \n  server.send(200, \"application/json\", json);\n}\n\nvoid handleStart() {\n  isTestActive = true;\n  Serial.println(\"TEST STARTED\");\n  server.send(200, \"text/plain\", \"OK\");\n}\n\nvoid handleStop() {\n  isTestActive = false;\n  Serial.println(\"TEST STOPPED\");\n  server.send(200, \"text/plain\", \"OK\");\n}\n\nvoid handleGain() {\n  if (server.hasArg(\"v\")) {\n    float newGain = server.arg(\"v\").toFloat();\n    if (newGain >= 0.1 && newGain <= 10.0) {\n      gain = newGain;\n      Serial.println(\"Gain: \" + String(gain, 1) + \"x\");\n    }\n  }\n  server.send(200, \"text/plain\", \"OK\");\n}\n\nvoid setupWebServer() {\n  Serial.println(\"Starting web server...\");\n  \n  server.on(\"/\", handleRoot);\n  server.on(\"/status\", handleStatus);\n  server.on(\"/start\", HTTP_POST, handleStart);\n  server.on(\"/stop\", HTTP_POST, handleStop);\n  server.on(\"/gain\", handleGain);\n  \n  server.begin();\n  Serial.println(\"Web server ready\");\n}\n\n// Microphone task\nvoid microphoneTask(void* parameter) {\n  int16_t inputBuffer[BUFFER_SIZE];\n  int16_t outputBuffer[BUFFER_SIZE * 2];\n  \n  Serial.println(\"Microphone task started\");\n  \n  while (true) {\n    if (isTestActive) {\n      size_t bytesRead = i2s_input.readBytes((uint8_t*)inputBuffer, sizeof(inputBuffer));\n      \n      if (bytesRead > 0) {\n        size_t samplesRead = bytesRead / sizeof(int16_t);\n        \n        // Find max amplitude\n        int amplitude = 0;\n        for (size_t i = 0; i < samplesRead; i++) {\n          int sample = abs(inputBuffer[i]);\n          if (sample > amplitude) amplitude = sample;\n        }\n        \n        // Update statistics\n        xSemaphoreTake(amplitudeMutex, portMAX_DELAY);\n        currentAmplitude = amplitude;\n        if (amplitude > maxAmplitude) maxAmplitude = amplitude;\n        xSemaphoreGive(amplitudeMutex);\n        \n        // Apply gain and convert to stereo\n        for (size_t i = 0; i < samplesRead; i++) {\n          int32_t amplifiedSample = (int32_t)(inputBuffer[i] * gain);\n          if (amplifiedSample > 32767) amplifiedSample = 32767;\n          if (amplifiedSample < -32767) amplifiedSample = -32767;\n          \n          outputBuffer[i * 2] = (int16_t)amplifiedSample;     // Left\n          outputBuffer[i * 2 + 1] = (int16_t)amplifiedSample; // Right\n        }\n        \n        // Play through speakers\n        i2s_output.write((uint8_t*)outputBuffer, samplesRead * 2 * sizeof(int16_t));\n        \n        // LED blink\n        if (amplitude > MIN_AMPLITUDE) {\n          ledState = !ledState;\n          digitalWrite(LED_PIN, ledState ? LOW : HIGH);\n        }\n      }\n    } else {\n      digitalWrite(LED_PIN, HIGH);\n      vTaskDelay(pdMS_TO_TICKS(50));\n    }\n    \n    vTaskDelay(pdMS_TO_TICKS(1));\n  }\n}\n\n// =======================================================\n// MAIN\n// =======================================================\n\nvoid setup() {\n  Serial.begin(115200);\n  delay(1000);\n  \n  Serial.println(\"ESP32-S3 MICROPHONE TEST - SIMPLE VERSION\");\n  Serial.println(\"===========================================\");\n  Serial.println(\"Version: 1.0 (Simple)\");\n  \n  pinMode(LED_PIN, OUTPUT);\n  digitalWrite(LED_PIN, HIGH);\n  \n  amplitudeMutex = xSemaphoreCreateMutex();\n  if (amplitudeMutex == NULL) {\n    Serial.println(\"ERROR: Mutex creation failed!\");\n    while(1);\n  }\n  \n  setupI2S();\n  setupWiFi();\n  setupWebServer();\n  \n  xTaskCreatePinnedToCore(\n    microphoneTask,\n    \"MicrophoneTask\",\n    8192,\n    NULL,\n    2,\n    NULL,\n    1\n  );\n  \n  Serial.println(\"READY!\");\n  Serial.println(\"Open: http://\" + WiFi.softAPIP().toString());\n  Serial.println(\"Click START TEST to begin\");\n}\n\nvoid loop() {\n  server.handleClient();\n  \n  // Reset max amplitude every 10 seconds\n  static unsigned long lastReset = 0;\n  if (millis() - lastReset > 10000) {\n    xSemaphoreTake(amplitudeMutex, portMAX_DELAY);\n    maxAmplitude = 0;\n    xSemaphoreGive(amplitudeMutex);\n    lastReset = millis();\n  }\n  \n  delay(10);\n}