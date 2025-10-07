/*
 * Microphone Test Script for ESP32-S3 Smart Speaker (ASCII Version)
 * 
 * This script tests the microphone by recording audio and playing it back
 * in real-time so you can hear what the microphone is picking up.
 * 
 * Features:
 * - Real-time audio recording from I2S microphone
 * - Playback of recorded audio through I2S speakers
 * - Adjustable microphone gain  
 * - Real-time signal level monitoring
 * - Web interface for control
 */

#include <WiFi.h>
#include <WebServer.h>
#include <AudioTools.h>
#include <AudioTools/CoreAudio/AudioI2S/I2SStream.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>

// =======================================================
// ESP32-S3 PIN CONFIGURATION
// =======================================================
#define I2S_TX_BCLK_PIN 19      // Output BCLK (speakers)
#define I2S_TX_LRC_PIN 21       // Output LRC (speakers)
#define I2S_TX_DATA_OUT_PIN 10  // Output data (speakers)
#define I2S_RX_BCLK_PIN 17      // Input BCLK (microphone)
#define I2S_RX_LRC_PIN 18       // Input LRC (microphone)
#define I2S_RX_DATA_IN_PIN 16   // Input data (microphone)
#define LED_PIN 2               // Built-in LED for indication

// =======================================================
// AUDIO SETTINGS
// =======================================================
const int SAMPLE_RATE = 16000;        // Sample rate
const int BITS_PER_SAMPLE = 16;       // Bit depth
const int CHANNELS_INPUT = 1;         // Mono input (microphone)
const int CHANNELS_OUTPUT = 2;        // Stereo output (speakers)
const int BUFFER_SIZE = 512;          // Buffer size
const int BUFFER_COUNT = 8;           // Number of buffers

// =======================================================
// MICROPHONE SETTINGS
// =======================================================
const int MIN_AMPLITUDE = 50;         // Minimum amplitude for sound detection
const int SILENCE_THRESHOLD = 30;     // Silence threshold
const float DEFAULT_GAIN = 2.0;       // Default gain
const int MONITORING_INTERVAL = 100;  // Monitoring interval in ms

// =======================================================
// WI-FI SETTINGS
// =======================================================
const char* AP_SSID = "ESP32_MicTest";
const char* AP_PASSWORD = "12345678";

// =======================================================
// GLOBAL VARIABLES
// =======================================================
I2SStream i2s_input;                 // Input stream (microphone)
I2SStream i2s_output;                // Output stream (speakers)
WebServer server(80);                // Web server

// Control variables
volatile bool isTestActive = false;   // Is test active
volatile bool isMonitoring = true;    // Signal level monitoring
volatile float gain = DEFAULT_GAIN;   // Current gain
volatile int currentAmplitude = 0;    // Current signal amplitude
volatile int maxAmplitude = 0;        // Maximum amplitude
volatile bool ledState = false;       // LED state

// Synchronization
SemaphoreHandle_t amplitudeMutex;
QueueHandle_t audioQueue;

// Audio data buffer
struct AudioBuffer {
  int16_t data[BUFFER_SIZE];
  size_t size;
};

// =======================================================
// INITIALIZATION FUNCTIONS
// =======================================================

void setupI2S() {
  Serial.println("Setting up I2S microphone...");
  
  // Input I2S configuration (microphone)
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
    Serial.println("ERROR: I2S microphone initialization failed!");
    return;
  }
  Serial.println("I2S microphone initialized successfully");
  
  Serial.println("Setting up I2S speakers...");
  
  // Output I2S configuration (speakers)
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
    Serial.println("ERROR: I2S speakers initialization failed!");
    return;
  }
  Serial.println("I2S speakers initialized successfully");
}

void setupWiFi() {
  Serial.println("Creating WiFi access point...");
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.println("Access point created:");
  Serial.println("   SSID: " + String(AP_SSID));
  Serial.println("   Password: " + String(AP_PASSWORD));
  Serial.println("   IP: " + WiFi.softAPIP().toString());
}

