/*
 * Copyright (c) 2026 Paulus Schulinck (Github @PaulskPt)
 *
 * SPDX-License-Identifier: MIT
 * 
 * Project: ble_ntp_time
 * Local BLE GATT User: nRF54LM20-DK
 * Remote BLE GATT User: RPiCM5 (~/pi_ble_oled)
 * 
 * Created with help from Google AI
 * 
 * Note: attached via I2C an 1.12in mono OLED by Adafruit, see:
 * https://www.adafruit.com/product/5297
 * 
 * Zephyr Display Driver Backend API:
 * https://docs.zephyrproject.org/latest/doxygen/html/group__display__interface__backend.html
 * 
 * Zephyr display_driver_api_ Struct Reference:
 * https://docs.zephyrproject.org/latest/doxygen/html/structdisplay__driver__api.html#details
 * e.g.: display_set_brightness_api, display_set_contrast_api, 
 * display_set_orientation_api, display_clear_api
 * 
 * Zephyr Monochrome Character Framebuffer
 * https://docs.zephyrproject.org/latest/doxygen/html/group__monochrome__character__framebuffer.html
 * 
 * See (new) nRF54LM20-DK (nRF54LM20B) documentation at:
 * https://docs.zephyrproject.org/latest/boards/nordic/nrf54lm20dk/doc/index.html
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/display/cfb.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
// ---- Additions @PaulskPt:
#include <zephyr/drivers/display.h>
#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/settings/settings.h>
#include <zephyr/posix/posix_time.h>
#include <time.h>
#include "secret.h"
/* ==================================================================== */
/* FORWARD DECLARATIONS (Fixes Implicit Declaration Errors)             */
/* ==================================================================== */
static void print_word_wrapped(const char *text, uint8_t start_row);
void update_screen(void);

/* Ensure your RX write callback has a prototype if defined lower down */
static ssize_t write_rx_data(struct bt_conn *conn,
			     const struct bt_gatt_attr *attr,
			     const void *buf, uint16_t len,
			     uint16_t offset, uint8_t flags);

bool last_epoch_displayed = false;
char last_epoch_str[11] = {0}; // see write_rx_data()

#define BT_UUID_OLED_SERVICE_VAL \
	BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x567812345678)
#define BT_UUID_OLED_WRITE_VAL \
	BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x567812345679)

static struct bt_uuid_128 oled_service_uuid = BT_UUID_INIT_128(BT_UUID_OLED_SERVICE_VAL);
static struct bt_uuid_128 oled_write_uuid = BT_UUID_INIT_128(BT_UUID_OLED_WRITE_VAL);
/* ==================================================================== */
/* GLOBAL VARIABLES & STATE FLAGS                                       */
/* ==================================================================== */
const struct device *display_dev;

static bool lStart = true;

/* Global state tracker to keep the top header accurate */
static bool is_connected = false;

/* MASTER TIME OUT FIX -> Counts down seconds to clear the disconnect alert */
static int disconnect_alert_timeout_secs = 0;

/* MASTER TIME SYNC STORAGE: Placed here so update_screen can see them! */
static time_t synchronized_epoch = 0;
static uint32_t sync_uptime_reference = 0;

/* 1. Declare a system work tracking structure at file scope */
static struct k_work adv_restart_work;

/* Your global incoming text buffer variable from the Pi */
/* CORRECT FIX: Allocate the actual memory buffer here so the linker can find it */
char display_buffer[128] = {0}; 

/**
 * @brief Converts a BLE HCI disconnect reason code into human-readable text.
 *        Extracts the requested text portions between parentheses only.
 */
