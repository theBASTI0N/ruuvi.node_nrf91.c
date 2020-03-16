#include <zephyr.h>
#include <lte_lc.h>
#include <net/bsdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <modem_info.h>
#include <time.h>

#include "ruuvinode.h"
#include "modem_controller.h"

void setup_psm(void)
{
	// https://github.com/Rallare/fw-nrfconnect-nrf/blob/nrf9160_samples/samples/nrf9160/udp_gps/src/main.c
	/*
	* GPRS Timer 3 value (octet 3)
	*
	* Bits 5 to 1 represent the binary coded timer value.
	*
	* Bits 6 to 8 defines the timer value unit for the GPRS timer as follows:
	* Bits 
	* 8 7 6
	* 0 0 0 value is incremented in multiples of 10 minutes 
	* 0 0 1 value is incremented in multiples of 1 hour 
	* 0 1 0 value is incremented in multiples of 10 hours
	* 0 1 1 value is incremented in multiples of 2 seconds
	* 1 0 0 value is incremented in multiples of 30 seconds
	* 1 0 1 value is incremented in multiples of 1 minute
	* 1 1 0 value is incremented in multiples of 320 hours (NOTE 1)
	* 1 1 1 value indicates that the timer is deactivated (NOTE 2).
	*/
	char psm_settings[] = CONFIG_LTE_PSM_REQ_RPTAU;
	printk("PSM bits: %c%c%c\n", psm_settings[0], psm_settings[1],
	       psm_settings[2]);
	printk("PSM Interval: %c%c%c%c%c\n", psm_settings[3], psm_settings[4],
	       psm_settings[5], psm_settings[6], psm_settings[7]);
	int err = lte_lc_psm_req(true);
	if (err < 0) {
		printk("Error setting PSM: %d Errno: %d\n", err, errno);
	}
}

int lte_connect(enum modem_actions action)
{
	int err;

	enum lte_lc_nw_reg_status nw_reg_status;


	if (action == LTE_INIT) {
		if (IS_ENABLED(CONFIG_LTE_AUTO_INIT_AND_CONNECT)) {
			/* Do nothing, modem is already turned on
			 * and connected.
			 */
		} else {
			printk("\n\rConnecting to LTE network. \n\r");
			printk("\n\rThis may take several minutes.\n\r");
			err = lte_lc_init_and_connect();
			if (err == -ETIMEDOUT) {
				goto exit;
			} else if (err) {
				return err;
			}
		}
		printk("Connected to LTE network\n");
	} else if (action == CHECK_LTE_CONNECTION) {
		err = lte_lc_nw_reg_status_get(&nw_reg_status);
		if (err) {
			printk("lte_lc_nw_reg_status error: %d", err);
		}

		//printk("Checking LTE connection...");

		switch (nw_reg_status) {
		case LTE_LC_NW_REG_REGISTERED_HOME:
		case LTE_LC_NW_REG_REGISTERED_ROAMING:
			break;
		default:
			goto exit;
		}
	}

	

	return 0;

exit:

	printk("LTE link could not be established, or maintained\n");

	return 0;
}

time_t modem_ts(void){
	int err;
	static char ts_buf[MODEM_TIME_LEN + 1];
	struct tm t;
	time_t epoch;
	int tz;
	char *pch;
	err = modem_info_string_get(MODEM_INFO_DATE_TIME, ts_buf);
	if (err != MODEM_TIME_LEN) {
		printk("modem_info_string_get(MODEM time), error: %d", err);
		return -1;
	}
	
    char *msg = strdup(ts_buf);
	
	pch = strtok(msg, "/,:+");
	t.tm_year = atoi(pch);
	t.tm_year = t.tm_year + 2000 -1900;
	pch = strtok(NULL, "/,:+");
	t.tm_mon = atoi(pch);
	t.tm_mon = t.tm_mon -1;
	pch = strtok(NULL, "/,:+");
	t.tm_mday = atoi(pch);
	pch = strtok(NULL, "/,:+");
	t.tm_hour = atoi(pch);
	pch = strtok(NULL, "/,:+");
	t.tm_min = atoi(pch);
	pch = strtok(NULL, "/,:+");
    t.tm_sec = atoi(pch);
	pch = strtok(NULL, "/,:+");
	tz = atoi(pch);
	t.tm_isdst = -1;
	epoch = mktime(&t);
	memset(ts_buf, 0, MODEM_TIME_LEN + 1);
	free(msg);
	free(pch);
	return epoch;
}