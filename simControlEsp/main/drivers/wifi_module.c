/*
 * creating and running wifi
 * start/stop http server
 */
#include <string.h>
#include <esp_netif.h>
#include <esp_log.h>
#include <esp_event.h>
#include <nvs.h>
#include <esp_wifi.h>
#include "freertos/task.h"
#include "lwip/ip4_addr.h" // Include for IP4_ADDR macro
#include "freertos/event_groups.h"
#include "freertos/queue.h"

#include "wifi_module.h"
#include "params.h"

static const char *TAG = "wifi_m";
#undef LOG_LOCAL_LEVEL
#define LOG_LOCAL_LEVEL ESP_LOG_ERROR

EventGroupHandle_t wifi_state_flags;
#define WIFI_RECONECT_NEED			BIT0	//Reconnection is required when the connection with the AP is lost
#define WIFI_STA_CONNECTED			BIT1	//Connection with AP established
#define WIFI_AP_CHECK_STA			BIT2	//You need to check the connection with the AP. see comment for WIFI_ATTEMPT_MAX
#define WIFI_AP_STA_NO_CONNECTED	BIT3	//no clients connected to access point

#define STORAGE_WIFI_PARAM "SR"

typedef struct {
	unsigned ssid :1;
	unsigned password :1;
} wifi_sta_conf_mask_t;

static wifi_ip_connected_cb wifi_ip_connected_func;
static wifi_ip_disconnected_cb wifi_ip_disconnected_func;

static QueueHandle_t ip_event_queue;

typedef struct {
    wifi_mode_t mode;
    esp_ip4_addr_t ip;
} wifi_ip_event_t;

static void wifi_ip_connected_task(void *pvParameters) {
    wifi_ip_event_t event_data;

    while (true) {
        if (xQueueReceive(ip_event_queue, &event_data, portMAX_DELAY)) {
			if (event_data.mode == WIFI_MODE_NULL){
				if (wifi_ip_disconnected_func) {
					wifi_ip_disconnected_func();
				}
			}
			else {
				if (wifi_ip_connected_func) {
					wifi_ip_connected_func(event_data.mode, event_data.ip);
				}
			}
		}
    }
}

/**
 * @brief Gatekeeper for the parameters of the wifi station.
 * @param[in] Pointer to a structure with parameters, if null then the parameters are only read.
 *            If specified, then, depending on the mask, the corresponding value is remembered.
 * @param[in] Indicates which setting should be saved.
 */
static wifi_sta_config_t sta_config_set(wifi_sta_config_t *set, wifi_sta_conf_mask_t mask) {
	static wifi_sta_config_t value = { .ssid = "", .password = "" };
	portMUX_TYPE reg_mutex = portMUX_INITIALIZER_UNLOCKED;
	taskENTER_CRITICAL(&reg_mutex);
	if (set) {
		if (mask.ssid)
			memcpy(value.ssid, set->ssid, sizeof(value.ssid) / sizeof(uint8_t));
		if (mask.password)
			memcpy(value.password, set->password, sizeof(value.password) / sizeof(uint8_t));
	}
	taskEXIT_CRITICAL(&reg_mutex);
	return value;
}

static esp_err_t read_wifi_param(const paramName_t param, char *value, size_t maxLen) {
	return read_nvs_param(STORAGE_WIFI_PARAM, param, value, maxLen);
}

static esp_err_t read_wifi_params(void) {

	wifi_sta_config_t value;
	esp_err_t ret = read_nvs_param(STORAGE_WIFI_PARAM, STA_PARAM_SSID_NAME, (char*) value.ssid, sizeof(value.ssid) / sizeof(uint8_t));
	ESP_LOGV(TAG, "ret read %s", esp_err_to_name(ret));
	if (ret == ESP_OK) {
		ret = read_nvs_param(STORAGE_WIFI_PARAM, STA_PARAM_PASWRD_NAME, (char*) value.password, sizeof(value.password) / sizeof(uint8_t));
		ESP_LOGV(TAG, "ret read %s", esp_err_to_name(ret));
		if (ret == ESP_OK) {
			const wifi_sta_conf_mask_t mask = { .ssid = 1, .password = 1 };
			sta_config_set(&value, mask);
			ESP_LOGV(TAG, "wifi param read %s %s", value.ssid, value.password);
		}
	}
	return ret;
}