static const char *get_verbose_disconnect_reason(uint8_t reason)
{
    switch (reason) {
    case BT_HCI_ERR_AUTH_FAIL:                  // 0x05
        return "Authentication Failure";
    case BT_HCI_ERR_PIN_OR_KEY_MISSING:         // 0x06
        return "PIN or Key Missing";
    case BT_HCI_ERR_MEM_CAPACITY_EXCEEDED:      // 0x07
        return "Memory Capacity Exceeded";
    case BT_HCI_ERR_CONN_TIMEOUT:               // 0x08
        return "Connection Timeout";
    case BT_HCI_ERR_REMOTE_USER_TERM_CONN:      // 0x13
        return "Remote User Terminated Connection";
    case BT_HCI_ERR_REMOTE_LOW_RESOURCES:       // 0x14
        return "Remote Device Low Resources";
    case BT_HCI_ERR_REMOTE_POWER_OFF:           // 0x15
        return "Remote Device Power Off";
    case BT_HCI_ERR_LOCALHOST_TERM_CONN:        // 0x16
        return "Connection Terminated by Local Host";
    case BT_HCI_ERR_UNSUPP_REMOTE_FEATURE:      // 0x1A
        return "Unsupported Remote Feature";
    case BT_HCI_ERR_PAIRING_NOT_SUPPORTED:      // 0x29
        return "Pairing Not Supported";
    case BT_HCI_ERR_UNACCEPT_CONN_PARAM:        // 0x3B
        return "Unacceptable Connection Parameters";
    default:
        return "Unspecified or Unknown Error";
    }
}

void setContrast()
{
	int contr = 50;
	int err = display_set_contrast(display_dev, contr);
	if (err) {
		printf("Display set contrast failed (err %d)\n", err);
	} else {
		printf("Display contrast successfully initialized to %d\n", contr);
	}
}


/**
 * @brief Prints an input string onto the OLED display with smart word-wrapping.
 *        Prevents mid-word splitting by wrapping whole words at space boundaries.
 *        Max character capacity per line: 16 characters (128 pixels / 8 pixels per char).
 */