// =======================================================
// WEB ROUTES
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
        <h1>Microphone Test ESP32-S3</h1>
        
        <div class="status-panel">
            <h3>Status: <span id="status" class="status-inactive">Stopped</span></h3>
            <div>Current amplitude: <span id="amplitude" class="amplitude">0</span></div>
            <div>Maximum amplitude: <span id="maxAmplitude">0</span></div>
            <div class="meter">
                <div id="meter-fill" class="meter-fill" style="width: 0%"></div>
            </div>
        </div>
        
        <div class="control-panel">
            <h3>Controls</h3>
            <button id="startBtn" onclick="startTest()">[START] Start Test</button>
            <button id="stopBtn" onclick="stopTest()" class="stop">[STOP] Stop Test</button>
            <br>
            <div class="slider-container">
                <label for="gainSlider">Gain: <span id="gainValue">2.0</span>x</label>
                <input type="range" id="gainSlider" class="slider" min="0.1" max="10" step="0.1" value="2.0" onchange="setGain(this.value)">
            </div>
        </div>
        
        <div class="info">
            <h4>Instructions:</h4>
            <p>- Click <strong>"Start Test"</strong> to record audio from microphone and play through speakers</p>
            <p>- Speak into microphone - you should hear your voice through speakers</p>
            <p>- Adjust gain if sound is too quiet or loud</p>
            <p>- Monitor signal level on the indicator</p>
            <p>- Click <strong>"Stop Test"</strong> to finish</p>
        </div>
        
        <div class="info">
            <h4>Technical Specifications:</h4>
            <p>- Sample rate: 16000 Hz</p>
            <p>- Bit depth: 16 bit</p>
            <p>- Input: Mono (microphone)</p>
            <p>- Output: Stereo (speakers)</p>
        </div>
    </div>

    <script>
        function updateStatus() {
            fetch('/status')
                .then(response => response.json())
                .then(data => {
                    document.getElementById('status').textContent = data.active ? 'Active' : 'Stopped';
                    document.getElementById('status').className = data.active ? 'status-active' : 'status-inactive';
                    document.getElementById('amplitude').textContent = data.amplitude;
                    document.getElementById('maxAmplitude').textContent = data.maxAmplitude;
                    
                    // Update level indicator
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
        
        // Update status every 200ms
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
  Serial.println("Microphone test STARTED");
  
  server.send(200, "application/json", "{\"status\":\"started\"}");
}

void handleStop() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method not allowed");
    return;
  }
  
  isTestActive = false;
  Serial.println("Microphone test STOPPED");
  
  server.send(200, "application/json", "{\"status\":\"stopped\"}");
}

void handleGain() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method not allowed");
    return;
  }
  
  // Parse JSON request
  String body = server.arg("plain");
  if (body.length() > 0) {
    int startIndex = body.indexOf("\"gain\":") + 7;
    int endIndex = body.indexOf("}", startIndex);
    if (startIndex > 6 && endIndex > startIndex) {
      String gainStr = body.substring(startIndex, endIndex);
      float newGain = gainStr.toFloat();
      if (newGain >= 0.1 && newGain <= 10.0) {
        gain = newGain;
        Serial.println("Gain set to: " + String(gain, 1) + "x");
      }
    }
  }
  
  server.send(200, "application/json", "{\"status\":\"gain_set\", \"gain\":" + String(gain, 1) + "}");
}

void setupWebServer() {
  Serial.println("Setting up web server...");
  
  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/start", HTTP_POST, handleStart);
  server.on("/stop", HTTP_POST, handleStop);
  server.on("/gain", HTTP_POST, handleGain);
  
  server.begin();
  Serial.println("Web server started on http://" + WiFi.softAPIP().toString());
}

// =======================================================
// REAL-TIME TASKS
// =======================================================