static esp_err_t write_wifi_param(const paramName_t paramName, const char *value, size_t maxLen) {
	ESP_LOGV(TAG, "wifi param %s %s save", paramName, value);
	wifi_sta_config_t sta_config;
	wifi_sta_conf_mask_t mask = { .ssid = 0, .password = 0 };
	if (paramName && value) {
		if (strlen(value) <= maxLen) {
			if (!strcmp(paramName, STA_PARAM_SSID_NAME)) {
				strcpy((char*) sta_config.ssid, value);
				mask.ssid = 1;
			} else if (!strcmp(paramName, STA_PARAM_PASWRD_NAME)) {
				strcpy((char*) sta_config.password, value);
				mask.password = 1;
			}
			sta_config_set(&sta_config, mask);
			ESP_LOGV(TAG, "save OK");
			return ESP_OK;
		}
	}
	ESP_LOGE(TAG, "wifi param %s %s save error", paramName, value);
	return ESP_ERR_INVALID_ARG;
}

static esp_err_t save_wifi_params(void) {

	nvs_handle my_handle;
	esp_err_t ret = ESP_ERR_INVALID_SIZE;

	const wifi_sta_conf_mask_t mask; //the value is not important
	wifi_sta_config_t wifi_sta_param = sta_config_set(NULL, mask);
	ESP_LOGV(TAG, "start commit ssid len %d", strlen((char* )wifi_sta_param.ssid));
	if (strlen((char*) wifi_sta_param.ssid) > 0) {
		ESP_LOGV(TAG, "param save to nvs start");
		if (nvs_open(STORAGE_WIFI_PARAM, NVS_READWRITE, &my_handle) == ESP_OK) {
			if ((nvs_set_str(my_handle, STA_PARAM_SSID_NAME, (char*) wifi_sta_param.ssid) == ESP_OK)
					&& (nvs_set_str(my_handle, STA_PARAM_PASWRD_NAME, (char*) wifi_sta_param.password) == ESP_OK)) {
				ret = nvs_commit(my_handle);
				if (ret == ESP_OK) {
					nvs_close(my_handle);
					ESP_LOGV(TAG, "param save OK");
				}
			}
		}
	}
	return ret;
}

static void disconnect_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {

	wifi_ip_event_t ip_event = {
		.mode = WIFI_MODE_NULL,
	};
	IP4_ADDR(&ip_event.ip, 0, 0, 0, 0);
    if (xQueueSend(ip_event_queue, &ip_event, portMAX_DELAY) != pdPASS) {
        ESP_LOGE(TAG, "Failed to send IP event to queue");
    }

	xEventGroupClearBits(wifi_state_flags, WIFI_STA_CONNECTED);
}

// Обработчик события получения IP-адреса
static void event_got_ip_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;

    wifi_ip_event_t ip_event = {
        .mode = WIFI_MODE_STA,
        .ip = event->ip_info.ip
    };
    if (xQueueSend(ip_event_queue, &ip_event, portMAX_DELAY) != pdPASS) {
        ESP_LOGE(TAG, "Failed to send IP event to queue");
    }

    xEventGroupSetBits(wifi_state_flags, WIFI_STA_CONNECTED);
}

static void event_ap_start_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
	esp_netif_t *netif = (esp_netif_t*) arg;
	esp_netif_ip_info_t ip_info;
	if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {

		wifi_ip_event_t ip_event = {
			.mode = WIFI_MODE_STA,
			.ip = ip_info.ip
		};
		if (xQueueSend(ip_event_queue, &ip_event, portMAX_DELAY) != pdPASS) {
			ESP_LOGE(TAG, "Failed to send IP event to queue");
		}
	}
}

//station connect to/disconnect from AP
static void event_ap_change_st_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
	static uint8_t cliens_count = 0;
	ESP_LOGV(TAG, "AP envent %ld", event_id);
	if ((event_id == WIFI_EVENT_AP_STADISCONNECTED) && (cliens_count)) {
		cliens_count--;
	}
	if ((event_id == WIFI_EVENT_AP_STACONNECTED) && (cliens_count < 0xff)) {
		cliens_count++;
	}
	if (cliens_count) {
		xEventGroupClearBits(wifi_state_flags, WIFI_AP_STA_NO_CONNECTED);
		ESP_LOGV(TAG, "AP count clients %d", cliens_count);
		return;
	}
	xEventGroupSetBits(wifi_state_flags, WIFI_AP_STA_NO_CONNECTED);
	ESP_LOGV(TAG, "no clients from AP");
}