static void print_word_wrapped(const char *text, uint8_t start_row)
{
    if (!text || !display_dev) return;

    char buffer[128];
    strncpy(buffer, text, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    char line_buffer[32] = "";
    uint8_t current_row = start_row;
    
    /* Tokenize the input text by looking for space characters */
    char *word = strtok(buffer, " ");
    
    while (word != NULL) {
        size_t current_len = strlen(line_buffer);
        size_t word_len = strlen(word);
        
        /* Check if adding this word (plus a space) exceeds our 16-character boundary */
        size_t space_needed = (current_len > 0) ? 1 : 0;
        
        if (current_len + space_needed + word_len <= 16) {
            /* Word fits cleanly on the current line! Append it */
            if (current_len > 0) {
                strcat(line_buffer, " ");
            }
            strcat(line_buffer, word);
        } else {
            /* Word does not fit. Print the completed line buffer onto the current row */
            if (current_len > 0 && current_row <= 112) {
                cfb_print(display_dev, line_buffer, 0, current_row);
                current_row += 16; // Drop down exactly one font row height
            }
            
            /* Start the next line buffer with the word that didn't fit */
            strncpy(line_buffer, word, sizeof(line_buffer) - 1);
            line_buffer[sizeof(line_buffer) - 1] = '\0';
        }
        
        /* Move to the next space-separated word token */
        word = strtok(NULL, " ");
    }
    
    /* Print any remaining characters left over in the line buffer */
    if (strlen(line_buffer) > 0 && current_row <= 112) {
        cfb_print(display_dev, line_buffer, 0, current_row);
    }
}

/**
 * @brief Handles all text frame drawings and screen updates systematically.
 */
void update_screen(void)
{
	if (!display_dev || !device_is_ready(display_dev)) return;

	/* Handle frame buffer clear while honoring your verified gating filter */
	if (lStart) {
		lStart = false;
		setContrast();
		cfb_framebuffer_clear(display_dev, true);
		cfb_framebuffer_set_font(display_dev, 0);
	} else {
		cfb_framebuffer_clear(display_dev, false);
	}

	/* 1. DYNAMIC TOP HEADER: Always draw status cleanly based on active event states (Row 0) */
	if (!is_connected && disconnect_alert_timeout_secs > 0) {
		cfb_print(display_dev, "STATUS DROP", 0, 0);
	} else if (is_connected) {
		cfb_print(display_dev, "BLE: LINKED", 0, 0);
	} else {
		cfb_print(display_dev, "BLE: SEARCH", 0, 0);
	}

	/* 2. FIXED SEPARATOR LINE: Maintained strictly on Row 16 */
	cfb_print(display_dev, "-----------", 0, 16);

	/* ==================================================================== */
	/* CASCADING PUSHED-DOWN VERTICAL LAYOUT MATRIX                         */
	/* ==================================================================== */

	/* 3. RAW INCOMING EPOCH STRING DISPLAY: Shifted from Row 96 up to Row 32 */
	if (strlen(last_epoch_str) > 0) {
		/* Print to your SH1107 OLED glass memory segments */
		cfb_print(display_dev, last_epoch_str, 0, 32);
		
		/* Continuous serial diagnostic output check */
		if (!last_epoch_displayed) {
			printf("UI Log Check: Displaying Last Captured Epoch String = \"%s\"\n", last_epoch_str);
			last_epoch_displayed = true; /* Toggle state flag guard */
		}
	} else {
		cfb_print(display_dev, "No Epoch", 0, 32);
	}

	/* 4. DYNAMIC DATA TRACKING VARIABLES DECLARATION */
	char date_str[32] = {0};
	char time_str[32] = {0};
	char header_str[32] = {0};
	const char *zone_label = ""; /* Starts blank until time data packet lands */

	if (synchronized_epoch > 0) {
		uint32_t elapsed_ms = k_uptime_get_32() - sync_uptime_reference;
		
		/* BASELINE UTC TIME: Raw synchronized counter plus ticking seconds */
		time_t utc_running_time = synchronized_epoch + (elapsed_ms / 1000);
		
		/* ==================================================================== */
		/* AUTOMATED PORTUGAL TIME ZONE ENGINE (Europe/Lisbon)                  */
		/* ==================================================================== */
		int current_gmt_offset_seconds = 0; /* Default to Winter Time (GMT+0) */
		zone_label = " (WET)";              /* Default zone suffix string */

		/* Scan through your 10-year lookup table to check the current date range */
		for (size_t i = 0; i < WEST_TABLE_SIZE; i++) {
			if (utc_running_time >= west_table[i].start_epoch && 
			    utc_running_time <= west_table[i].end_epoch) {
				current_gmt_offset_seconds = 3600; /* Force WEST (GMT+1) */
				zone_label = " (WEST)";            /* Switch suffix string */
				break;
			}
		}

		/* Apply the dynamically selected offset directly to the baseline UTC counter */
		time_t local_running_time = utc_running_time + current_gmt_offset_seconds;
		
		/* Convert the adjusted local timestamp into hours, minutes, and calendar data */
		struct tm *time_info = gmtime(&local_running_time);
		
		/* Format calendar date as yyyy-mm-dd (tm_year is years since 1900, tm_mon is 0-11) */
		snprintf(date_str, sizeof(date_str), "%04d-%02d-%02d",
			 time_info->tm_year + 1900, time_info->tm_mon + 1, time_info->tm_mday);

		/* Format strictly as pure digits HH:MM:SS */
		snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d", 
			 time_info->tm_hour, time_info->tm_min, time_info->tm_sec);
	} else {
		snprintf(date_str, sizeof(date_str), "---- -- --");
		snprintf(time_str, sizeof(time_str), "UNKNOWN");
	}

	/* 5. CALENDAR DATE ROW: Pushed down from Row 32 to Row 48 */
	cfb_print(display_dev, date_str, 0, 48);

	/* 6. TIME ZONE HEADER ROW: Pushed down from Row 48 to Row 64 */
	snprintf(header_str, sizeof(header_str), "Time%s", zone_label);
	cfb_print(display_dev, header_str, 0, 64);

	/* 7. CLOCK COUNTER ROW: Pushed down from Row 64 to Row 80 */
	cfb_print(display_dev, time_str, 0, 80);

	/* 8. PERSISTENT SENSOR METRIC DISPLAY: Pushed down from Row 80 to Row 96 */
	if (strlen(display_buffer) > 0) {
		print_word_wrapped(display_buffer, 96);
	} else {
		print_word_wrapped("Waiting for data", 96);
	}

	/* Re-render changes directly onto your Adafruit glass surface */
	cfb_framebuffer_finalize(display_dev);
}



