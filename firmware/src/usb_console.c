/*
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * usb_console.c
 * Copyright (C) 2015 Simon Newton
 */

#include "usb_console.h"

#include <stdbool.h>
#include <stdint.h>

#include "receiver_counters.h"
#include "syslog.h"
#include "system_definitions.h"
#include "transceiver.h"
#include "uid_store.h"

#define USB_CONSOLE_BUFFER_SIZE 1024
// #define USB_CONSOLE_BUFFER_SIZE 70


// USB Device CDC Read Buffer Size. This should be a multiple of the CDC
// Bulk Endpoint size
#define USB_CONSOLE_READ_BUFFER_SIZE 64

// USB Device CDC Write Buffer Size.
#define USB_CONSOLE_WRITE_BUFFER_SIZE 64

// This needs to be a \r\n otherwise it doesn't display correctly in minicom on
// Linux.
static const char LOG_TERMINATOR[] = "\r\n";
static const int LOG_TERMINATOR_SIZE = sizeof(LOG_TERMINATOR) - 1;

typedef enum {
  READ_STATE_WAIT_FOR_CONFIGURATION,
  READ_STATE_WAIT_FOR_CARRIER,
  READ_STATE_SCHEDULE_READ,
  READ_STATE_WAIT_FOR_READ_COMPLETE,
  READ_STATE_READ_COMPLETE,
  READ_STATE_ERROR
} ReadState;

typedef enum {
  WRITE_STATE_WAIT_FOR_CONFIGURATION,
  WRITE_STATE_WAIT_FOR_CARRIER,
  WRITE_STATE_WAIT_FOR_DATA,
  WRITE_STATE_WAIT_FOR_WRITE_COMPLETE,
  WRITE_STATE_WRITE_COMPLETE,
} WriteState;

typedef struct {
  // The next index to read from. Range -1 to USB_CONSOLE_BUFFER_SIZE -1
  // -1 means the buffer is empty.
  int16_t read;
  // The next index to write to. Range 0 to USB_CONSOLE_BUFFER_SIZE - 1
  int16_t write;
  char buffer[USB_CONSOLE_BUFFER_SIZE];
} CircularBuffer;

typedef struct {
  // Set Line Coding Data
  USB_CDC_LINE_CODING set_line_coding;
  // Get Line Coding Data
  USB_CDC_LINE_CODING line_coding;
  // Control Line State
  USB_CDC_CONTROL_LINE_STATE control_line_state;

  // CDC Read
  ReadState read_state;
  USB_DEVICE_CDC_TRANSFER_HANDLE read_handle;
  char readBuffer[USB_CONSOLE_READ_BUFFER_SIZE];
  // The amount of data read.
  int read_length;

  // CDC Write
  WriteState write_state;
  USB_DEVICE_CDC_TRANSFER_HANDLE write_handle;
  CircularBuffer write;
  // The size of the last CDC write.
  unsigned int write_size;
} USBConsoleData;

USBConsoleData g_usb_console;

static uint16_t SpaceRemaining() {
  int16_t remaining = USB_CONSOLE_BUFFER_SIZE;
  if (g_usb_console.write.read != -1) {
    if (g_usb_console.write.read < g_usb_console.write.write) {
      remaining -= (g_usb_console.write.write - g_usb_console.write.read);
    } else {
      remaining -= (g_usb_console.write.write + g_usb_console.write.read);
    }
  }
  return remaining;
}

static void AbortTransfers() {
  // TODO(simon): Fix this. There seems to be some internal state that isn't
  // reset correctly. Re-enumerating the USB works but cancelling the IRPs
  // doesn't.
  USB_DEVICE_IRPCancelAll(USBTransport_GetHandle(), 0x03);
  USB_DEVICE_IRPCancelAll(USBTransport_GetHandle(), 0x83);
  g_usb_console.write.read = -1;
  g_usb_console.write.write = 0;
}

static void DisplayUID() {
  char uid[UID_LENGTH * 2 + 2];
  uid[UID_LENGTH * 2 + 1] = 0;
  UIDStore_AsAsciiString(uid);
  SysLog_Message(SYSLOG_INFO, uid);
}

/*
 * @brief This is called by the Harmony CDC module when CDC events occur.
 */