esp_err_t wifi_start(wifi_mode_start_t mode) {

	static esp_err_t netif_init_result = ESP_ERR_ESP_NETIF_INIT_FAILED;
	esp_err_t ret = ESP_ERR_INVALID_STATE;
	if (netif_init_result == ESP_ERR_ESP_NETIF_INIT_FAILED) {
		netif_init_result = esp_netif_init();
		if (netif_init_result == ESP_OK) {
			ret = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_got_ip_handler, NULL);
			if (ret != ESP_OK)
				return ret;
			ret = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, NULL);
			if (ret != ESP_OK)
				return ret;
			wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
			ret = esp_wifi_init(&cfg);
			if (ret != ESP_OK)
				return ret;
			esp_netif_create_default_wifi_sta();
			ret = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_STOP, &disconnect_handler, NULL);
			if (ret != ESP_OK)
				return ret;
			esp_netif_t *netif = esp_netif_create_default_wifi_ap();
			ret = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_START, &event_ap_start_handler, (void*) netif);
			if (ret != ESP_OK)
				return ret;
			ret = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_STACONNECTED, &event_ap_change_st_handler, NULL);
			if (ret != ESP_OK)
				return ret;
			ret = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_STADISCONNECTED, &event_ap_change_st_handler, NULL);
			if (ret != ESP_OK)
				return ret;
		}
	}
	ret = esp_wifi_set_mode((wifi_mode_t) mode);

	if (ret == ESP_OK) {
		wifi_config_t wifi_config = { .ap = { .ssid = AP_SSID } }; //This is how the network name for the AP is initialized, if you just copy the name into an array, it doesn�t work, initialization happens, but no one can connect to the network
		ret = ESP_ERR_INVALID_STATE;
		if (mode == WIFI_START_AP) {
			wifi_config.ap.ssid_len = strlen(AP_SSID);
			strcpy((char*) wifi_config.ap.password, AP_PASS);
			wifi_config.ap.max_connection = AP_MAX_STA_CONN;
			wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
			if (strlen(AP_PASS) == 0) {
				wifi_config.ap.authmode = WIFI_AUTH_OPEN;
			}
			esp_wifi_set_storage(WIFI_STORAGE_RAM);
			ret = esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config);
			if (ret == ESP_OK)
				ESP_LOGV(TAG, "ap net=%s pas=%s maxch %d", wifi_config.ap.ssid, wifi_config.ap.password, wifi_config.ap.max_connection);
		}
		if ((mode == WIFI_START_STA) && (wifi_is_sta_param() == ESP_OK)) {
			const wifi_sta_conf_mask_t mask; //the value is not important
			wifi_sta_config_t wifi_sta_param = sta_config_set(NULL, mask);
			ESP_LOGI(TAG, "st net=%s pas=%s", wifi_sta_param.ssid, wifi_sta_param.password);
			if (strlen((char*) wifi_sta_param.ssid) > 0) {
				ESP_LOGV(TAG, "config st set");
				strcpy((char*) wifi_config.sta.ssid, (char*) wifi_sta_param.ssid);
				strcpy((char*) wifi_config.sta.password, (char*) wifi_sta_param.password);
				esp_wifi_set_storage(WIFI_STORAGE_RAM);
				ret = esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
			}
		}
		if (ret == ESP_OK) {
			ret = esp_wifi_start();
			ESP_LOGV(TAG, "start");
		}
		if ((ret == ESP_OK) && (mode == WIFI_START_STA)) {
			ret = esp_wifi_connect();
			xEventGroupSetBits(wifi_state_flags, WIFI_RECONECT_NEED);
			ESP_LOGV(TAG, "STA connect start");
		}
	}
	return ret;
}

void wifi_stop(void) {
	esp_wifi_stop();
	if (esp_wifi_set_mode(WIFI_MODE_NULL) != ESP_OK) {
		ESP_LOGV(TAG, "error stop mode");
	}
	xEventGroupClearBits(wifi_state_flags, WIFI_RECONECT_NEED);
}

esp_err_t wifi_is_sta_param(void) {
	const wifi_sta_conf_mask_t mask; //the value is not important
	wifi_sta_config_t wifi_sta_param = sta_config_set(NULL, mask);
	if (strlen((char*) wifi_sta_param.ssid) > 0)
		return ESP_OK;
	return ESP_ERR_NOT_FOUND;
}