/**
 * @brief GATT Callback triggered when text is received over the air from the Pi CM5.
 *        Stores the payload in the global array and triggers a wrapped screen refresh.
 */
static ssize_t write_rx_data(struct bt_conn *conn,
			     const struct bt_gatt_attr *attr,
			     const void *buf, uint16_t len,
			     uint16_t offset, uint8_t flags)
{
	/* REMOVE OR COMMENT OUT OLD 8-BYTE VALIDATION LOOP: */
	/* 
	if (len != 8) {
		printf("Time Sync Error: Invalid payload length received (%d bytes)\n", len);
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}
	*/

	/* Declare our temporary scratch buffer (64 bytes is perfect) */
	char temp_incoming[64];

	/* Ensure we don't overflow our local buffer array footprint */
	if (len >= sizeof(temp_incoming)) {
		len = sizeof(temp_incoming) - 1;
	}

	/* Copy exactly 'len' bytes from the active BLE raw data buffer stream */
	memcpy(temp_incoming, buf, len);
	
	/* Enforce strict null-termination at exactly the data boundary index */
	temp_incoming[len] = '\0';

	printf("BLE Combined Event: String parsed safely = \"%s\" (Length: %d)\n", temp_incoming, len);

	/* Locate the comma token delimiter inside the verified string container */
	char *comma_ptr = strchr(temp_incoming, ',');
	if (comma_ptr != NULL) {
		*comma_ptr = '\0';
		char *epoch_str = temp_incoming;
		strcpy(last_epoch_str, epoch_str); // copy to global variable
		last_epoch_displayed = false;

		char *sensor_str = comma_ptr + 1;

		/* 1. PARSE TIMESTAMP SEGMENT & LATCH CORE HARDWARE SYSTEM REGISTERS */
		uint64_t received_epoch = strtoull(epoch_str, NULL, 10);
		if (received_epoch > 0) {
			synchronized_epoch = (time_t)received_epoch;
			sync_uptime_reference = k_uptime_get_32();

			struct timespec ts;
			ts.tv_sec = (time_t)received_epoch;
			ts.tv_nsec = 0;
			sys_clock_settime(CLOCK_REALTIME, &ts);
			printf("  -> Clock synced to epoch: %llu\n", (unsigned long long)received_epoch);
		}

		/* 2. FORMAT AND COPY SENSOR METRIC INTO DISPLAY BUFFER */
		snprintf(display_buffer, sizeof(display_buffer), "Temp: %s C", sensor_str);
		printf("  -> Local UI Text Constructed: \"%s\"\n", display_buffer);
	} else {
		printf("Data Warning: Comma delimiter not found inside incoming byte array stream.\n");
	}

	/* Proactively force an instant screen redraw to refresh both clock and weather rows */
	update_screen();

	return len;
}

BT_GATT_SERVICE_DEFINE(oled_svc,
	BT_GATT_PRIMARY_SERVICE(&oled_service_uuid),
	BT_GATT_CHARACTERISTIC(&oled_write_uuid.uuid,
		BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
		BT_GATT_PERM_WRITE, NULL, write_rx_data, NULL)
);

