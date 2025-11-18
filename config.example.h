// Пример файла конфигурации для голосового помощника
// Скопируйте этот файл в config.h и заполните своими API ключами

#ifndef CONFIG_H
#define CONFIG_H

// Yandex SpeechKit API
#define YANDEX_API_KEY "ваш_yandex_api_key_здесь"
#define YANDEX_FOLDER_ID "ваш_yandex_folder_id_здесь"

// OpenWeatherMap API
#define OPENWEATHER_API_KEY "ваш_openweather_api_key_здесь"

// Настройки WiFi по умолчанию (опционально)
#define DEFAULT_SSID "ваша_wifi_сеть"
#define DEFAULT_PASSWORD "ваш_wifi_пароль"

// Настройки устройства
#define DEVICE_NAME "SmartSpeaker"
#define DEFAULT_CITY "Moscow"  // город для прогноза погоды

#endif // CONFIG_H