USB_DEVICE_CDC_EVENT_RESPONSE USBConsole_CDCEventHandler(
    USB_DEVICE_CDC_INDEX index,
    USB_DEVICE_CDC_EVENT event,
    void* event_data,
    uintptr_t unused_user_data) {
  if (index != USB_DEVICE_CDC_INDEX_0) {
    return USB_DEVICE_CDC_EVENT_RESPONSE_NONE;
  }

  USB_CDC_CONTROL_LINE_STATE* line_state;
  switch (event) {
    case USB_DEVICE_CDC_EVENT_GET_LINE_CODING:
      // The host wants to know the current line coding. This is a control
      // transfer request.
      USB_DEVICE_ControlSend(USBTransport_GetHandle(),
                             &g_usb_console.line_coding,
                             sizeof(USB_CDC_LINE_CODING));
      break;

    case USB_DEVICE_CDC_EVENT_SET_LINE_CODING:
      // The host wants to set the line coding. This is a control transfer.
      USB_DEVICE_ControlReceive(USBTransport_GetHandle(),
                                &g_usb_console.set_line_coding,
                                sizeof(USB_CDC_LINE_CODING));
      break;

    case USB_DEVICE_CDC_EVENT_SET_CONTROL_LINE_STATE:
      // This means the host is setting the control line state.
      line_state = (USB_CDC_CONTROL_LINE_STATE *) event_data;
      g_usb_console.control_line_state.dtr = line_state->dtr;
      if (g_usb_console.control_line_state.carrier != line_state->carrier) {
        // The carrier state changed.
        if (line_state->carrier) {
          // Host connect
          g_usb_console.write_state = WRITE_STATE_WAIT_FOR_DATA;
          g_usb_console.read_state = READ_STATE_SCHEDULE_READ;
        } else {
          // Host disconnect
          g_usb_console.write_state = WRITE_STATE_WAIT_FOR_CARRIER;
          g_usb_console.read_state = READ_STATE_WAIT_FOR_CARRIER;
        }
        g_usb_console.control_line_state.carrier = line_state->carrier;
      }
      USB_DEVICE_ControlStatus(USBTransport_GetHandle(),
                               USB_DEVICE_CONTROL_STATUS_OK);
      break;

    case USB_DEVICE_CDC_EVENT_SEND_BREAK:
      // Noop
      break;

    case USB_DEVICE_CDC_EVENT_READ_COMPLETE:
      g_usb_console.read_state = READ_STATE_READ_COMPLETE;
      g_usb_console.read_length =
          ((USB_DEVICE_CDC_EVENT_DATA_READ_COMPLETE *) event_data)->length;
      g_usb_console.read_handle = USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID;
      break;

    case USB_DEVICE_CDC_EVENT_CONTROL_TRANSFER_DATA_RECEIVED:
      // The data stage of the last control transfer is complete. For now we
      // accept all the data.
      USB_DEVICE_ControlStatus(USBTransport_GetHandle(),
                               USB_DEVICE_CONTROL_STATUS_OK);
      break;

    case USB_DEVICE_CDC_EVENT_CONTROL_TRANSFER_DATA_SENT:
      // This means the GET LINE CODING function data is valid.
      break;

    case USB_DEVICE_CDC_EVENT_WRITE_COMPLETE:
      g_usb_console.write_state = WRITE_STATE_WRITE_COMPLETE;
      g_usb_console.write_handle = USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID;
      break;

    case USB_DEVICE_CDC_EVENT_CONTROL_TRANSFER_ABORTED:
      break;
    default:
      break;
  }
  return USB_DEVICE_CDC_EVENT_RESPONSE_NONE;
}

/*
 * @brief Check if the device was reset.
 * @returns true if the USB device was reset, false otherwise.
 */
static bool CheckAndHandleReset() {
  if (USBTransport_IsConfigured() == false) {
    g_usb_console.read_state = READ_STATE_WAIT_FOR_CONFIGURATION;
    g_usb_console.read_handle = USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID;
    g_usb_console.write_handle = USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID;
    g_usb_console.write_state = WRITE_STATE_WAIT_FOR_CONFIGURATION;
    return true;
  }
  return false;
}

/*
 * @brief Log raw data to the console.
 * @pre str is NULL terminated and has at least one non-NULL character.
 * @pre There is at least 1 byte of space in the buffer.
 */
void LogRaw(const char* str) {
  const char* c = str;
  for (; *c != 0; c++) {
    if (g_usb_console.write.write == g_usb_console.write.read && c != str) {
      return;
    }

    g_usb_console.write.buffer[g_usb_console.write.write] = *c;
    g_usb_console.write.write++;
    if (g_usb_console.write.write == USB_CONSOLE_BUFFER_SIZE) {
      g_usb_console.write.write = 0;
    }
  }
}