/* Custom 128-bit Vendor-Specific UUID definitions for Time Sync Service */
#define BT_UUID_TIME_SERVICE_VAL \
	BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef0)

/* Characteristic UUID for the 64-bit Epoch time variable payload */
#define BT_UUID_TIME_EPOCH_VAL \
	BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef1)

static struct bt_uuid_128 time_service_uuid = BT_UUID_INIT_128(BT_UUID_TIME_SERVICE_VAL);
static struct bt_uuid_128 time_epoch_uuid = BT_UUID_INIT_128(BT_UUID_TIME_EPOCH_VAL);

/**
 * @brief GATT Callback triggered when the Pi CM5 writes an 8-byte Unix Epoch timestamp.
 *        Packs the raw binary bytes into a uint64_t and caches it locally.
 */
/**
 * @brief GATT Callback triggered when the Pi CM5 writes an 8-byte Unix Epoch timestamp.
 *        Packs the raw binary bytes into a uint64_t and synchronizes BOTH software variables
 *        and the nRF54 internal hardware Real-Time Clock (RTC) calendar registers.
 */

/**
 * @brief GATT Callback triggered when the Pi CM5 writes an 8-byte Unix Epoch timestamp.
 *        Packs the raw binary bytes into a uint64_t and synchronizes BOTH your software variables
 *        and the nRF54 internal Global RTC (GRTC) hardware counter abstraction subsystem.
 */
/**
 * @brief GATT Callback triggered when the Pi CM5 writes an 8-byte Unix Epoch timestamp.
 *        Packs the raw binary bytes into a uint64_t and synchronizes BOTH software variables
 *        and the nRF54 internal Global Real-Time Counter (GRTC) hardware registers directly.
 */
/**
 * @brief GATT Callback triggered when the Pi CM5 writes an 8-byte Unix Epoch timestamp.
 *        Packs the raw binary bytes into a uint64_t and synchronizes BOTH software variables
 *        and Zephyr's underlying real-time system clock infrastructure cleanly.
 */
/**
 * @brief GATT Callback triggered when the Pi CM5 writes an 8-byte Unix Epoch timestamp.
 *        Packs the raw binary bytes into a uint64_t and synchronizes BOTH software variables
 *        and Zephyr's underlying real-time system clock infrastructure cleanly.
 */
static ssize_t write_epoch_time_cb(struct bt_conn *conn,
				   const struct bt_gatt_attr *attr,
				   const void *buf, uint16_t len,
				   uint16_t offset, uint8_t flags)
{
	char temp_incoming[64] = {0};

	if (len >= sizeof(temp_incoming)) {
		len = sizeof(temp_incoming) - 1;
	}
	memcpy(temp_incoming, buf, len);
	temp_incoming[len] = '\0';

	printf("BLE Combined Event: String parsed safely = \"%s\" (Length: %d)\n", temp_incoming, len);

	char *comma_ptr = strchr(temp_incoming, ',');
	if (comma_ptr != NULL) {
		*comma_ptr = '\0';
		char *epoch_str = temp_incoming;
		char *sensor_str = comma_ptr + 1;

		/* CRITICAL FIX: Latch the isolated epoch string into your globals right here! */
		strncpy(last_epoch_str, epoch_str, sizeof(last_epoch_str) - 1);
		last_epoch_str[sizeof(last_epoch_str) - 1] = '\0';
		last_epoch_displayed = false;

		/* 1. PARSE TIMESTAMP SEGMENT & LATCH CORE HARDWARE SYSTEM REGISTERS */
		uint64_t received_epoch = strtoull(epoch_str, NULL, 10);
		if (received_epoch > 0) {
			synchronized_epoch = (time_t)received_epoch;
			sync_uptime_reference = k_uptime_get_32();

			struct timespec ts;
			ts.tv_sec = (time_t)received_epoch;
			ts.tv_nsec = 0;

			if (sys_clock_settime(CLOCK_REALTIME, &ts) == 0) {
				printf("  -> Hardware Success: Zephyr core clock synced to epoch: %llu\n", (unsigned long long)received_epoch);
			} else {
				printf("  -> Hardware Warning: Real-time clock update rejected by system core.\n");
			}
		}

		/* 2. FORMAT AND COPY SENSOR METRIC INTO DISPLAY BUFFER */
		snprintf(display_buffer, sizeof(display_buffer), "Temp: %s C", sensor_str);
		printf("  -> Local UI Text Constructed: \"%s\"\n", display_buffer);
	} else {
		printf("Data Warning: Comma delimiter not found inside incoming byte array stream.\n");
	}

	update_screen();
	return len;
}



