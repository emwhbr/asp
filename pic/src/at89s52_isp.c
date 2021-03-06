// ************************************************************************
// *                                                                      *
// * Copyright (C) 2014 Bonden i Nol (hakanbrolin@hotmail.com)            *
// *                                                                      *
// * This program is free software; you can redistribute it and/or modify *
// * it under the terms of the GNU General Public License as published by *
// * the Free Software Foundation; either version 2 of the License, or    *
// * (at your option) any later version.                                  *
// *                                                                      *
// ************************************************************************

#include "at89s52_isp.h"
#include "at89s52_io.h"
#include "xmodem.h"
#include "ihex.h"
#include "user_io.h"
#include "delay.h"
#include "asp_hw.h"
#include "utility.h"

/////////////////////////////////////////////////////////////////////////////
//               Definition of macros
/////////////////////////////////////////////////////////////////////////////
#define XMODEM_RECV_FLAGS  (XMODEM_FLAG_CRC)
//#define XMODEM_RECV_FLAGS 0

#define XMODEM_SEND_FLAGS  (XMODEM_FLAG_CRC)
//#define XMODEM_SEND_FLAGS 0

/////////////////////////////////////////////////////////////////////////////
//               Function prototypes
/////////////////////////////////////////////////////////////////////////////
static int enable_isp(void);
static void disable_isp(void);
static int isp_probe(void);
static int isp_erase(void);
static int isp_read_bin(void);
static int isp_write_bin(void);
static int isp_verify_bin(void);
static int isp_read_hex(void);
static int isp_write_hex(void);
static int isp_verify_hex(void);

static void signal_xmodem_error(int rc);
static void signal_xmodem_activity(void);

// Callback functions, binary data
static int cb_xmodem_read_bin_data(XMODEM_PACKET *xpack);
static int cb_xmodem_write_bin_data(const XMODEM_PACKET *xpack);
static int cb_xmodem_verify_bin_data(const XMODEM_PACKET *xpack);

// Callback functions, Intel HEX data
static int cb_xmodem_read_hex_data(XMODEM_PACKET *xpack);
static int cb_xmodem_write_hex_data(const XMODEM_PACKET *xpack);
static int cb_xmodem_verify_hex_data(const XMODEM_PACKET *xpack);

/////////////////////////////////////////////////////////////////////////////
//               Global variables
/////////////////////////////////////////////////////////////////////////////
static uint16_t g_chip_addr; // Keep track of current chip address

static uint8_t g_chip_data[XMODEM_PACKET_DATA_BYTES]; // Data read from chip

static IHEX g_ihex; // The opaque object for handling of Intel HEX records

/////////////////////////////////////////////////////////////////////////////
//               Public functions
/////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////

int at89s52_isp_probe(void)
{
  int rc;

  rc = enable_isp();  // Activate RESET pin
  if (rc != AT89S52_ISP_SUCCESS) goto at89s52_isp_probe_exit;
  rc = isp_probe();

 at89s52_isp_probe_exit:
  disable_isp();     // Deactivate RESET pin
  return rc;
}

////////////////////////////////////////////////////////////////

int at89s52_isp_erase(void)
{
  int rc;

  rc = enable_isp();  // Activate RESET pin
  if (rc != AT89S52_ISP_SUCCESS) goto at89s52_isp_erase_exit;
  rc = isp_erase();

 at89s52_isp_erase_exit:
  disable_isp();      // Deactivate RESET pin
  return rc;
}

////////////////////////////////////////////////////////////////

int at89s52_isp_read_bin(void)
{
  int rc;

  rc = enable_isp();  // Activate RESET pin
  if (rc != AT89S52_ISP_SUCCESS) goto at89s52_isp_read_bin_exit;
  rc = isp_read_bin();

 at89s52_isp_read_bin_exit:
  disable_isp();     // Deactivate RESET pin
  return rc;
}

////////////////////////////////////////////////////////////////

int at89s52_isp_write_bin(void)
{
  int rc;

  rc = enable_isp();  // Activate RESET pin
  if (rc != AT89S52_ISP_SUCCESS) goto at89s52_isp_write_bin_exit;
  rc = isp_erase();
  if (rc != AT89S52_ISP_SUCCESS) goto at89s52_isp_write_bin_exit;
  rc = isp_write_bin();

 at89s52_isp_write_bin_exit:
  disable_isp();     // Deactivate RESET pin
  return rc;
}

