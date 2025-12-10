#include "app_support.h"
#include <esp_log.h>
#include <driver/ledc.h>
#include <esp_timer.h>
#include <esp_rom_sys.h>
#include <esp_https_ota.h>
#include <esp_ota_ops.h>
#include <esp_insights.h>
#include <esp_diagnostics.h>
#include <esp_rmaker_mqtt.h>
#include <esp_rmaker_common_events.h>
#include <string.h>

// --- UTILITIES & DRIVERS ---

void buzzer_init(void) {
    ledc_timer_config_t t = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0, .freq_hz = 2000, .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&t);
    ledc_channel_config_t ch = {
        .gpio_num = BUZZER_GPIO, .speed_mode = LEDC_LOW_SPEED_MODE, .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE, .timer_sel = LEDC_TIMER_0, .duty = 0, .hpoint = 0,
    };
    ledc_channel_config(&ch);
}

void buzzer_set_freq(int freq_hz) {
    ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, freq_hz);
}

void buzzer_tone(bool on) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, on ? 512 : 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

// --- UNIQUE BUZZER SOUNDS ---

void buzzer_fan_sound(void) {
    for(int f=500; f<=1500; f+=200) {
        buzzer_set_freq(f);
        buzzer_tone(true);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    buzzer_tone(false);
    buzzer_set_freq(2000);
}

void buzzer_light_sound(void) {
    buzzer_set_freq(2500);
    buzzer_tone(true);
    vTaskDelay(pdMS_TO_TICKS(100));
    buzzer_tone(false);
    buzzer_set_freq(2000);
}

void buzzer_tv_sound(void) {
    int notes[] = {261, 329, 392}; // C4, E4, G4
    for(int i=0; i<3; i++) {
        buzzer_set_freq(notes[i] * 2); // Octave up
        buzzer_tone(true);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    buzzer_tone(false);
    buzzer_set_freq(2000);
}

void buzzer_plug_sound(void) {
    buzzer_set_freq(400);
    buzzer_tone(true);
    vTaskDelay(pdMS_TO_TICKS(80));
    buzzer_set_freq(800);
    vTaskDelay(pdMS_TO_TICKS(80));
    buzzer_tone(false);
    buzzer_set_freq(2000);
}

void buzzer_doorbell(void) {
    buzzer_set_freq(659);
    buzzer_tone(true);
    vTaskDelay(pdMS_TO_TICKS(400));
    buzzer_set_freq(523);
    vTaskDelay(pdMS_TO_TICKS(600));
    buzzer_tone(false);
    buzzer_set_freq(2000);
}

void buzzer_fan_speed_sound(int speed) {
    if (speed <= 0) {
        buzzer_set_freq(1000);
        buzzer_tone(true);
        vTaskDelay(pdMS_TO_TICKS(200));
        buzzer_tone(false);
        buzzer_set_freq(2000);
        return;
    }
    buzzer_set_freq(3000); 
    for (int i = 0; i < speed; i++) {
        buzzer_tone(true);
        vTaskDelay(pdMS_TO_TICKS(80));
        buzzer_tone(false);
        vTaskDelay(pdMS_TO_TICKS(80));
    }
    buzzer_set_freq(2000);
}

void buzzer_error_sound(void) {
    buzzer_set_freq(200);
    for(int i=0; i<3; i++) {
        buzzer_tone(true);
        vTaskDelay(pdMS_TO_TICKS(150));
        buzzer_tone(false);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    buzzer_set_freq(2000);
}

// Blink Green LED 3 times
extern volatile bool blinking_active; // Defined in app_main.c
void indicate_device_on(void) {
    blinking_active = true;
    gpio_set_level(LED_RED_GPIO, 0);
    for(int i=0; i<3; i++) {
        gpio_set_level(LED_GREEN_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(150));
        gpio_set_level(LED_GREEN_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(150));
    }
    blinking_active = false;
}

void beep(int ms) {
    buzzer_tone(true);
    vTaskDelay(pdMS_TO_TICKS(ms));
    buzzer_tone(false);
}

// --- ULTRASONIC SENSOR ---
float get_distance_cm(void) {
    gpio_set_level(TRIG_GPIO, 0);
    esp_rom_delay_us(2);
    gpio_set_level(TRIG_GPIO, 1);
    esp_rom_delay_us(10);
    gpio_set_level(TRIG_GPIO, 0);

    int64_t start = esp_timer_get_time();
    int64_t timeout = start + 25000;
    
    while (gpio_get_level(ECHO_GPIO) == 0) {
        if (esp_timer_get_time() > timeout) return -1.0;
    }
    
    int64_t echo_start = esp_timer_get_time();
    while (gpio_get_level(ECHO_GPIO) == 1) {
        if (esp_timer_get_time() > echo_start + 25000) break;
    }
    int64_t echo_end = esp_timer_get_time();

    float duration = (float)(echo_end - echo_start);
    float distance = (duration * 0.0343) / 2.0;
    return distance;
}

// --- NOTIFICATION TASK ---
typedef struct {
    char message[96];
} notification_msg_t;

void notification_task(void *arg) {
    notification_msg_t n;
    while (1) {
        if (xQueueReceive(notification_queue, &n, portMAX_DELAY)) {
            send_alert(n.message);
        }
    }
}


// --- INSIGHTS ---
#define INSIGHTS_TOPIC_SUFFIX       "diagnostics/from-node"
#define INSIGHTS_TOPIC_RULE         "insights_message_delivery"

static int app_insights_data_send(void *data, size_t len)
{
    char topic[128];
    int msg_id = -1;
    if (data == NULL) {
        return 0;
    }
    char *node_id = esp_rmaker_get_node_id();
    if (!node_id) {
        return -1;
    }
    if (esp_rmaker_mqtt_is_budget_available() == false) {
        return ESP_FAIL;
    }
    esp_rmaker_create_mqtt_topic(topic, sizeof(topic), INSIGHTS_TOPIC_SUFFIX, INSIGHTS_TOPIC_RULE);
    esp_rmaker_mqtt_publish(topic, data, len, RMAKER_MQTT_QOS1, &msg_id);
    return msg_id;
}

static void rmaker_common_event_handler(void* arg, esp_event_base_t event_base,
                                        int32_t event_id, void* event_data)
{
    if (event_base != RMAKER_COMMON_EVENT) {
        return;
    }
    esp_insights_transport_event_data_t data;
    switch(event_id) {
        case RMAKER_MQTT_EVENT_PUBLISHED:
            memset(&data, 0, sizeof(data));
            data.msg_id = *(int *)event_data;
            esp_event_post(INSIGHTS_EVENT, INSIGHTS_EVENT_TRANSPORT_SEND_SUCCESS, &data, sizeof(data), portMAX_DELAY);
            break;
        default:
            break;
    }
}

esp_err_t app_insights_enable(void)
{
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    char *node_id = esp_rmaker_get_node_id();

    esp_insights_transport_config_t transport = {
        .callbacks.data_send  = app_insights_data_send,
    };
    esp_insights_transport_register(&transport);

    esp_event_handler_register(RMAKER_COMMON_EVENT, ESP_EVENT_ANY_ID, rmaker_common_event_handler, NULL);

    esp_insights_config_t config = {
        .log_type = ESP_DIAG_LOG_TYPE_ERROR | ESP_DIAG_LOG_TYPE_WARNING | ESP_DIAG_LOG_TYPE_EVENT,
        .node_id  = node_id,
        .alloc_ext_ram = true,
    };

    esp_insights_enable(&config);

    return ESP_OK;
}