/* Register the Time Sync Service directly into your GATT Database layout */
BT_GATT_SERVICE_DEFINE(time_sync_svc,
	BT_GATT_PRIMARY_SERVICE(&time_service_uuid),
	BT_GATT_CHARACTERISTIC(&time_epoch_uuid.uuid,
			       BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_WRITE,
			       NULL, write_epoch_time_cb, NULL),
);


// Note @PaulskPt The adv_restart_handler() added by Google AI
// to handle the "Failed to restart advertising loop (err -12), 
// after the remote user terminated connection



/* 2. Create the clean background worker callback execution loop */
static void adv_restart_handler(struct k_work *work)
{
    static const struct bt_le_adv_param explicit_adv_param = {
        .id = 0,
        .sid = 0,
        .secondary_max_skip = 0,
        .options = BT_LE_ADV_OPT_CONN,
        .interval_min = 0x00a0,          /* 100ms advertising interval */
        .interval_max = 0x00f0,          /* 150ms advertising interval */
    };

    /* Executing here gives the stack the split-second buffer it needs to clear the lines */
    int err = bt_le_adv_start(&explicit_adv_param, NULL, 0, NULL, 0);
    if (err) {
        printf("Background Worker: Failed to restart advertising loop (err %d)\n", err);
    } else {
        printf("Connectable advertising restarted. Waiting for Pi CM5...\n");
    }
}

/* 3. Initialize this work structure right inside your main() function before booting BLE */
// Place this line inside main():
// k_work_init(&adv_restart_work, adv_restart_handler);


/* Update your advertisement packets to explicitly include the Local Device Name */
static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_OLED_SERVICE_VAL),
};

/* Include an extra Scan Response packet so the Pi can request the name string over the air */
static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static void connected(struct bt_conn *conn, uint8_t err)
{
    /* Clear the active display frame buffer */
    cfb_framebuffer_clear(display_dev, false);

    if (err) {
        printf("Connection failed (err %u)\n", err);
        cfb_print(display_dev, "BLE: FAILED", 0, 0);
        cfb_framebuffer_finalize(display_dev);
        return;
    }

    /* Print out to your VSCode terminal window tracker line */
    printf("Connected to client device!\n");

		/* ==================================================================== */
    /* MASTER FIX: Tell the rest of the application we are officially connected */
    /* ==================================================================== */
    is_connected = true;

    /* Line 1 (Row 0): MATCHED FIX -> Change to your preferred linked header */
    cfb_print(display_dev, "BLE: LINKED", 0, 0);

    /* Line 2 (Row 16): Clean horizontal spacer line layout */
    cfb_print(display_dev, "-----------", 0, 16);

    /* Line 3 (Row 32) and downwards: Run your smart word-wrapped text parser */
    /* This will automatically wrap "Terminal" and "Connected" cleanly onto separate lines */
    print_word_wrapped("Terminal Connected", 32);

    /* Flush layout data to your 1.12-inch Adafruit SH1107 OLED glass */
    cfb_framebuffer_finalize(display_dev);
}
/*
static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	const char *verbose_text = get_verbose_disconnect_reason(reason);
	printf("BLE Disconnected. Reason: 0x%02X -> %s\n", reason, verbose_text);

	is_connected = false;

	// 1. Arm the 15-second countdown timer right here 
	disconnect_alert_timeout_secs = 15;

	// 2. Save the verbose string into the shared array 
	strncpy(display_buffer, verbose_text, sizeof(display_buffer) - 1);
	display_buffer[sizeof(display_buffer) - 1] = '\0';

	// 3. Force an immediate screen refresh pass 
	update_screen();

	k_work_submit(&adv_restart_work);
}
*/
static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	const char *verbose_text = get_verbose_disconnect_reason(reason);
	printf("BLE Disconnected. Reason: 0x%02X -> %s\n", reason, verbose_text);

	/* 1. Toggle our clean core connection tracker status */
	is_connected = false;

	/* REMOVED: The 15-second countdown timer and the display_buffer overwrite blocks! */

	/* 2. Force an immediate screen refresh pass to toggle Row 0 */
	update_screen();

	/* 3. Submit work to natively spin advertising back up over the air */
	k_work_submit(&adv_restart_work);
}


