#include "app_support.h"
#include <esp_log.h>
#include <nvs_flash.h>
#include <esp_rmaker_core.h>
#include <esp_rmaker_standard_types.h>
#include <esp_rmaker_standard_params.h>
#include <esp_rmaker_standard_devices.h>
#include <app_network.h>
#include "dht11.h"
#include <esp_rmaker_ota.h>
#include <esp_rmaker_schedule.h>
#include <esp_rmaker_scenes.h>
#include <esp_diagnostics.h>
#include <string.h>
#include <esp_timer.h>

// --- PIN DEFINITIONS (Keypad) ---
#define KEYPAD_ROWS 4
#define KEYPAD_COLS 4
static const gpio_num_t KEYPAD_ROW_GPIOS[KEYPAD_ROWS] = { GPIO_NUM_21, GPIO_NUM_20, GPIO_NUM_19, GPIO_NUM_18 };
static const gpio_num_t KEYPAD_COL_GPIOS[KEYPAD_COLS] = { GPIO_NUM_9, GPIO_NUM_8, GPIO_NUM_7, GPIO_NUM_6 };

// --- GLOBALS ---
QueueHandle_t notification_queue;
static SemaphoreHandle_t sys_mutex;

// RainMaker Parameters
static esp_rmaker_param_t *param_temp;
static esp_rmaker_param_t *param_humidity;
static esp_rmaker_param_t *param_alert;
static esp_rmaker_param_t *param_door_status;
static esp_rmaker_param_t *param_sec_status;
static esp_rmaker_param_t *param_set_pw;
esp_rmaker_param_t *param_ota_url; // Non-static for access in app_support.c
static esp_rmaker_param_t *param_fw_version;

static esp_rmaker_param_t *param_home_fan;
static esp_rmaker_param_t *param_home_light;
static esp_rmaker_param_t *param_home_tv;
static esp_rmaker_param_t *param_home_plug;
static esp_rmaker_param_t *param_home_door;
static esp_rmaker_param_t *param_home_sec;

static esp_rmaker_param_t *param_fan_power;
static esp_rmaker_param_t *param_fan_speed;
static esp_rmaker_param_t *param_fan_status;
static esp_rmaker_param_t *param_light_power;
static esp_rmaker_param_t *param_light_status;
static esp_rmaker_param_t *param_tv_power;
static esp_rmaker_param_t *param_tv_status;
static esp_rmaker_param_t *param_plug_power;
static esp_rmaker_param_t *param_plug_status;

// State
static bool system_armed = true;
static bool door_is_open = false;
static char password_buffer[5] = {0};
static int  password_index = 0;
static char master_password[16] = DEFAULT_PASSWORD;

static bool fan_state = false;
static int  fan_speed = 0;
static bool light_state = false;
static bool tv_state = false;
static bool plug_state = false;

volatile bool blinking_active = false; // Exposed for app_support.c

// --- FUNCTIONS ---

void send_alert(const char *msg) {
    esp_rmaker_param_update_and_report(param_alert, esp_rmaker_str(msg));
    esp_rmaker_raise_alert(msg);
    ESP_LOGW(TAG, "ALERT: %s", msg);
}

static void keypad_init(void) {
    for (int r = 0; r < KEYPAD_ROWS; r++) {
        gpio_reset_pin(KEYPAD_ROW_GPIOS[r]);
        gpio_set_direction(KEYPAD_ROW_GPIOS[r], GPIO_MODE_OUTPUT);
        gpio_set_level(KEYPAD_ROW_GPIOS[r], 1);
    }
    for (int c = 0; c < KEYPAD_COLS; c++) {
        gpio_reset_pin(KEYPAD_COL_GPIOS[c]);
        gpio_set_direction(KEYPAD_COL_GPIOS[c], GPIO_MODE_INPUT);
        gpio_pullup_en(KEYPAD_COL_GPIOS[c]);
    }
}