////////////////////////////////////////////////////////////////

int at89s52_isp_verify_bin(void)
{
  int rc;

  rc = enable_isp();  // Activate RESET pin
  if (rc != AT89S52_ISP_SUCCESS) goto at89s52_isp_verify_bin_exit;
  rc = isp_verify_bin();

 at89s52_isp_verify_bin_exit:
  disable_isp();     // Deactivate RESET pin
  return rc;
}

////////////////////////////////////////////////////////////////

int at89s52_isp_read_hex(void)
{
  int rc;

  rc = enable_isp();  // Activate RESET pin
  if (rc != AT89S52_ISP_SUCCESS) goto at89s52_isp_read_hex_exit;
  rc = isp_read_hex();

 at89s52_isp_read_hex_exit:
  disable_isp();     // Deactivate RESET pin
  return rc;
}

////////////////////////////////////////////////////////////////

int at89s52_isp_write_hex(void)
{
  int rc;

  rc = enable_isp();  // Activate RESET pin
  if (rc != AT89S52_ISP_SUCCESS) goto at89s52_isp_write_hex_exit;
  rc = isp_erase();
  if (rc != AT89S52_ISP_SUCCESS) goto at89s52_isp_write_hex_exit;
  rc = isp_write_hex();

 at89s52_isp_write_hex_exit:
  disable_isp();     // Deactivate RESET pin
  return rc;
}

////////////////////////////////////////////////////////////////

int at89s52_isp_verify_hex(void)
{
  int rc;

  rc = enable_isp();  // Activate RESET pin
  if (rc != AT89S52_ISP_SUCCESS) goto at89s52_isp_verify_hex_exit;
  rc = isp_verify_hex();

 at89s52_isp_verify_hex_exit:
  disable_isp();     // Deactivate RESET pin
  return rc;
}

/////////////////////////////////////////////////////////////////////////////
//               Private functions
/////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////

static int enable_isp(void)
{
  int rc;

  TARGET_RESET_PIN = 1; // Activate RESET pin

  // RESET must be active for a while, before any command is sent
  delay_ms(AT89S52_RESET_ACTIVE_MIN_TIME_MS);

  // Enable serial programming
  rc = at89s52_io_enable_programming();
  if (rc != AT89S52_SUCCESS) {
    TARGET_ERR_LED_ON; // Signal error
    return AT89S52_ISP_FAILURE;
  }
  return AT89S52_ISP_SUCCESS;
}

////////////////////////////////////////////////////////////////

static void disable_isp(void)
{
  TARGET_RESET_PIN = 0; // Deactivate RESET pin
}

////////////////////////////////////////////////////////////////

static int isp_probe(void)
{
  uint32_t signature;
  char signature_str[8];

  if (at89s52_io_read_signature(&signature) != AT89S52_SUCCESS) {
    TARGET_ERR_LED_ON; // Signal error
    return AT89S52_ISP_FAILURE;
  }
  if (signature == AT89S52_SIGNATURE) {
    user_io_new_line();
    user_io_put_line("Found AT89S52", 13);
  }
  else {
    user_io_new_line();
    uint32_to_hex_str(signature, signature_str);
    user_io_put("Bad signature=0x", 16);
    user_io_put_line(signature_str, 8);
    TARGET_ERR_LED_ON; // Signal error
    return AT89S52_ISP_FAILURE;
  }
  return AT89S52_ISP_SUCCESS;
}

////////////////////////////////////////////////////////////////

static int isp_erase(void)
{
  if (at89s52_io_chip_erase() != AT89S52_SUCCESS) {
    TARGET_ERR_LED_ON; // Signal error
    return AT89S52_ISP_FAILURE;
  }
  return AT89S52_ISP_SUCCESS;
}

////////////////////////////////////////////////////////////////