/* Form a standard connection callback structure layout mapping your functions */
static struct bt_conn_cb conn_callbacks = {
	.connected = connected,
	.disconnected = disconnected,
};

/* Register the structure variable address into the core background event loop */
void register_callbacks(void)
{
	bt_conn_cb_register(&conn_callbacks);
}

int main(void)
{
	int err;

	display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(display_dev)) {
		printf("Display device not ready\n");
		return 0;
	}

  /* 1. Initialize the character framebuffer base engine */
	cfb_framebuffer_init(display_dev);
	
	/* 2. Set the global font profile baseline first */
	cfb_framebuffer_set_font(display_dev, 0);

	/* 3. Turn off hardware blanking to enable the pixel array */
	display_blanking_off(display_dev);

	/* 4. Establish initial hardware performance constraints */
	setContrast();

	err = display_set_brightness(display_dev, 200);
	if (err) {
		printf("Note: Dynamic software brightness throttling not supported by SH1107 hardware (%d)\n", err);
	}
	/* ==================================================================== */
  /* STEP 3 INITIALIZATION PLACE: Schedule background work tracking loops */
  /* ==================================================================== */
  k_work_init(&adv_restart_work, adv_restart_handler);
	printf("Initializing Bluetooth stack...\n");
	update_screen();

	err = bt_enable(NULL);
	if (err) {
		printf("Bluetooth init failed (err %d)\n", err);
		return 0;
	}

	printf("Bluetooth initialized successfully.\n");

	register_callbacks();
	
	/* Kick off your standard connectable advertising arrays safely */
	err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if (err) {
		printf("Advertising failed to start (err %d)\n", err);
		return 0;
	}

	printf("Advertising active. Waiting for Pi CM5 scan tracking...\n");

	/* ==================================================================== */
	/* MASTER TICK REFRESH ENGINE LOOP                                      */
	/* ==================================================================== */
	while (1) {
		/* 1. Sleep for exactly 1000ms (1 second) before running checks */
		k_sleep(K_MSEC(1000));

		/* 2. Check if a disconnect alert timer countdown is currently active */
		if (disconnect_alert_timeout_secs > 0) {
			
			/* Decrease our global countdown variable by one second */
			disconnect_alert_timeout_secs--;
			
			/* Log a quick tracking step to your VSCode serial terminal */
			printf("Alert timeout countdown tracking: %02d seconds remaining...\n", 
			       disconnect_alert_timeout_secs);
			
			/* The exact second the countdown hit zero, wipe the stale data */
			if (disconnect_alert_timeout_secs == 0) {
				memset(display_buffer, 0, sizeof(display_buffer));
				printf("Alert timeout expired. Wiping display memory segments...\n");
			}
		}

		/* 3. Force a complete screen drawing update to handle the active views */
		update_screen();
	}
	return 0;
}