//reconnect if need, or AP on
static void wifi_reconnect(void *vParam) {
	uint32_t attempt_reconect = WIFI_ATTEMPT_MAX;
	uint32_t timeout_check_st = 0;
	while (1) {
		vTaskDelay((WIFI_STA_RECONNECT_TIMEOUT_S * 1000) / portTICK_PERIOD_MS);
		if (xEventGroupGetBits(wifi_state_flags) & WIFI_STA_CONNECTED) {
			attempt_reconect = WIFI_ATTEMPT_MAX;
			continue;
		}
		wifi_mode_t mode;
		if (esp_wifi_get_mode(&mode) == ESP_OK) {
			if ((mode == WIFI_MODE_STA) || (mode == WIFI_MODE_APSTA)) {
				if (!(xEventGroupGetBits(wifi_state_flags) & WIFI_RECONECT_NEED)) {
					continue;
				}
				if (attempt_reconect) {
					esp_wifi_connect();
					attempt_reconect--;
					ESP_LOGV(TAG, "attempt reconect %ld", attempt_reconect);
					continue;
				}
				//Attempts are exhausted, go to the access point mode.
				//But every half hour we try to connect in station mode if there are no clients connected to the AP
				wifi_stop();
				if (wifi_is_sta_param() == ESP_OK) {
					xEventGroupSetBits(wifi_state_flags, WIFI_RECONECT_NEED);
				}
				if (wifi_start(WIFI_START_AP) != ESP_OK) {
					ESP_LOGV(TAG, "alarm! AP off!");
				}
				ESP_LOGV(TAG, "connection attempts ended, AP mode on");
				xEventGroupSetBits(wifi_state_flags, WIFI_AP_CHECK_STA);
				timeout_check_st = (WIFI_STA_CHECK_TIMEOUT_M * 60) / WIFI_STA_RECONNECT_TIMEOUT_S;
				continue;
			}
			//AP mode, periodically check the availability of the access point.
#define CHECK_CONNECT_TO_AP (WIFI_RECONECT_NEED | WIFI_AP_CHECK_STA | WIFI_AP_STA_NO_CONNECTED)
			if ((xEventGroupGetBits(wifi_state_flags) & CHECK_CONNECT_TO_AP) == CHECK_CONNECT_TO_AP)//only if there is a need to reconnect and no connected clients
			{
				if (timeout_check_st) {
					timeout_check_st--;
					continue;
				}
				ESP_LOGV(TAG, "check connect to AP");
				xEventGroupClearBits(wifi_state_flags, WIFI_AP_CHECK_STA);
				wifi_stop();
				if (wifi_is_sta_param() == ESP_OK) {
					xEventGroupSetBits(wifi_state_flags, WIFI_RECONECT_NEED);
				}
				if (wifi_start(WIFI_START_STA) != ESP_OK) {
					ESP_LOGV(TAG, "alarm! ST off!");
				}
			}
		}
	}
}

esp_err_t wifi_is_sta_connected(void) {
	return (xEventGroupGetBits(wifi_state_flags) & WIFI_STA_CONNECTED) ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t wifi_is_ap_clients_connected(void) {
	return (xEventGroupGetBits(wifi_state_flags) & WIFI_AP_STA_NO_CONNECTED) ? ESP_ERR_INVALID_STATE : ESP_OK;
}

// Инициализация Wi-Fi
void wifi_init(wifi_ip_connected_cb func_ip_connect, wifi_ip_disconnected_cb func_ip_disconnect) {

	esp_log_level_set(TAG, LOG_LOCAL_LEVEL);

  	//Можно вызывать много раз, главное вызвать
	esp_event_loop_create_default();

	wifi_state_flags = xEventGroupCreate();
    if (!wifi_state_flags) {
        ESP_LOGE(TAG, "create wifi_state_flags fault!");
        return;
    }

    xEventGroupClearBits(wifi_state_flags, WIFI_RECONECT_NEED | WIFI_STA_CONNECTED | WIFI_AP_CHECK_STA);
    xEventGroupSetBits(wifi_state_flags, WIFI_AP_STA_NO_CONNECTED);

    wifi_sta_config_t param_len;
    if (paramReg(STA_PARAM_SSID_NAME, sizeof(param_len.ssid) / sizeof(uint8_t), read_wifi_param, write_wifi_param, save_wifi_params) == ESP_OK) {
        paramReg(STA_PARAM_PASWRD_NAME, sizeof(param_len.password) / sizeof(uint8_t), read_wifi_param, write_wifi_param, save_wifi_params);
    }

    read_wifi_params();

    wifi_ip_connected_func = func_ip_connect;
    wifi_ip_disconnected_func = func_ip_disconnect;

    // Создание очереди для передачи IP-данных
    ip_event_queue = xQueueCreate(5, sizeof(wifi_ip_event_t));
    if (!ip_event_queue) {
        ESP_LOGE(TAG, "Failed to create IP event queue");
        return;
    }

    // Создание задачи для обработки IP-данных
    xTaskCreatePinnedToCore(wifi_ip_connected_task, "wifi_ip_connected_task", 2048 * 3, NULL, 10, NULL, 1);

    // Создание задачи для управления подключением Wi-Fi
    xTaskCreatePinnedToCore(wifi_reconnect, "wifi_reconnect", 2048 * 3, NULL, 9, NULL, 1);
}