static int isp_read_bin(void)
{
  int rc;

  user_io_new_line();
  user_io_put_line("XMODEM recv now", 15);

  // Send a BIN file, let callback handle each packet
  // and read packet data from flash.
  g_chip_addr = 0;
  rc = xmodem_send_file(cb_xmodem_read_bin_data,
			AT89S52_FLASH_MEMORY_SIZE / XMODEM_PACKET_DATA_BYTES,
			XMODEM_SEND_FLAGS);
  ACTIV_LED_OFF;
  if (rc != XMODEM_SUCCESS) {
    signal_xmodem_error(rc);
    return AT89S52_ISP_FAILURE;
  }

  return AT89S52_ISP_SUCCESS;
}

////////////////////////////////////////////////////////////////

static int isp_write_bin(void)
{
  int rc;

  user_io_new_line();
  user_io_put_line("XMODEM send now", 15);
  ACTIV_LED_ON;

  // Receive a BIN file, let callback handle each packet
  // and write packet data to flash.
  g_chip_addr = 0;
  rc = xmodem_recv_file(cb_xmodem_write_bin_data,
			XMODEM_RECV_FLAGS);
  ACTIV_LED_OFF;
  if (rc != XMODEM_SUCCESS) {
    signal_xmodem_error(rc);
    return AT89S52_ISP_FAILURE;
  }

  return AT89S52_ISP_SUCCESS;
}

////////////////////////////////////////////////////////////////

static int isp_verify_bin(void)
{
  int rc;

  user_io_new_line();
  user_io_put_line("XMODEM send now", 15);
  ACTIV_LED_ON;

  // Receive a BIN file, let callback handle each packet
  // and compare packet data with data read from flash.
  g_chip_addr = 0;
  rc = xmodem_recv_file(cb_xmodem_verify_bin_data,
			XMODEM_RECV_FLAGS);
  ACTIV_LED_OFF;
  if (rc != XMODEM_SUCCESS) {
    signal_xmodem_error(rc);
    return AT89S52_ISP_FAILURE;
  }

  return AT89S52_ISP_SUCCESS;
}

////////////////////////////////////////////////////////////////

static int isp_read_hex(void)
{
  int rc;
  uint32_t file_size_bytes;
  uint16_t xmodem_packets; // Keep track of packets to send

  user_io_new_line();
  user_io_put_line("XMODEM recv now", 15);
  ACTIV_LED_ON;

  // Initialize opaque object
  ihex_initialize(&g_ihex);

  // Send a HEX file, let callback handle each packet
  // and read packet data from flash.
  g_chip_addr = 0;
  rc = ihex_get_file_size(IHEX_RECORD_MAX_BYTES,
			  AT89S52_FLASH_MEMORY_SIZE / IHEX_RECORD_MAX_BYTES,
			  &file_size_bytes);

  xmodem_packets = file_size_bytes / XMODEM_PACKET_DATA_BYTES;
  if ( (file_size_bytes % XMODEM_PACKET_DATA_BYTES) != 0) {
    xmodem_packets++; // Add one extra packet if necessary
  }

  rc = xmodem_send_file(cb_xmodem_read_hex_data,
			xmodem_packets,
			XMODEM_SEND_FLAGS);
  ACTIV_LED_OFF;
  if (rc != XMODEM_SUCCESS) {
    signal_xmodem_error(rc);
    return AT89S52_ISP_FAILURE;
  }

  return AT89S52_ISP_SUCCESS;
}

////////////////////////////////////////////////////////////////

static int isp_write_hex(void)
{
  int rc;

  user_io_new_line();
  user_io_put_line("XMODEM send now", 15);
  ACTIV_LED_ON;

  // Initialize opaque object
  ihex_initialize(&g_ihex);

  // Receive a HEX file, let callback handle each packet
  // and write packet data to flash.
  rc = xmodem_recv_file(cb_xmodem_write_hex_data,
			XMODEM_RECV_FLAGS);

  ACTIV_LED_OFF;
  if (rc != XMODEM_SUCCESS) {
    signal_xmodem_error(rc);
    return AT89S52_ISP_FAILURE;
  }

  return AT89S52_ISP_SUCCESS;
}

////////////////////////////////////////////////////////////////