// Public Functions
// ----------------------------------------------------------------------------
void USBConsole_Initialize() {
  // Dummy line coding params.
  g_usb_console.line_coding.dwDTERate = 9600;
  g_usb_console.line_coding.bParityType = 0;
  g_usb_console.line_coding.bParityType = 0;
  g_usb_console.line_coding.bDataBits = 8;
  g_usb_console.control_line_state.carrier = 0;

  g_usb_console.read_state = READ_STATE_WAIT_FOR_CONFIGURATION;
  g_usb_console.read_handle = USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID;
  g_usb_console.read_length = 0;

  g_usb_console.write_state = WRITE_STATE_WAIT_FOR_CONFIGURATION;
  g_usb_console.write_handle = USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID;
  g_usb_console.write.read = -1;
  g_usb_console.write.write = 0;

  USB_DEVICE_CDC_EventHandlerSet(USB_DEVICE_CDC_INDEX_0,
                                 USBConsole_CDCEventHandler, NULL);
}

void USBConsole_Log(const char* message) {
  if (g_usb_console.control_line_state.carrier == 0 || *message == 0) {
    return;
  }

  int16_t remaining = SpaceRemaining();
  if (remaining < LOG_TERMINATOR_SIZE) {
    // There isn't enough room for the terminator characters.
    return;
  }

  if (g_usb_console.write.read < 0) {
    // If the buffer is empty, set the read index, otherwise don't change it.
    g_usb_console.write.read = 0;
    g_usb_console.write.write = 0;
  }

  LogRaw(message);

  // We need to terminate with \r\n
  remaining = SpaceRemaining();
  if (remaining < LOG_TERMINATOR_SIZE) {
    g_usb_console.write.write -= LOG_TERMINATOR_SIZE;
    if (g_usb_console.write.write < 0) {
      g_usb_console.write.write += USB_CONSOLE_BUFFER_SIZE;
    }
  }
  LogRaw(LOG_TERMINATOR);
  return;
}

