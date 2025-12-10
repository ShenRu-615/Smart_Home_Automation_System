#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_rmaker_core.h>
#include <esp_rmaker_standard_params.h>
#include <driver/gpio.h>

// --- PIN DEFINITIONS ---
#ifndef DHT_GPIO
#define DHT_GPIO GPIO_NUM_10
#endif

#define BUZZER_GPIO    GPIO_NUM_2
#define LED_RED_GPIO   GPIO_NUM_4
#define LED_GREEN_GPIO GPIO_NUM_5
#define TRIG_GPIO      GPIO_NUM_3
#define ECHO_GPIO      GPIO_NUM_1

// --- CONFIGURATION ---
#define DOOR_THRESHOLD_CM 15.0
#define DEFAULT_PASSWORD "2580"

// --- LOGGING TAG ---
#define TAG "SMART_HOME_HUB"

// --- EVENT TAGS ---
#define EVT_SEC   "SECURITY"
#define EVT_DOOR  "DOOR"
#define EVT_DEV   "DEVICE"
#define EVT_SYS   "SYSTEM"

// --- SHARED GLOBALS ---
extern esp_rmaker_param_t *param_ota_url;
extern QueueHandle_t notification_queue;

// --- FUNCTION PROTOTYPES ---

// From app_main.c (Called by support)
void send_alert(const char *msg);

// From app_support.c (Called by main)
void buzzer_init(void);
void buzzer_set_freq(int freq_hz);
void buzzer_tone(bool on);
void buzzer_fan_sound(void);
void buzzer_light_sound(void);
void buzzer_tv_sound(void);
void buzzer_plug_sound(void);
void buzzer_doorbell(void);
void buzzer_fan_speed_sound(int speed);
void buzzer_error_sound(void);

void indicate_device_on(void);
void beep(int ms);
float get_distance_cm(void);

void notification_task(void *arg);
esp_err_t app_insights_enable(void);