// Microphone monitoring task
void microphoneTask(void* parameter) {
  int16_t inputBuffer[BUFFER_SIZE];
  int16_t outputBuffer[BUFFER_SIZE * 2]; // Stereo buffer
  
  Serial.println("Microphone task started");
  
  while (true) {
    if (isTestActive) {
      // Read data from microphone
      size_t bytesRead = i2s_input.readBytes((uint8_t*)inputBuffer, sizeof(inputBuffer));
      
      if (bytesRead > 0) {
        size_t samplesRead = bytesRead / sizeof(int16_t);
        
        // Analyze amplitude
        int amplitude = 0;
        for (size_t i = 0; i < samplesRead; i++) {
          amplitude = max(amplitude, abs(inputBuffer[i]));
        }
        
        // Update amplitude statistics
        xSemaphoreTake(amplitudeMutex, portMAX_DELAY);
        currentAmplitude = amplitude;
        maxAmplitude = max((int)maxAmplitude, amplitude);
        xSemaphoreGive(amplitudeMutex);
        
        // Apply gain and convert to stereo
        for (size_t i = 0; i < samplesRead; i++) {
          int32_t amplifiedSample = (int32_t)(inputBuffer[i] * gain);
          // Limit value to avoid clipping
          amplifiedSample = constrain(amplifiedSample, -32767, 32767);
          
          // Duplicate to both channels for stereo
          outputBuffer[i * 2] = (int16_t)amplifiedSample;     // Left channel
          outputBuffer[i * 2 + 1] = (int16_t)amplifiedSample; // Right channel
        }
        
        // Output sound through speakers
        i2s_output.write((uint8_t*)outputBuffer, samplesRead * 2 * sizeof(int16_t));
        
        // Blink LED when active
        if (amplitude > MIN_AMPLITUDE) {
          ledState = !ledState;
          digitalWrite(LED_PIN, ledState ? LOW : HIGH);
        }
      }
    } else {
      // Pause when test is inactive
      digitalWrite(LED_PIN, HIGH); // LED off
      vTaskDelay(pdMS_TO_TICKS(50));
    }
    
    // Small delay for other tasks
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

// Monitoring task (statistics update)
void monitoringTask(void* parameter) {
  Serial.println("Monitoring task started");
  
  while (true) {
    if (isMonitoring && isTestActive) {
      xSemaphoreTake(amplitudeMutex, portMAX_DELAY);
      int amp = currentAmplitude;
      int maxAmp = maxAmplitude;
      xSemaphoreGive(amplitudeMutex);
      
      // Print information to Serial Monitor every second
      static unsigned long lastPrint = 0;
      if (millis() - lastPrint > 1000) {
        Serial.printf("Amplitude: %d, Max: %d, Gain: %.1fx\n", amp, maxAmp, gain);
        lastPrint = millis();
      }
    }
    
    vTaskDelay(pdMS_TO_TICKS(MONITORING_INTERVAL));
  }
}

// =======================================================
// MAIN FUNCTIONS
// =======================================================

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n" + String('=', 50));
  Serial.println("ESP32-S3 MICROPHONE TEST");
  Serial.println(String('=', 50));
  Serial.println("Version: 1.0 (ASCII)");
  Serial.println("Free memory: " + String(ESP.getFreeHeap()) + " bytes");
  Serial.println();
  
  // Initialize LED
  Serial.print("Initializing LED... ");
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH); // LED off (inverted logic)
  Serial.println("OK");
  
  // Create mutex
  Serial.print("Creating mutex... ");
  amplitudeMutex = xSemaphoreCreateMutex();
  if (amplitudeMutex == NULL) {
    Serial.println("ERROR!");
    while(1);
  }
  Serial.println("OK");
  
  // Create queue
  Serial.print("Creating queue... ");
  audioQueue = xQueueCreate(10, sizeof(AudioBuffer));
  if (audioQueue == NULL) {
    Serial.println("ERROR!");
    while(1);
  }
  Serial.println("OK");
  
  // Setup I2S
  setupI2S();
  
  // Setup WiFi
  setupWiFi();
  
  // Setup web server
  setupWebServer();
  
  // Start tasks
  Serial.println("Starting tasks...");
  
  xTaskCreatePinnedToCore(
    microphoneTask,    // Task function
    "MicrophoneTask",  // Task name
    8192,              // Stack size
    NULL,              // Parameter
    2,                 // Priority
    NULL,              // Task handle
    1                  // Core (1)
  );
  
  xTaskCreatePinnedToCore(
    monitoringTask,    // Task function
    "MonitoringTask",  // Task name
    2048,              // Stack size
    NULL,              // Parameter
    1,                 // Priority
    NULL,              // Task handle
    0                  // Core (0)
  );
  
  Serial.println();
  Serial.println(String('=', 60));
  Serial.println("MICROPHONE TEST READY!");
  Serial.println(String('=', 60));
  Serial.println("Open in browser: http://" + WiFi.softAPIP().toString());
  Serial.println("Click 'Start Test' to hear microphone");
  Serial.println("Speak into microphone - you will hear your voice");
  Serial.println();
  Serial.println("Tips:");
  Serial.println("   - If sound is too quiet - increase gain");
  Serial.println("   - If there are distortions - decrease gain");
  Serial.println("   - Monitor signal level on web page");
  Serial.println();
}

void loop() {
  // Handle web server
  server.handleClient();
  
  // Reset maximum amplitude every 10 seconds
  static unsigned long lastReset = 0;
  if (millis() - lastReset > 10000) {
    xSemaphoreTake(amplitudeMutex, portMAX_DELAY);
    maxAmplitude = 0;
    xSemaphoreGive(amplitudeMutex);
    lastReset = millis();
  }
  
  delay(10);
}