static void keypad_task(void *arg) {
    keypad_init();
    const char keymap[KEYPAD_ROWS][KEYPAD_COLS] = {
        {'1','2','3','A'}, {'4','5','6','B'}, {'7','8','9','C'}, {'*','0','#','D'}
    };
    
    ESP_LOGI(TAG, "Keypad Ready. Enter %s# to Toggle Arm/Disarm", master_password);
    ESP_DIAG_EVENT(EVT_SYS, "Keypad Task Started");

    while (1) {
        for (int r = 0; r < KEYPAD_ROWS; r++) {
            gpio_set_level(KEYPAD_ROW_GPIOS[r], 0);
            esp_rom_delay_us(50);
            
            for (int c = 0; c < KEYPAD_COLS; c++) {
                if (gpio_get_level(KEYPAD_COL_GPIOS[c]) == 0) {
                    char key = keymap[r][c];
                    ESP_LOGI(TAG, "Key Pressed: %c", key);
                    beep(50);

                    xSemaphoreTake(sys_mutex, portMAX_DELAY);
                    
                    if (key == 'A') {
                        fan_speed++;
                        if (fan_speed > 5) fan_speed = 0;
                        fan_state = (fan_speed > 0);
                        indicate_device_on();
                        buzzer_fan_speed_sound(fan_speed);
                        
                        esp_rmaker_param_update_and_report(param_fan_power, esp_rmaker_bool(fan_state));
                        esp_rmaker_param_update_and_report(param_fan_speed, esp_rmaker_int(fan_speed));
                        
                        char buf[32];
                        if (fan_speed == 0) snprintf(buf, sizeof(buf), "Fan Off");
                        else snprintf(buf, sizeof(buf), "Fan Speed %d", fan_speed);
                        
                        esp_rmaker_param_update_and_report(param_fan_status, esp_rmaker_str(buf));
                        esp_rmaker_param_update_and_report(param_home_fan, esp_rmaker_str(buf));
                        
                        ESP_LOGI(TAG, "Fan Speed: %d", fan_speed);
                        ESP_DIAG_EVENT(EVT_DEV, "Fan Manual Control: %d", fan_speed);
                        send_alert(fan_state ? "Fan Turned ON (Keypad)" : "Fan Turned OFF (Keypad)");
                    }
                    else if (key == 'B') {
                        light_state = !light_state;
                        if (light_state) buzzer_light_sound();
                        indicate_device_on();
                        esp_rmaker_param_update_and_report(param_light_power, esp_rmaker_bool(light_state));
                        esp_rmaker_param_update_and_report(param_light_status, esp_rmaker_str(light_state ? "Light On" : "Light Off"));
                        esp_rmaker_param_update_and_report(param_home_light, esp_rmaker_str(light_state ? "On" : "Off"));
                        ESP_LOGI(TAG, "Light Toggled: %d", light_state);
                        ESP_DIAG_EVENT(EVT_DEV, "Light %s (Keypad)", light_state ? "ON" : "OFF");
                        send_alert(light_state ? "Light Turned ON (Keypad)" : "Light Turned OFF (Keypad)");
                    }
                    else if (key == 'C') {
                        tv_state = !tv_state;
                        if (tv_state) buzzer_tv_sound();
                        indicate_device_on();
                        esp_rmaker_param_update_and_report(param_tv_power, esp_rmaker_bool(tv_state));
                        esp_rmaker_param_update_and_report(param_tv_status, esp_rmaker_str(tv_state ? "TV On" : "TV Off"));
                        esp_rmaker_param_update_and_report(param_home_tv, esp_rmaker_str(tv_state ? "On" : "Off"));
                        ESP_LOGI(TAG, "TV Toggled: %d", tv_state);
                        ESP_DIAG_EVENT(EVT_DEV, "TV %s (Keypad)", tv_state ? "ON" : "OFF");
                        send_alert(tv_state ? "TV Turned ON (Keypad)" : "TV Turned OFF (Keypad)");
                    }
                    else if (key == 'D') {
                        plug_state = !plug_state;
                        if (plug_state) buzzer_plug_sound();
                        indicate_device_on();
                        esp_rmaker_param_update_and_report(param_plug_power, esp_rmaker_bool(plug_state));
                        esp_rmaker_param_update_and_report(param_plug_status, esp_rmaker_str(plug_state ? "Plug On" : "Plug Off"));
                        esp_rmaker_param_update_and_report(param_home_plug, esp_rmaker_str(plug_state ? "On" : "Off"));
                        ESP_LOGI(TAG, "Plug Toggled: %d", plug_state);
                        ESP_DIAG_EVENT(EVT_DEV, "Plug %s (Keypad)", plug_state ? "ON" : "OFF");
                        send_alert(plug_state ? "Plug Turned ON (Keypad)" : "Plug Turned OFF (Keypad)");
                    }
                    else if (key == '*') {
                        password_index = 0;
                        memset(password_buffer, 0, sizeof(password_buffer));
                        esp_rmaker_param_update_and_report(param_sec_status, esp_rmaker_str("Cleared"));
                        ESP_LOGI(TAG, "Buffer Cleared");
                    }
                    else if (key == '#') {
                        if (strcmp(password_buffer, master_password) == 0) {
                            system_armed = !system_armed;
                            buzzer_tone(false);
                            
                            if(system_armed) {
                                beep(100); vTaskDelay(100); beep(100);
                                send_alert("Door Locked via Keypad");
                                esp_rmaker_param_update_and_report(param_sec_status, esp_rmaker_str("Door Locked"));
                                esp_rmaker_param_update_and_report(param_home_sec, esp_rmaker_str("Locked"));
                                ESP_LOGI(TAG, "System Locked");
                                ESP_DIAG_EVENT(EVT_SEC, "Door Locked");
                            } else {
                                beep(500);
                                send_alert("Door Unlocked via Keypad");
                                esp_rmaker_param_update_and_report(param_sec_status, esp_rmaker_str("Door Unlocked"));
                                esp_rmaker_param_update_and_report(param_home_sec, esp_rmaker_str("Unlocked"));
                                ESP_LOGI(TAG, "System Unlocked");
                                ESP_DIAG_EVENT(EVT_SEC, "Door Unlocked");
                            }
                        } else {
                            ESP_LOGW(TAG, "Wrong Password Attempt");
                            esp_rmaker_param_update_and_report(param_sec_status, esp_rmaker_str("Wrong Password"));
                            buzzer_error_sound();
                            send_alert("Invalid Password Entered");
                            ESP_DIAG_EVENT(EVT_SEC, "Invalid Password");
                        }
                        password_index = 0;
                        memset(password_buffer, 0, sizeof(password_buffer));
                    }
                    else {
                        if (key >= '0' && key <= '9') {
                            if (password_index < 4) {
                                password_buffer[password_index++] = key;
                                password_buffer[password_index] = '\0';
                                esp_rmaker_param_update_and_report(param_sec_status, esp_rmaker_str("Entering Password..."));
                            } else {
                                ESP_LOGW(TAG, "Password buffer full");
                            }
                        }
                    }
                    
                    xSemaphoreGive(sys_mutex);
                    while(gpio_get_level(KEYPAD_COL_GPIOS[c]) == 0) vTaskDelay(10); 
                }
            }
            gpio_set_level(KEYPAD_ROW_GPIOS[r], 1);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void sensor_task(void *arg) {
    buzzer_init();
    ESP_DIAG_EVENT(EVT_SYS, "Sensor Task Started");
    
    gpio_reset_pin(LED_RED_GPIO); gpio_set_direction(LED_RED_GPIO, GPIO_MODE_OUTPUT);
    gpio_reset_pin(LED_GREEN_GPIO); gpio_set_direction(LED_GREEN_GPIO, GPIO_MODE_OUTPUT);
    gpio_reset_pin(TRIG_GPIO); gpio_set_direction(TRIG_GPIO, GPIO_MODE_OUTPUT);
    gpio_reset_pin(ECHO_GPIO); gpio_set_direction(ECHO_GPIO, GPIO_MODE_INPUT);
    
    DHT11_init((gpio_num_t)DHT_GPIO);

    TickType_t last_dht_read = 0;
    int64_t last_activity_time = 0;
    bool temp_alert_sent = false;

    while (1) {
        xSemaphoreTake(sys_mutex, portMAX_DELAY);

        float dist = get_distance_cm();
        bool person_nearby = (dist > 0 && dist < DOOR_THRESHOLD_CM);
        
        if (!door_is_open) {
            if (person_nearby && !system_armed) {
                door_is_open = true;
                last_activity_time = esp_timer_get_time();

                esp_rmaker_param_update_and_report(param_door_status, esp_rmaker_bool(door_is_open));
                esp_rmaker_param_update_and_report(param_home_door, esp_rmaker_str("Open"));
                
                buzzer_doorbell();
                send_alert("Automatic Door Opened");
                esp_rmaker_param_update_and_report(param_sec_status, esp_rmaker_str("Door Opened"));
                esp_rmaker_param_update_and_report(param_home_sec, esp_rmaker_str("Door Open"));
                
                ESP_LOGI(TAG, "Door Opened Automatically");
                ESP_DIAG_EVENT(EVT_DOOR, "Door Opened");
            }
        } else {
            if (person_nearby) {
                last_activity_time = esp_timer_get_time();
            }

            if ((esp_timer_get_time() - last_activity_time > 10000000) || system_armed) {
                door_is_open = false;
                
                if (!system_armed) {
                    system_armed = true;
                    send_alert("System Auto-Armed: No Activity");
                } else {
                    send_alert("Door Closed");
                }

                esp_rmaker_param_update_and_report(param_door_status, esp_rmaker_bool(door_is_open));
                esp_rmaker_param_update_and_report(param_home_door, esp_rmaker_str("Closed"));
                esp_rmaker_param_update_and_report(param_sec_status, esp_rmaker_str(system_armed ? "Door Locked" : "Door Unlocked"));
                esp_rmaker_param_update_and_report(param_home_sec, esp_rmaker_str(system_armed ? "Locked" : "Unlocked"));
                
                ESP_LOGI(TAG, "Door Closed / System Locked");
                ESP_DIAG_EVENT(EVT_DOOR, "Door Closed");
            }
        }

        if (!blinking_active) {
            if (system_armed) {
                gpio_set_level(LED_RED_GPIO, 1);
                gpio_set_level(LED_GREEN_GPIO, 0);
            } else {
                if (door_is_open) {
                    gpio_set_level(LED_RED_GPIO, 0);
                    gpio_set_level(LED_GREEN_GPIO, 1);
                } else {
                    gpio_set_level(LED_RED_GPIO, 0);
                    gpio_set_level(LED_GREEN_GPIO, 0);
                }
            }
            buzzer_tone(false);
        }

        xSemaphoreGive(sys_mutex);

        if (xTaskGetTickCount() - last_dht_read > pdMS_TO_TICKS(2000)) {
            struct dht11_reading r = DHT11_read();
            if (r.status == 0) {
                esp_rmaker_param_update_and_report(param_temp, esp_rmaker_float(r.temperature));
                esp_rmaker_param_update_and_report(param_humidity, esp_rmaker_float(r.humidity));

                if (r.temperature > 50.0) {
                    if (!temp_alert_sent) {
                        char alert_msg[64];
                        snprintf(alert_msg, sizeof(alert_msg), "High Temp Alert: %.1f C", (float)r.temperature);
                        send_alert(alert_msg);
                        ESP_DIAG_EVENT(EVT_SYS, "High Temperature: %.1f", (float)r.temperature);
                        buzzer_error_sound(); 
                        temp_alert_sent = true;
                    }
                } else {
                    if (r.temperature < 48.0) {
                        temp_alert_sent = false;
                    }
                }
            }
            last_dht_read = xTaskGetTickCount();
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static esp_err_t write_cb(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param,
            const esp_rmaker_param_val_t val, void *priv, esp_rmaker_write_ctx_t *ctx) {
    
    if (ctx) {
        ESP_LOGI(TAG, "Received write request via RainMaker");
    }
    
    const char *device_name = esp_rmaker_device_get_name(device);
    const char *param_name = esp_rmaker_param_get_name(param);


    if (strcmp(param_name, "Set Password") == 0) {
        if (strlen(val.val.s) > 0 && strlen(val.val.s) < sizeof(master_password)) {
            strcpy(master_password, val.val.s);
            ESP_LOGI(TAG, "Password updated to: %s", master_password);
            
            nvs_handle_t my_handle;
            if (nvs_open("storage", NVS_READWRITE, &my_handle) == ESP_OK) {
                nvs_set_str(my_handle, "master_pw", master_password);
                nvs_commit(my_handle);
                nvs_close(my_handle);
            }
            send_alert("Security Password Changed via App");
            esp_rmaker_param_update_and_report(param, esp_rmaker_str("Updated"));
        } else {
            esp_rmaker_param_update_and_report(param, esp_rmaker_str("Invalid"));
        }
        return ESP_OK;
    }

    if (strcmp(param_name, "Speed") == 0) {
        if (strcmp(device_name, "Fan") == 0) {
            fan_speed = val.val.i;
            fan_state = (fan_speed > 0);
            
            esp_rmaker_param_update_and_report(param_fan_power, esp_rmaker_bool(fan_state));
            
            char buf[32];
            if (fan_speed == 0) snprintf(buf, sizeof(buf), "Fan Off");
            else snprintf(buf, sizeof(buf), "Fan Speed %d", fan_speed);
            
            esp_rmaker_param_update_and_report(param_fan_status, esp_rmaker_str(buf));
            esp_rmaker_param_update_and_report(param_home_fan, esp_rmaker_str(buf));
            
            buzzer_fan_speed_sound(fan_speed);
            ESP_DIAG_EVENT(EVT_DEV, "Fan Speed Changed: %d", fan_speed);
            send_alert(fan_state ? "Fan Speed Changed (App)" : "Fan Turned OFF (App)");
        }
        esp_rmaker_param_update_and_report(param, val);
    }
    else if (strcmp(param_name, "Power") == 0) {
        bool state = val.val.b;
        bool changed = false;

        if (strcmp(device_name, "Fan") == 0) {
            if (fan_state != state) { fan_state = state; changed = true; }
            if (fan_state && fan_speed == 0) fan_speed = 1;
            if (!fan_state) fan_speed = 0;
            
            esp_rmaker_param_update_and_report(param_fan_speed, esp_rmaker_int(fan_speed));
            
            char buf[32];
            if (fan_speed == 0) snprintf(buf, sizeof(buf), "Fan Off");
            else snprintf(buf, sizeof(buf), "Fan Speed %d", fan_speed);
            
            esp_rmaker_param_update_and_report(param_fan_status, esp_rmaker_str(buf));
            esp_rmaker_param_update_and_report(param_home_fan, esp_rmaker_str(buf));

             if (changed && fan_state) {
                buzzer_fan_sound();
                indicate_device_on();
            } else {
                buzzer_fan_speed_sound(fan_speed);
            }
            ESP_DIAG_EVENT(EVT_DEV, "Fan %s (App)", fan_state ? "ON" : "OFF");
            send_alert(fan_state ? "Fan Turned ON (App)" : "Fan Turned OFF (App)");
        }
        else if (strcmp(device_name, "Light") == 0) {
            if (light_state != state) { light_state = state; changed = true; }
            esp_rmaker_param_update_and_report(param_light_status, esp_rmaker_str(state ? "Light On" : "Light Off"));
            esp_rmaker_param_update_and_report(param_home_light, esp_rmaker_str(state ? "On" : "Off"));
            if (changed && state) {
                buzzer_light_sound();
                indicate_device_on();
            }
            ESP_DIAG_EVENT(EVT_DEV, "Light %s (App)", light_state ? "ON" : "OFF");
            send_alert(light_state ? "Light Turned ON (App)" : "Light Turned OFF (App)");
        }
        else if (strcmp(device_name, "TV") == 0) {
            if (tv_state != state) { tv_state = state; changed = true; }
            esp_rmaker_param_update_and_report(param_tv_status, esp_rmaker_str(state ? "TV On" : "TV Off"));
            esp_rmaker_param_update_and_report(param_home_tv, esp_rmaker_str(state ? "On" : "Off"));
            if (changed && state) {
                buzzer_tv_sound();
                indicate_device_on();
            }
            ESP_DIAG_EVENT(EVT_DEV, "TV %s (App)", tv_state ? "ON" : "OFF");
            send_alert(tv_state ? "TV Turned ON (App)" : "TV Turned OFF (App)");
        }
        else if (strcmp(device_name, "Plug") == 0) {
            if (plug_state != state) { plug_state = state; changed = true; }
            esp_rmaker_param_update_and_report(param_plug_status, esp_rmaker_str(state ? "Plug On" : "Plug Off"));
            esp_rmaker_param_update_and_report(param_home_plug, esp_rmaker_str(state ? "On" : "Off"));
            if (changed && state) {
                buzzer_plug_sound();
                indicate_device_on();
            }
            ESP_DIAG_EVENT(EVT_DEV, "Plug %s (App)", plug_state ? "ON" : "OFF");
            send_alert(plug_state ? "Plug Turned ON (App)" : "Plug Turned OFF (App)");
        }
        
        if (changed && state) {
            buzzer_fan_sound();
        }

        esp_rmaker_param_update_and_report(param, val);
    }
    return ESP_OK;
}

void app_main()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    nvs_handle_t my_handle;
    if (nvs_open("storage", NVS_READONLY, &my_handle) == ESP_OK) {
        size_t required_size = sizeof(master_password);
        if (nvs_get_str(my_handle, "master_pw", master_password, &required_size) == ESP_OK) {
            ESP_LOGI(TAG, "Loaded Password from NVS: %s", master_password);
        }
        nvs_close(my_handle);
    }

    sys_mutex = xSemaphoreCreateMutex();
    notification_queue = xQueueCreate(5, sizeof(struct { char message[96]; }));

    app_network_init();

    esp_rmaker_config_t rainmaker_cfg = { .enable_time_sync = true };
    esp_rmaker_node_t *node = esp_rmaker_node_init(&rainmaker_cfg, "SMART_HOME_SYSTEM", "ESP32+Sensors");

    esp_rmaker_device_t *home = esp_rmaker_device_create("Home", ESP_RMAKER_DEVICE_OTHER, NULL);
    param_temp = esp_rmaker_param_create("Temperature", NULL, esp_rmaker_float(0), PROP_FLAG_READ);
    param_humidity = esp_rmaker_param_create("Humidity", NULL, esp_rmaker_float(0), PROP_FLAG_READ);
    param_alert = esp_rmaker_param_create("System Alert", NULL, esp_rmaker_str("System OK"), PROP_FLAG_READ);
    
    param_home_fan = esp_rmaker_param_create("Fan Status", NULL, esp_rmaker_str("Off"), PROP_FLAG_READ);
    param_home_light = esp_rmaker_param_create("Light Status", NULL, esp_rmaker_str("Off"), PROP_FLAG_READ);
    param_home_tv = esp_rmaker_param_create("TV Status", NULL, esp_rmaker_str("Off"), PROP_FLAG_READ);
    param_home_plug = esp_rmaker_param_create("Plug Status", NULL, esp_rmaker_str("Off"), PROP_FLAG_READ);
    param_home_door = esp_rmaker_param_create("Door Status", NULL, esp_rmaker_str("Closed"), PROP_FLAG_READ);
    param_home_sec = esp_rmaker_param_create("Security Mode", NULL, esp_rmaker_str("Locked"), PROP_FLAG_READ);

    esp_rmaker_device_add_param(home, param_temp);
    esp_rmaker_device_add_param(home, param_humidity);
    esp_rmaker_device_add_param(home, param_alert);
    esp_rmaker_device_add_param(home, param_home_fan);
    esp_rmaker_device_add_param(home, param_home_light);
    esp_rmaker_device_add_param(home, param_home_tv);
    esp_rmaker_device_add_param(home, param_home_plug);
    esp_rmaker_device_add_param(home, param_home_door);
    esp_rmaker_device_add_param(home, param_home_sec);
    
    esp_rmaker_node_add_device(node, home);

    esp_rmaker_device_t *sec = esp_rmaker_device_create("Security", ESP_RMAKER_DEVICE_OTHER, NULL);
    param_door_status = esp_rmaker_param_create("Door", NULL, esp_rmaker_bool(false), PROP_FLAG_READ);
    param_sec_status = esp_rmaker_param_create("Status", NULL, esp_rmaker_str("Door Locked"), PROP_FLAG_READ);
    
    param_set_pw = esp_rmaker_param_create("Set Password", NULL, esp_rmaker_str(""), PROP_FLAG_WRITE);
    esp_rmaker_device_add_param(sec, param_set_pw);

    esp_rmaker_device_add_param(sec, param_door_status);
    esp_rmaker_device_add_param(sec, param_sec_status);
    
    esp_rmaker_device_add_cb(sec, write_cb, NULL);
    
    esp_rmaker_node_add_device(node, sec);

    esp_rmaker_device_t *fan = esp_rmaker_device_create("Fan", ESP_RMAKER_DEVICE_FAN, NULL);
    param_fan_power = esp_rmaker_power_param_create("Power", false);
    esp_rmaker_device_add_param(fan, param_fan_power);
    esp_rmaker_device_assign_primary_param(fan, param_fan_power);
    
    param_fan_status = esp_rmaker_param_create("Status", NULL, esp_rmaker_str("Fan Off"), PROP_FLAG_READ);
    esp_rmaker_device_add_param(fan, param_fan_status);
    
    param_fan_speed = esp_rmaker_param_create("Speed", ESP_RMAKER_PARAM_SPEED, esp_rmaker_int(0), PROP_FLAG_READ | PROP_FLAG_WRITE);
    esp_rmaker_param_add_ui_type(param_fan_speed, ESP_RMAKER_UI_SLIDER);
    esp_rmaker_param_add_bounds(param_fan_speed, esp_rmaker_int(0), esp_rmaker_int(5), esp_rmaker_int(1));
    esp_rmaker_device_add_param(fan, param_fan_speed);

    esp_rmaker_device_add_cb(fan, write_cb, NULL);
    esp_rmaker_node_add_device(node, fan);

    esp_rmaker_device_t *light = esp_rmaker_device_create("Light", ESP_RMAKER_DEVICE_LIGHTBULB, NULL);
    param_light_power = esp_rmaker_power_param_create("Power", false);
    esp_rmaker_device_add_param(light, param_light_power);
    esp_rmaker_device_assign_primary_param(light, param_light_power);

    param_light_status = esp_rmaker_param_create("Status", NULL, esp_rmaker_str("Light Off"), PROP_FLAG_READ);
    esp_rmaker_device_add_param(light, param_light_status);
    esp_rmaker_device_add_cb(light, write_cb, NULL);
    esp_rmaker_node_add_device(node, light);

    esp_rmaker_device_t *tv = esp_rmaker_device_create("TV", ESP_RMAKER_DEVICE_TV, NULL);
    param_tv_power = esp_rmaker_power_param_create("Power", false);
    esp_rmaker_device_add_param(tv, param_tv_power);
    esp_rmaker_device_assign_primary_param(tv, param_tv_power);

    param_tv_status = esp_rmaker_param_create("Status", NULL, esp_rmaker_str("TV Off"), PROP_FLAG_READ);
    esp_rmaker_device_add_param(tv, param_tv_status);
    esp_rmaker_device_add_cb(tv, write_cb, NULL);
    esp_rmaker_node_add_device(node, tv);

    esp_rmaker_device_t *plug = esp_rmaker_device_create("Plug", ESP_RMAKER_DEVICE_SOCKET, NULL);
    param_plug_power = esp_rmaker_power_param_create("Power", false);
    esp_rmaker_device_add_param(plug, param_plug_power);
    esp_rmaker_device_assign_primary_param(plug, param_plug_power);

    param_plug_status = esp_rmaker_param_create("Status", NULL, esp_rmaker_str("Plug Off"), PROP_FLAG_READ);
    esp_rmaker_device_add_param(plug, param_plug_status);
    esp_rmaker_device_add_cb(plug, write_cb, NULL);
    esp_rmaker_node_add_device(node, plug);

    esp_rmaker_ota_enable_default();
    app_insights_enable();

    esp_rmaker_start();
    app_network_start(POP_TYPE_RANDOM);

    xTaskCreate(sensor_task, "sensor_task", 4096, NULL, 5, NULL);
    xTaskCreate(keypad_task, "keypad_task", 4096, NULL, 5, NULL);
    xTaskCreate(notification_task, "notify_task", 3072, NULL, 3, NULL);
}