static int isp_verify_hex(void)
{
  int rc;

  user_io_new_line();
  user_io_put_line("XMODEM send now", 15);
  ACTIV_LED_ON;

  // Initialize opaque object
  ihex_initialize(&g_ihex);

  // Receive a HEX file, let callback handle each packet
  // and compare packet data with data read from flash.
  rc = xmodem_recv_file(cb_xmodem_verify_hex_data,
			XMODEM_RECV_FLAGS);

  ACTIV_LED_OFF;
  if (rc != XMODEM_SUCCESS) {
    signal_xmodem_error(rc);
    return AT89S52_ISP_FAILURE;
  }

  return AT89S52_ISP_SUCCESS;
}

////////////////////////////////////////////////////////////////

static void signal_xmodem_error(int rc)
{
  // Signal host related (Xmodem) error
  // on selected error codes
  switch(rc) {
  case XMODEM_MEDIA_ERROR:
  case XMODEM_TIMEOUT:
  case XMODEM_END_OF_TRANSFER:
  case XMODEM_BAD_PACKET:
    HOST_ERR_LED_ON; // Signal error
    break;
  }
}

////////////////////////////////////////////////////////////////

static void signal_xmodem_activity(void)
{
  static uint8_t activity_led_on = 1;

  // Every Xmodem transfer will toggle activity LED
  if (activity_led_on) {
    ACTIV_LED_ON;
    activity_led_on = 0;
    
  }
  else {
    ACTIV_LED_OFF;
    activity_led_on = 1;
  }
}

////////////////////////////////////////////////////////////////

static int cb_xmodem_read_bin_data(XMODEM_PACKET *xpack)
{
  int rc;

  signal_xmodem_activity();

  // Read packet data from chip
  rc = at89s52_io_read_flash(g_chip_addr,
			     xpack->data,
			     XMODEM_PACKET_DATA_BYTES);
  if (rc != AT89S52_SUCCESS) {    
    TARGET_ERR_LED_ON; // Signal error
    return 1;
  }

  g_chip_addr += XMODEM_PACKET_DATA_BYTES; // Next chip address

  return 0;
}

////////////////////////////////////////////////////////////////

static int cb_xmodem_write_bin_data(const XMODEM_PACKET *xpack)
{
  int rc;

  signal_xmodem_activity();

  // Write packet data to chip
  rc = at89s52_io_write_flash(g_chip_addr,
			      xpack->data,
			      XMODEM_PACKET_DATA_BYTES);
  if (rc != AT89S52_SUCCESS) {    
    TARGET_ERR_LED_ON; // Signal error
    return 1;
  }

  g_chip_addr += XMODEM_PACKET_DATA_BYTES; // Next chip address

  return 0;
}

////////////////////////////////////////////////////////////////

static int cb_xmodem_verify_bin_data(const XMODEM_PACKET *xpack)
{
  int rc;
  int i;

  signal_xmodem_activity();

  // Read data from chip
  rc = at89s52_io_read_flash(g_chip_addr,
			     g_chip_data,
			     XMODEM_PACKET_DATA_BYTES);
  if (rc != AT89S52_SUCCESS) {        
    TARGET_ERR_LED_ON; // Signal error
    return 1;
  }

  // Compare data read from chip and data recived from XMODEM
  for (i=0; i < XMODEM_PACKET_DATA_BYTES; i++) {
    if (g_chip_data[i] != xpack->data[i]) {
      TARGET_ERR_LED_ON; // Signal error
      return 1;
    }
  }

  g_chip_addr += XMODEM_PACKET_DATA_BYTES; // Next chip address

  return 0;
}

////////////////////////////////////////////////////////////////