void USBConsole_Tasks() {
  if (CheckAndHandleReset()) {
    return;
  }

  // Writer state machine
  switch (g_usb_console.write_state) {
    case WRITE_STATE_WAIT_FOR_CONFIGURATION:
      if (!USBTransport_IsConfigured()) {
        break;
      }
      g_usb_console.write_state = WRITE_STATE_WAIT_FOR_CARRIER;
      break;
    case WRITE_STATE_WAIT_FOR_CARRIER:
      // Noop
      break;
    case WRITE_STATE_WAIT_FOR_DATA:
      if (g_usb_console.write.read != -1) {
        g_usb_console.write_handle = USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID;
        if (g_usb_console.write.read < g_usb_console.write.write) {
          g_usb_console.write_size =
            g_usb_console.write.write - g_usb_console.write.read;
        } else {
          // Buffer has wrapped
          g_usb_console.write_size =
            USB_CONSOLE_BUFFER_SIZE - g_usb_console.write.read;
        }
        if (g_usb_console.write_size > USB_CONSOLE_WRITE_BUFFER_SIZE) {
          g_usb_console.write_size = USB_CONSOLE_WRITE_BUFFER_SIZE;
        }
        USB_DEVICE_CDC_RESULT res = USB_DEVICE_CDC_Write(
            USB_DEVICE_CDC_INDEX_0,
            &g_usb_console.write_handle,
            g_usb_console.write.buffer + g_usb_console.write.read,
            g_usb_console.write_size,
            USB_DEVICE_CDC_TRANSFER_FLAGS_DATA_COMPLETE);
        // If there was an error, try again later.
        if (res == USB_DEVICE_CDC_RESULT_OK) {
          g_usb_console.write_state = WRITE_STATE_WAIT_FOR_WRITE_COMPLETE;
        }
      }
      break;
    case WRITE_STATE_WAIT_FOR_WRITE_COMPLETE:
      // Noop
      break;
    case WRITE_STATE_WRITE_COMPLETE:
      g_usb_console.write.read += g_usb_console.write_size;
      if (g_usb_console.write.read >= USB_CONSOLE_BUFFER_SIZE) {
        g_usb_console.write.read = 0;
      }
      if (g_usb_console.write.read == g_usb_console.write.write) {
        g_usb_console.write.write = 0;
        g_usb_console.write.read = -1;
      }
      g_usb_console.write_state = WRITE_STATE_WAIT_FOR_DATA;
      break;
  }

  // Reader state machine
  switch (g_usb_console.read_state) {
    case READ_STATE_WAIT_FOR_CONFIGURATION:
      if (!USBTransport_IsConfigured()) {
        break;
      }
      g_usb_console.read_state = READ_STATE_WAIT_FOR_CARRIER;
      break;
    case READ_STATE_WAIT_FOR_CARRIER:
      // Noop
      break;
    case READ_STATE_SCHEDULE_READ:
      g_usb_console.read_state = READ_STATE_WAIT_FOR_READ_COMPLETE;
      g_usb_console.read_handle = USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID;
      USB_DEVICE_CDC_Read(USB_DEVICE_CDC_INDEX_0,
                          &g_usb_console.read_handle,
                          g_usb_console.readBuffer,
                          USB_CONSOLE_READ_BUFFER_SIZE);

      if (g_usb_console.read_handle == USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID) {
        g_usb_console.read_state = READ_STATE_ERROR;
        break;
      }
      break;
    case READ_STATE_WAIT_FOR_READ_COMPLETE:
      // Noop
      break;
    case READ_STATE_READ_COMPLETE:
      switch (g_usb_console.readBuffer[0]) {
        case '+':
          SysLog_Increment();
          SysLog_Print(SYSLOG_ALWAYS, "Log level: %s",
                       SysLog_LevelToString(SysLog_GetLevel()));
          break;
        case '-':
          SysLog_Decrement();
          SysLog_Print(SYSLOG_ALWAYS, "Log level: %s",
                       SysLog_LevelToString(SysLog_GetLevel()));
          break;
        case 'c':
          SysLog_Print(SYSLOG_INFO, "DMX Frames %d",
                       ReceiverCounters_DMXFrames());
          SysLog_Print(SYSLOG_INFO, "RDM Frames %d",
                       ReceiverCounters_RDMFrames());
          break;
        case 'd':
          SysLog_Message(SYSLOG_DEBUG, "debug");
          break;
        case 'e':
          SysLog_Message(SYSLOG_ERROR, "error");
          break;
        case 'f':
          SysLog_Message(SYSLOG_FATAL, "fatal");
          break;
        case 'h':
          SysLog_Message(SYSLOG_INFO, "----------------------");
          SysLog_Message(SYSLOG_INFO, "c   Dump Counters");
          SysLog_Message(SYSLOG_INFO, "r   Reset");
          SysLog_Message(SYSLOG_INFO, "t   Transceiver Settings");
          SysLog_Message(SYSLOG_INFO, "h   Show help message");
          SysLog_Message(SYSLOG_INFO, "m   Get operating mode");
          SysLog_Message(SYSLOG_INFO, "M   Switch operating mode");
          SysLog_Message(SYSLOG_INFO, "u   Show UID");
          SysLog_Message(SYSLOG_INFO, "-   Decrease Log Level");
          SysLog_Message(SYSLOG_INFO, "+   Increase Log Level");
          SysLog_Message(SYSLOG_INFO, "----------------------");
          break;
        case 'i':
          SysLog_Message(SYSLOG_INFO, "info");
          break;
        case 'm':
          SysLog_Print(SYSLOG_INFO, "%s Mode",
                       Transceiver_GetMode() == T_MODE_CONTROLLER ?
                       "Controller" : "Responder");
          break;
        case 'M':
          Transceiver_SetMode(Transceiver_GetMode() == T_MODE_CONTROLLER ?
                              T_MODE_RESPONDER : T_MODE_CONTROLLER,
                              TRANSCEIVER_NO_NOTIFICATION);
          break;
        case 'r':
          APP_Reset();
          break;
        case 't':
          SysLog_Print(SYSLOG_INFO, "Break: %dus", Transceiver_GetBreakTime());
          SysLog_Print(SYSLOG_INFO, "Mark: %dus", Transceiver_GetMarkTime());
          SysLog_Print(SYSLOG_INFO, "RDM Bcast timeout: %d / 10 us",
                       Transceiver_GetRDMBroadcastTimeout());
          SysLog_Print(SYSLOG_INFO, "RDM timeout: %d / 10 us",
                       Transceiver_GetRDMResponseTimeout());
          SysLog_Print(SYSLOG_INFO, "RDM responder delay: %d / 10 us",
                       Transceiver_GetRDMResponderDelay());
          SysLog_Print(SYSLOG_INFO, "RDM responder jitter: %d / 10 us",
                       Transceiver_GetRDMResponderJitter());
          break;
        case 'w':
          SysLog_Message(SYSLOG_WARN, "warning");
          break;
        case 'u':
          DisplayUID();
          break;
        default:
          if (g_usb_console.read_length == USB_CONSOLE_READ_BUFFER_SIZE) {
            g_usb_console.readBuffer[USB_CONSOLE_READ_BUFFER_SIZE - 1] = 0;
          } else {
            g_usb_console.readBuffer[g_usb_console.read_length] = 0;
          }
          USBConsole_Log(g_usb_console.readBuffer);
      }
      g_usb_console.read_state = READ_STATE_SCHEDULE_READ;
      break;
    case READ_STATE_ERROR:
      break;
  }
}
