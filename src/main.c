#include <zephyr.h>
#include <stdio.h>
#include <string.h>
#include <drivers/gps.h>	
#include <modem_info.h>

#include "led_controller.h"
#include "uart_controller.h"
#include "modem_controller.h"
#include "gps_controller.h"
#include "http_controller.h"
#include "ruuvinode.h"
#include "data_parser.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(ruuvi_node, CONFIG_RUUVI_NODE_LOG_LEVEL);

/* size of stack area used by each thread */
#define STACKSIZE 1024

/* scheduling priority used by each thread */
#define PRIORITY 7
#define SLEEP_TIME	1000

// Status information
static atomic_val_t UART_STATUS;
static atomic_val_t MODEM_STATUS;
static atomic_val_t GPS_STATUS;
//static atomic_val_t HTTP_SOCKET_STATUS;
static atomic_val_t GPS_FIRST_FIX;
static int conf = 0;

// Sensor data
static struct gps_data gps_data;
double latT = 0;
double longT = 0;
static struct modem_param_info modem_param;

/* Stack definition for application workqueue */
K_THREAD_STACK_DEFINE(application_stack_area,
		      CONFIG_APPLICATION_WORKQUEUE_STACK_SIZE);
static struct k_work_q application_work_q;


//K_SEM_DEFINE(gps_timeout_sem, 0, 1);

static void set_gps_enable(const bool enable)
{
	if (enable == gps_control_is_enabled()) {
		return;
	}

	if (enable) {
		LOG_INF("Starting GPS");
		gps_control_enable();
		gps_control_start(K_SECONDS(1));

	} else {
		LOG_INF("Stopping GPS");
		gps_control_disable();
	}
}



/** @brief Starts gpsT as gpsThread */
//K_THREAD_DEFINE(gpsThread, STACKSIZE, gpsT, NULL, NULL, NULL,	PRIORITY, 0, K_NO_WAIT);

/** @brief Only called after first fix is acquired. */
static void gps_trigger_handler(struct device *dev, struct gps_trigger *trigger)
{
	LOG_INF("Entered GPS Trigger Handler!\n");
	static u32_t fix_count;

	ARG_UNUSED(trigger);

	if (++fix_count < CONFIG_GPS_CONTROL_FIX_COUNT) {
		return;
	}

	fix_count = 0;

	
	gps_sample_fetch(dev);
	int err = gps_channel_get(dev, GPS_CHAN_PVT, &gps_data);
	if(!err){
		atomic_set(&GPS_FIRST_FIX, 1);
		longT = gps_data.pvt.longitude;
		latT = gps_data.pvt.latitude;
		LOG_INF("GPS Coordinate Updated\n");
	}
	else{
		LOG_ERR("GPS Update Failure\n");
	}

	gps_control_stop(K_NO_WAIT);
}

int modem_data_get(void)
{
	int err;

	err = modem_info_params_get(&modem_param);
	if (err) {
		printk("Error getting modem_info: %d", err);
		return err;
	}

	return 0;
}

int modem_data_init(void)
{
	int err;

	err = modem_info_init();
	if (err) {
		printk("modem_info_init, error: %d", err);
		return err;
	}

	err = modem_info_params_init(&modem_param);
	if (err) {
		printk("modem_info_params_init, error: %d", err);
		return err;
	}

	/*err = modem_info_rsrp_register(modem_rsrp_handler);
	if (err) {
		printk("modem_info_rsrp_register, error: %d", err);
		return err;
	}*/

	return 0;
}

/** @brief Initialises the peripherals that are used by the application. */
static void sensors_init(void)
{
	atomic_set(&UART_STATUS, 1);
	atomic_set(&MODEM_STATUS, 1);
	atomic_set(&GPS_STATUS, 1);

	led_init();
	int ut = atomic_get(&UART_STATUS);
	int md = atomic_get(&MODEM_STATUS);
	int gp = atomic_get(&GPS_STATUS);
	
	//Turns status LEDs on,
	toggle_led_one(gp);
	toggle_led_two(ut);
	toggle_led_three(md);

	//Modem Data
	int err;
	err = modem_data_init();
	if (err) {
		LOG_ERR("modem_data_init, error: %d", err);
	}

	if(USE_LTE){
	//Modem LTE Connection
		md = lte_connect(LTE_INIT);
		if (md) {
			LOG_ERR("lte_connect, error: %d", md);
		}
		else{
			atomic_set(&MODEM_STATUS, md);
			toggle_led_three(md);
			setup_psm();
		}
	}

	//GPS
	if(USE_GPS){
		gp = gps_control_init(&application_work_q, gps_trigger_handler);
		if (gp) {
			LOG_ERR("gps_control_init, error %d", gp);
		}
		else{
			LOG_INF("GPS Initialised");
			atomic_set(&GPS_STATUS, gp);
			toggle_led_one(gp);
			k_sleep(5000);
			set_gps_enable(true);
		}
	}

	// UART
	while(ut){
		ut = uart_init();
	}
	toggle_led_two(ut);
	atomic_set(&UART_STATUS, ut);
	k_sleep(K_SECONDS(5));
}

void main(void)
{
	int err;

	LOG_INF("Application started\n");

	k_work_q_start(&application_work_q, application_stack_area,
		       K_THREAD_STACK_SIZEOF(application_stack_area),
		       CONFIG_APPLICATION_WORKQUEUE_PRIORITY);

	// Initilise the peripherals
	sensors_init();

	while(1){
		
		flash_led_four();
		if(!gps_control_is_active()){
			socket_toggle(true);
			k_sleep(K_SECONDS(1));
			if(!gps_control_is_active()){
				/*Check lte connection*/
				err = lte_connect(CHECK_LTE_CONNECTION);
				
				if(!err){
					open_post_socket();
					struct msg_buf msg;
					process_uart();
					encode_json(&msg, latT, longT);
					if(HTTPS_MODE){
						err = https_post(msg.buf, msg.len);
					}
					else{
						err = http_post(msg.buf, msg.len);
					}
					
					free(msg.buf);
					close_http_socket();
				}
				else{
					lte_connect(LTE_INIT);
				}
			}
			k_sleep(K_SECONDS(2));
			socket_toggle(false);
		}
		k_sleep(K_SECONDS(ADV_POST_INTERVAL-3));
	}

}
