/*
 * creating and running wifi
 */
#ifndef MAIN_DRIVERS_WIFI_MODULE_H_
#define MAIN_DRIVERS_WIFI_MODULE_H_

#include <esp_netif.h>
#include <stdint.h>
#include <esp_err.h>
#include <esp_wifi_types.h>

/*
 * WiFi default variables
 * ST mode parameters
 */
#define WIFI_PARAM_PREFIX			"wifi_"						//prefix name parameter for name WIFI settings
#define STA_PARAM_SSID_NAME			WIFI_PARAM_PREFIX"st_ssid"	//Name parameter for name network connect
#define STA_PARAM_PASWRD_NAME		WIFI_PARAM_PREFIX"st_psw"

/*
 * AP mode parameters
 */
#define AP_SSID					CONFIG_AP_PARAM_SSID_NAME		//network name if AP started
#define AP_PASS					CONFIG_AP_PARAM_PASWRD_NAME	    //if empty, then no password

#define AP_MAX_STA_CONN			4				//maximum count clients on AP

#define WIFI_ATTEMPT_MAX		10				//the maximum number of connection attempts in station mode.
												/* NOT implemented: After exhaustion, switch to access point mode.
												 * But every half hour we try to connect in station mode if
												 * there are no clients connected to the AP
												 */
#define WIFI_STA_RECONNECT_TIMEOUT_S	5		//reconnection period if the station lost the access point, seconds
#define WIFI_STA_CHECK_TIMEOUT_M		1		/* the period for checking the ability to connect to the access point, in minutes.
												 * Only if the access point was started after
												 * running out of connection attempts in station mode.
 	 	 	 	 	 	 	 	 	 	 	 	 */

// @formatter:off
typedef enum {
	WIFI_START_AP = WIFI_MODE_AP,
	WIFI_START_STA = WIFI_MODE_STA,
} wifi_mode_start_t;
// @formatter:on


/**
 * @brief Called when the IP interface becomes available, either in ST or AP mode.
 * @param[in] mode connected
 * @param[in] IP adresses
 */
typedef void (*wifi_ip_connected_cb)(wifi_mode_t mode, esp_ip4_addr_t ip);

/**
 * @brief Called when the UI becomes unavailable.
 */
typedef void (*wifi_ip_disconnected_cb)(void);

/**
 * @brief Launching WiFi in the specified mode. The previous mode is turned off. The launch result is returned.
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_STATE - launch failure, unable to start.
 *      	For example, there is not enough network name and password for station mode.
 *      -- other error values from the corresponding functions.
 */
esp_err_t wifi_start(wifi_mode_start_t mode);

/**
 * @brief stop WiFi. alias esp_wifi_stop
 * @return none
 */
void wifi_stop(void);

/**
 * @brief There are settings for the hundred mode
 * @return
 * 		- ESP_OK there are parameters.
 * 		- ESP_ERR_NOT_FOUND there are no paramters.
 */
esp_err_t wifi_is_sta_param(void);

/**
 * @brief There is a connection with the AP and an IP address has been received.
 * @return
 * 		- ESP_OK there is a connection.
 * 		- ESP_ERR_NOT_FOUND not connected to access point.
 */
esp_err_t wifi_is_sta_connected(void);

/**
 * @brief there are connected clients to soft AP.
 * @return
 * 		- ESP_OK there is a connection.
 * 		- ESP_ERR_NOT_FOUND no connected clients.
 */
esp_err_t wifi_is_ap_clients_connected(void);

/**
 * @brief setting up a wifi interface, initializing internal structures, etc.
 * @param[in] callback function for connection event to IP interface
 * @param[in] callback function for disconnection event
 */
void wifi_init(wifi_ip_connected_cb func_ip_connect, wifi_ip_disconnected_cb func_ip_disconnect);

#endif /* MAIN_DRIVERS_WIFI_MODULE_H_ */