static int cb_xmodem_read_hex_data(XMODEM_PACKET *xpack)
{
  int rc;
  int i;
  uint8_t xmodem_bytes = XMODEM_PACKET_DATA_BYTES;
  uint8_t xmodem_data_index = 0;
  uint8_t actual_bytes;
  uint8_t *ascii_data;
  uint8_t ihex_rec_nbytes;
  uint16_t ihex_rec_addr;
  uint8_t ihex_rec_type;

  signal_xmodem_activity();

  // Read data from chip and create ASCII HEX records
  // and place them in the XMODEM packet for transmission
  while (xmodem_bytes) {
    // Get any available data from ASCII HEX record
    rc = ihex_get_ascii_record(&g_ihex,
			       xmodem_bytes,
			       &actual_bytes,
			       &ascii_data);
    if (actual_bytes) {
      // Append ASCII data to packet
      for (i=0; i < actual_bytes; i++) {
	xpack->data[xmodem_data_index++] = ascii_data[i];
      }
      xmodem_bytes -= actual_bytes;
    }
    else {
      if (g_chip_addr < AT89S52_FLASH_MEMORY_SIZE) {
	// Read binary data from chip
	rc = at89s52_io_read_flash(g_chip_addr,
				   g_chip_data,
				   IHEX_RECORD_MAX_BYTES);
	if (rc != AT89S52_SUCCESS) {    
	  TARGET_ERR_LED_ON; // Signal error
	  return 1;
	}	
	ihex_rec_nbytes = IHEX_RECORD_MAX_BYTES;
	ihex_rec_addr = g_chip_addr;
	ihex_rec_type = IHEX_REC_DATA;

	g_chip_addr += IHEX_RECORD_MAX_BYTES; // Next chip address
      }
      else {
	// All data has been read from chip
	ihex_rec_nbytes = 0;
	ihex_rec_addr = 0;
	ihex_rec_type = IHEX_REC_EOF;
      }

      // Create one ASCII HEX record
      ihex_bin_to_ascii_record(&g_ihex,
			       ihex_rec_addr,
			       ihex_rec_type,
			       g_chip_data,
			       ihex_rec_nbytes);      
    }
  }

  return 0;
}

////////////////////////////////////////////////////////////////

static int cb_xmodem_write_hex_data(const XMODEM_PACKET *xpack)
{
  int rc;
  int i;
  IHEX_RECORD *ihex_rec;

  signal_xmodem_activity();

  // Extract HEX records from packet and
  // write data in each HEX record to chip
  for (i=0; i < XMODEM_PACKET_DATA_BYTES; i++) {    
    rc = ihex_ascii_to_bin_record(&g_ihex, xpack->data[i]);
    if ( (rc != IHEX_SUCCESS) &&
	 (rc != IHEX_RECORD_READY) ) {
      HOST_ERR_LED_ON; // Signal error
      return 1;
    }
    if (rc == IHEX_RECORD_READY) {  // We have a full HEX record
      ihex_get_bin_record(&g_ihex, &ihex_rec);
      if (ihex_rec->type == IHEX_REC_DATA) {
	rc = at89s52_io_write_flash(ihex_rec->addr,
				    ihex_rec->data,
				    ihex_rec->nbytes);
	if (rc != AT89S52_SUCCESS) {
	  TARGET_ERR_LED_ON; // Signal error
	  return 1;
	}
      }
    }
  }

  return 0;
}

////////////////////////////////////////////////////////////////

static int cb_xmodem_verify_hex_data(const XMODEM_PACKET *xpack)
{
  int rc;
  int i, j;
  IHEX_RECORD *ihex_rec;

  signal_xmodem_activity();

  // Extract HEX records from packet and compare
  // data in each HEX record with data read from chip
  for (i=0; i < XMODEM_PACKET_DATA_BYTES; i++) {    
    rc = ihex_ascii_to_bin_record(&g_ihex, xpack->data[i]);
    if ( (rc != IHEX_SUCCESS) &&
	 (rc != IHEX_RECORD_READY) ) {
      HOST_ERR_LED_ON; // Signal error
      return 1;
    }
    if (rc == IHEX_RECORD_READY) {  // We have a full HEX record      
      ihex_get_bin_record(&g_ihex, &ihex_rec);
      if (ihex_rec->type == IHEX_REC_DATA) {
	// Read data from chip
	rc = at89s52_io_read_flash(ihex_rec->addr,
				   g_chip_data,
				   ihex_rec->nbytes);
	if (rc != AT89S52_SUCCESS) {
	  TARGET_ERR_LED_ON; // Signal error
	  return 1;
	}
	// Compare data read from chip and data recived from XMODEM
	for (j=0; j < ihex_rec->nbytes; j++) {
	  if (g_chip_data[j] != ihex_rec->data[j]) {
	    TARGET_ERR_LED_ON; // Signal error
	    return 1;
	  }
	}
      }
    }
  }

  return 0;
}
