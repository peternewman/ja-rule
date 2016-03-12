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
 * rdm_responder.c
 * Copyright (C) 2015 Simon Newton
 */
#include "rdm_responder.h"

#include <string.h>

#include "coarse_timer.h"
#include "constants.h"
#include "macros.h"
#include "rdm_buffer.h"
#include "rdm_util.h"
#include "receiver_counters.h"
#include "utils.h"

const char MANUFACTURER_LABEL[] = "Open Lighting Project";
// TODO(simon): pull this from the bootloader at some point
const char BOOT_SOFTWARE_LABEL[] = "0.0.1";
static const uint32_t BOOT_SOFTWARE_VERSION = 0x00000001;

static const uint8_t FIVE5_CONSTANT = 0x55u;
static const uint8_t AA_CONSTANT = 0xaau;
static const uint8_t FE_CONSTANT = 0xfeu;
static const uint8_t SENSOR_VALUE_PARAM_DATA_LENGTH = 9u;
static const uint16_t FLASH_FAST = 1000u;
static const uint16_t FLASH_SLOW = 10000u;

// Microchip defines this macro in stdlib.h but it's non standard.
// We define it here so that the unit tests work.
#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif

/*
 * @brief A helper to return if the RDM Request does not require an RDM
 *   response.
 * @param header The RDMHeader of the request.
 */
#define ReturnUnlessUnicast(header) \
if (!RDMUtil_IsUnicast(header->dest_uid)) {\
  return RDM_RESPONDER_NO_RESPONSE; \
}

static RDMResponder root_responder;

RDMResponder *g_responder = &root_responder;

/*
 * @brief The responder state.
 */
typedef struct {
  // Mute params
  CoarseTimer_Value mute_timer;
  PORTS_CHANNEL mute_port;
  PORTS_BIT_POS mute_bit;

  // Identify params
  CoarseTimer_Value identify_timer;
  PORTS_CHANNEL identify_port;
  PORTS_BIT_POS identify_bit;
} InternalResponderState;

static InternalResponderState g_internal_state;

// Helper functions
// ----------------------------------------------------------------------------

/*
 * @brief Get the current personality.
 * @returns The current personality definition, or NULL if there isn't one.
 */
static inline const PersonalityDefinition* CurrentPersonality() {
  if (g_responder->def->personalities) {
    return &g_responder->def->personalities[
        g_responder->current_personality - 1];
  }
  return NULL;
}

/*
 * @brief Record the sensor at the specified index.
 */
static inline void RecordSensor(unsigned int i) {
  if (g_responder->def->sensors[i].recorded_value_support &
      SENSOR_SUPPORTS_RECORDING_MASK) {
    g_responder->sensors[i].recorded_value =
        g_responder->sensors[i].present_value;
  }
}

/*
 * @brief Reset the sensor at the specified index.
 */
static void ResetSensor(unsigned int i) {
  if (g_responder->def->sensors[i].recorded_value_support &
      SENSOR_SUPPORTS_LOWEST_HIGHEST_MASK) {
    g_responder->sensors[i].lowest_value =
        g_responder->sensors[i].present_value;
    g_responder->sensors[i].highest_value =
        g_responder->sensors[i].present_value;
  } else {
    g_responder->sensors[i].lowest_value = SENSOR_VALUE_UNSUPPORTED;
    g_responder->sensors[i].highest_value = SENSOR_VALUE_UNSUPPORTED;
  }

  if (g_responder->def->sensors[i].recorded_value_support &
      SENSOR_SUPPORTS_RECORDING_MASK) {
    g_responder->sensors[i].recorded_value =
      g_responder->sensors[i].present_value;
  } else {
    g_responder->sensors[i].recorded_value = SENSOR_VALUE_UNSUPPORTED;
  }
}

/*
 * @brief Build a SENSOR_VALUE response.
 */
static uint8_t *BuildSensorValueResponse(uint8_t *ptr, uint8_t index,
                                         const SensorData *sensor) {
  *ptr++ = index;
  ptr = PushUInt16(ptr, sensor->present_value);
  ptr = PushUInt16(ptr, sensor->lowest_value);
  ptr = PushUInt16(ptr, sensor->highest_value);
  ptr = PushUInt16(ptr, sensor->recorded_value);
  return ptr;
}

static inline uint16_t GetControlField() {
  return (g_responder->sub_device_count ? MUTE_SUBDEVICE_FLAG : 0) |
         (g_responder->is_managed_proxy ? MUTE_MANAGED_PROXY_FLAG : 0) |
         (g_responder->is_proxied_device ? MUTE_PROXY_FLAG : 0);
}

// Public Functions
// ----------------------------------------------------------------------------
void RDMResponder_Initialize(const RDMResponderSettings *settings) {
  g_internal_state.mute_timer = CoarseTimer_GetTime();
  g_internal_state.mute_port = settings->mute_port;
  g_internal_state.mute_bit = settings->mute_bit;

  g_internal_state.identify_timer = 0u;
  g_internal_state.identify_port = settings->identify_port;
  g_internal_state.identify_bit = settings->identify_bit;

  // Initialize hardware
  PLIB_PORTS_PinDirectionOutputSet(PORTS_ID_0, g_internal_state.identify_port,
                                   g_internal_state.identify_bit);
  PLIB_PORTS_PinClear(PORTS_ID_0, g_internal_state.identify_port,
                      g_internal_state.identify_bit);

  PLIB_PORTS_PinDirectionOutputSet(PORTS_ID_0, g_internal_state.mute_port,
                                   g_internal_state.mute_bit);
  PLIB_PORTS_PinSet(PORTS_ID_0, g_internal_state.mute_port,
                    g_internal_state.mute_bit);

  memcpy(g_responder->uid, settings->uid, UID_LENGTH);
  g_responder->def = NULL;
  RDMResponder_InitResponder();
}

void RDMResponder_Tasks() {
  if (g_responder->identify_on) {
    if (CoarseTimer_HasElapsed(g_internal_state.identify_timer, FLASH_FAST)) {
      g_internal_state.identify_timer = CoarseTimer_GetTime();
      PLIB_PORTS_PinToggle(PORTS_ID_0, g_internal_state.identify_port,
                           g_internal_state.identify_bit);
    }
  }

  if (!g_responder->is_muted) {
    if (CoarseTimer_HasElapsed(g_internal_state.mute_timer, FLASH_SLOW)) {
      g_internal_state.mute_timer = CoarseTimer_GetTime();
      PLIB_PORTS_PinToggle(PORTS_ID_0, g_internal_state.mute_port,
                           g_internal_state.mute_bit);
    }
  }
}

void RDMResponder_SwitchResponder(RDMResponder *responder) {
  g_responder = responder;
}

void RDMResponder_RestoreResponder() {
  g_responder = &root_responder;
}

void RDMResponder_InitResponder() {
  // This resets the non-mutable state of the responder and then calls
  // RDMResponder_ResetToFactoryDefaults() to reset the mutable state.
  g_responder->sensors = NULL;
  g_responder->is_subdevice = false;
  g_responder->is_managed_proxy = false;
  g_responder->is_proxied_device = false;

  RDMResponder_ResetToFactoryDefaults();
}

void RDMResponder_ResetToFactoryDefaults() {
  g_responder->dmx_start_address = INVALID_DMX_START_ADDRESS;
  g_responder->sub_device_count = 0u;
  g_responder->current_personality = 1u;
  g_responder->queued_message_count = 0u;

  g_responder->is_muted = false;
  g_responder->identify_on = false;

  if (g_responder->def) {
    RDMUtil_StringCopy(g_responder->device_label, RDM_DEFAULT_STRING_SIZE,
                       g_responder->def->default_device_label,
                       RDM_DEFAULT_STRING_SIZE);
    if (g_responder->def->personality_count) {
      g_responder->current_personality = 1u;
      g_responder->dmx_start_address = 1u;
    }
  }
  g_responder->using_factory_defaults = true;
}

void RDMResponder_GetUID(uint8_t *uid) {
  memcpy(uid, g_responder->uid, UID_LENGTH);
}

int RDMResponder_HandleDUBRequest(const uint8_t *param_data,
                                  unsigned int param_data_length) {
  if (g_responder->is_muted || param_data_length != 2 * UID_LENGTH) {
    return RDM_RESPONDER_NO_RESPONSE;
  }

  if (!(RDMUtil_UIDCompare(param_data, g_responder->uid) <= 0 &&
        RDMUtil_UIDCompare(g_responder->uid, param_data + UID_LENGTH) <= 0)) {
    return RDM_RESPONDER_NO_RESPONSE;
  }

  uint8_t *response = (uint8_t*) g_rdm_buffer;
  memset(response, FE_CONSTANT, 7);
  response[7] = AA_CONSTANT;
  response[8] = g_responder->uid[0] | AA_CONSTANT;
  response[9] = g_responder->uid[0] | FIVE5_CONSTANT;
  response[10] = g_responder->uid[1] | AA_CONSTANT;
  response[11] = g_responder->uid[1] | FIVE5_CONSTANT;

  response[12] = g_responder->uid[2] | AA_CONSTANT;
  response[13] = g_responder->uid[2] | FIVE5_CONSTANT;
  response[14] = g_responder->uid[3] | AA_CONSTANT;
  response[15] = g_responder->uid[3] | FIVE5_CONSTANT;
  response[16] = g_responder->uid[4] | AA_CONSTANT;
  response[17] = g_responder->uid[4] | FIVE5_CONSTANT;
  response[18] = g_responder->uid[5] | AA_CONSTANT;
  response[19] = g_responder->uid[5] | FIVE5_CONSTANT;

  uint16_t checksum = 0u;
  unsigned int i;
  for (i = 8u; i < 20u; i++) {
    checksum += response[i];
  }

  response[20] = ShortMSB(checksum) | AA_CONSTANT;
  response[21] = ShortMSB(checksum) | FIVE5_CONSTANT;
  response[22] = ShortLSB(checksum) | AA_CONSTANT;
  response[23] = ShortLSB(checksum) | FIVE5_CONSTANT;
  return -DUB_RESPONSE_LENGTH;
}

void RDMResponder_BuildHeader(const RDMHeader *incoming_header,
                              RDMResponseType response_type,
                              RDMCommandClass command_class,
                              uint16_t pid,
                              unsigned int message_length) {
  RDMHeader *outgoing_header = (RDMHeader*) g_rdm_buffer;
  outgoing_header->start_code = RDM_START_CODE;
  outgoing_header->sub_start_code = SUB_START_CODE;
  outgoing_header->message_length = message_length;
  memcpy(outgoing_header->dest_uid, incoming_header->src_uid, UID_LENGTH);
  memcpy(outgoing_header->src_uid, incoming_header->dest_uid, UID_LENGTH);
  outgoing_header->transaction_number = incoming_header->transaction_number;
  outgoing_header->port_id = response_type;
  outgoing_header->message_count = g_responder->queued_message_count;
  outgoing_header->sub_device = incoming_header->sub_device;
  outgoing_header->command_class = command_class;
  outgoing_header->param_id = htons(pid);
  outgoing_header->param_data_length = message_length - sizeof(RDMHeader);
}

int RDMResponder_AddHeaderAndChecksum(const RDMHeader *header,
                                      RDMResponseType response_type,
                                      unsigned int message_length) {
  uint8_t response_command_class = 0u;
  switch (header->command_class) {
    case DISCOVERY_COMMAND:
      response_command_class = DISCOVERY_COMMAND_RESPONSE;
      break;
    case GET_COMMAND:
      response_command_class = GET_COMMAND_RESPONSE;
      break;
    case SET_COMMAND:
      response_command_class = SET_COMMAND_RESPONSE;
      break;
    default:
      return RDM_RESPONDER_NO_RESPONSE;
  }

  uint8_t *ptr = g_rdm_buffer;
  *ptr++ = RDM_START_CODE;
  *ptr++ = SUB_START_CODE;
  *ptr++ = message_length;
  memcpy(ptr, header->src_uid, UID_LENGTH);
  ptr += UID_LENGTH;
  memcpy(ptr, header->dest_uid, UID_LENGTH);
  ptr += UID_LENGTH;
  *ptr++ = header->transaction_number;
  *ptr++ = response_type;
  *ptr++ = g_responder->queued_message_count;
  ptr = PushUInt16(ptr, ntohs(header->sub_device));
  *ptr++ = response_command_class;
  ptr = PushUInt16(ptr, ntohs(header->param_id));
  *ptr++ = message_length - sizeof(RDMHeader);
  return RDMUtil_AppendChecksum(g_rdm_buffer);
}

int RDMResponder_BuildSetAck(const RDMHeader *header) {
  ReturnUnlessUnicast(header);
  return RDMResponder_AddHeaderAndChecksum(header, ACK, sizeof(RDMHeader));
}

int RDMResponder_BuildNack(const RDMHeader *header, RDMNackReason reason) {
  ReturnUnlessUnicast(header);

  uint8_t *ptr = g_rdm_buffer + sizeof(RDMHeader);
  ptr = PushUInt16(ptr, reason);
  return RDMResponder_AddHeaderAndChecksum(header, NACK_REASON,
                                           ptr - g_rdm_buffer);
}

int RDMResponder_BuildAckTimer(const RDMHeader *header, uint16_t delay) {
  uint8_t *ptr = g_rdm_buffer + sizeof(RDMHeader);
  ptr = PushUInt16(ptr, delay);
  return RDMResponder_AddHeaderAndChecksum(header, ACK_TIMER,
                                           ptr - g_rdm_buffer);
}

int RDMResponder_BuildParamDescription(
    const RDMHeader *header,
    uint16_t param_id,
    const ParameterDescription *description) {
  uint8_t *ptr = g_rdm_buffer + sizeof(RDMHeader);
  ptr = PushUInt16(ptr, param_id);
  *ptr++ = description->pdl_size;
  *ptr++ = description->data_type;
  *ptr++ = description->command_class;
  *ptr++ = 0u;  // type is always 0.
  *ptr++ = description->unit;
  *ptr++ = description->prefix;
  ptr = PushUInt32(ptr, description->min_valid_value);
  ptr = PushUInt32(ptr, description->max_valid_value);
  ptr = PushUInt32(ptr, description->default_value);
  ptr += RDMUtil_StringCopy((char*) ptr, RDM_DEFAULT_STRING_SIZE,
                            description->description, RDM_DEFAULT_STRING_SIZE);
  return RDMResponder_AddHeaderAndChecksum(header, ACK, ptr - g_rdm_buffer);
}

int RDMResponder_DispatchPID(const RDMHeader *header,
                             const uint8_t *param_data) {
  const ResponderDefinition *definition = g_responder->def;
  uint16_t pid = ntohs(header->param_id);
  unsigned int i = 0u;
  // TODO(simon): convert to binary search if the list gets long.
  // We'll need to add a check to ensure it's sorted though.
  for (; i < definition->descriptor_count; i++) {
    if (pid == definition->descriptors[i].pid) {
      if (header->command_class == GET_COMMAND) {
        if (RDMUtil_IsUnicast(header->dest_uid)) {\
          if (definition->descriptors[i].get_handler) {
            if (header->param_data_length ==
                definition->descriptors[i].get_param_size) {
              return definition->descriptors[i].get_handler(header, param_data);
            } else {
              return RDMResponder_BuildNack(header, NR_FORMAT_ERROR);
            }
          } else {
            return RDMResponder_BuildNack(header, NR_UNSUPPORTED_COMMAND_CLASS);
          }
        } else {
          return RDM_RESPONDER_NO_RESPONSE;
        }
      } else {
        if (definition->descriptors[i].set_handler) {
          return definition->descriptors[i].set_handler(header, param_data);
        } else {
          return RDMResponder_BuildNack(header, NR_UNSUPPORTED_COMMAND_CLASS);
        }
      }
    }
  }
  return RDMResponder_BuildNack(header, NR_UNKNOWN_PID);
}

int RDMResponder_Ioctl(ModelIoctl command, uint8_t *data, unsigned int length) {
  switch (command) {
    case IOCTL_GET_UID:
      if (length != UID_LENGTH) {
        return 0;
      }
      RDMResponder_GetUID(data);
      return 1;
    default:
      return 0;
  }
}

// PID Handlers
// ----------------------------------------------------------------------------
int RDMResponder_GenericReturnString(const RDMHeader *header,
                                     const char *reply_string,
                                     unsigned int max_size) {
  unsigned int msg_length = sizeof(RDMHeader);
  msg_length += RDMUtil_StringCopy((char*)(g_rdm_buffer + sizeof(RDMHeader)),
                                   max_size, reply_string, max_size);
  return RDMResponder_AddHeaderAndChecksum(header, ACK, msg_length);
}

int RDMResponder_GenericGetBool(const RDMHeader *header, bool value) {
  g_rdm_buffer[sizeof(RDMHeader)] = value;
  return RDMResponder_AddHeaderAndChecksum(
      header, ACK, sizeof(RDMHeader) + sizeof(uint8_t));
}

int RDMResponder_GenericSetBool(const RDMHeader *header,
                                const uint8_t *param_data,
                                bool *value) {
  if (header->param_data_length != sizeof(uint8_t)) {
    return RDMResponder_BuildNack(header, NR_FORMAT_ERROR);
  }
  switch (param_data[0]) {
    case 0:
      *value = false;
      break;
    case 1:
      *value = true;
      break;
    default:
      return RDMResponder_BuildNack(header, NR_DATA_OUT_OF_RANGE);
  }
  return RDMResponder_BuildSetAck(header);
}

int RDMResponder_GenericGetUInt8(const RDMHeader *header, uint8_t value) {
  g_rdm_buffer[sizeof(RDMHeader)] = value;
  return RDMResponder_AddHeaderAndChecksum(
      header, ACK, sizeof(RDMHeader) + sizeof(uint8_t));
}

int RDMResponder_GenericSetUInt8(const RDMHeader *header,
                                 const uint8_t *param_data,
                                 uint8_t *value) {
  if (header->param_data_length != sizeof(uint8_t)) {
    return RDMResponder_BuildNack(header, NR_FORMAT_ERROR);
  }
  *value = param_data[0];
  return RDMResponder_BuildSetAck(header);
}

int RDMResponder_GenericGetUInt16(const RDMHeader *header, uint16_t value) {
  uint8_t *ptr = g_rdm_buffer + sizeof(RDMHeader);
  ptr = PushUInt16(ptr, value);
  return RDMResponder_AddHeaderAndChecksum(header, ACK, ptr - g_rdm_buffer);
}

int RDMResponder_GenericSetUInt16(const RDMHeader *header,
                                  const uint8_t *param_data,
                                  uint16_t *value) {
  if (header->param_data_length != sizeof(uint16_t)) {
    return RDMResponder_BuildNack(header, NR_FORMAT_ERROR);
  }
  *value = ExtractUInt16(param_data);
  return RDMResponder_BuildSetAck(header);
}

int RDMResponder_GenericGetUInt32(const RDMHeader *header, uint32_t value) {
  uint8_t *ptr = g_rdm_buffer + sizeof(RDMHeader);
  ptr = PushUInt32(ptr, value);
  return RDMResponder_AddHeaderAndChecksum(header, ACK, ptr - g_rdm_buffer);
}

int RDMResponder_GenericSetUInt32(const RDMHeader *header,
                                  const uint8_t *param_data,
                                  uint32_t *value) {
  if (header->param_data_length != sizeof(uint32_t)) {
    return RDMResponder_BuildNack(header, NR_FORMAT_ERROR);
  }
  *value = ExtractUInt32(param_data);
  return RDMResponder_BuildSetAck(header);
}

int RDMResponder_SetMute(const RDMHeader *header) {
  if (header->param_data_length) {
    return RDM_RESPONDER_NO_RESPONSE;
  }
  g_responder->is_muted = true;
  PLIB_PORTS_PinClear(PORTS_ID_0, g_internal_state.mute_port,
                      g_internal_state.mute_bit);

  ReturnUnlessUnicast(header);

  uint8_t *ptr = g_rdm_buffer + sizeof(RDMHeader);
  ptr = PushUInt16(ptr, GetControlField());
  return RDMResponder_AddHeaderAndChecksum(header, ACK, ptr - g_rdm_buffer);
}

// TODO(simon): combine with RDMResponder_SetMute above.
int RDMResponder_SetUnMute(const RDMHeader *header) {
  if (header->param_data_length) {
    return RDM_RESPONDER_NO_RESPONSE;
  }
  g_responder->is_muted = false;
  PLIB_PORTS_PinSet(PORTS_ID_0, g_internal_state.mute_port,
                    g_internal_state.mute_bit);
  g_internal_state.mute_timer = CoarseTimer_GetTime();

  ReturnUnlessUnicast(header);

  uint8_t *ptr = g_rdm_buffer + sizeof(RDMHeader);
  ptr = PushUInt16(ptr, GetControlField());
  return RDMResponder_AddHeaderAndChecksum(header, ACK, ptr - g_rdm_buffer);
}

int RDMResponder_GetSupportedParameters(const RDMHeader *header,
                                        UNUSED const uint8_t *param_data) {
  const ResponderDefinition *definition = g_responder->def;

  // TODO(simon): handle ack-overflow here
  unsigned int i = 0u;
  uint8_t *ptr = g_rdm_buffer + sizeof(RDMHeader);
  for (; i < definition->descriptor_count; i++) {
    switch (definition->descriptors[i].pid) {
      case PID_DISC_UNIQUE_BRANCH:
      case PID_DISC_MUTE:
      case PID_DISC_UN_MUTE:
      case PID_SUPPORTED_PARAMETERS:
      case PID_PARAMETER_DESCRIPTION:
      case PID_DEVICE_INFO:
      case PID_SOFTWARE_VERSION_LABEL:
      case PID_DMX_START_ADDRESS:
      case PID_IDENTIFY_DEVICE:
        if (g_responder->is_subdevice) {
          ptr = PushUInt16(ptr, definition->descriptors[i].pid);
        }
        break;
      default:
        ptr = PushUInt16(ptr, definition->descriptors[i].pid);
    }
  }

  return RDMResponder_AddHeaderAndChecksum(header, ACK, ptr - g_rdm_buffer);
}

int RDMResponder_GetCommsStatus(const RDMHeader *header,
                                UNUSED const uint8_t *param_data) {
  uint8_t *ptr = g_rdm_buffer + sizeof(RDMHeader);

  ptr = PushUInt16(ptr, ReceiverCounters_RDMShortFrame());
  ptr = PushUInt16(ptr, ReceiverCounters_RDMLengthMismatch());
  ptr = PushUInt16(ptr, ReceiverCounters_RDMChecksumInvalidCounter());

  return RDMResponder_AddHeaderAndChecksum(header, ACK, ptr - g_rdm_buffer);
}

int RDMResponder_SetCommsStatus(const RDMHeader *header,
                                UNUSED const uint8_t *param_data) {
  if (header->param_data_length != 0u) {
    return RDMResponder_BuildNack(header, NR_FORMAT_ERROR);
  }
  ReceiverCounters_ResetCommsStatusCounters();
  return RDMResponder_BuildSetAck(header);
}

int RDMResponder_GetDeviceInfo(const RDMHeader *header,
                               UNUSED const uint8_t *param_data) {
  const PersonalityDefinition *personality = CurrentPersonality();

  uint8_t *ptr = g_rdm_buffer + sizeof(RDMHeader);
  ptr = PushUInt16(ptr, RDM_VERSION);
  ptr = PushUInt16(ptr, g_responder->def->model_id);
  ptr = PushUInt16(ptr, g_responder->def->product_category);
  ptr = PushUInt32(ptr, g_responder->def->software_version);
  ptr = PushUInt16(ptr, personality ? personality->dmx_footprint : 0u);
  *ptr++ = g_responder->current_personality;

  if (g_responder->def->personalities) {
    *ptr++ = g_responder->def->personality_count;
  } else {
    *ptr++ = 1u;
  }
  ptr = PushUInt16(ptr, g_responder->dmx_start_address);
  ptr = PushUInt16(ptr, g_responder->sub_device_count);
  *ptr++ = g_responder->def->sensor_count;

  return RDMResponder_AddHeaderAndChecksum(header, ACK, ptr - g_rdm_buffer);
}

int RDMResponder_GetProductDetailIds(const RDMHeader *header,
                                     UNUSED const uint8_t *param_data) {
  const ResponderDefinition *definition = g_responder->def;
  uint8_t *ptr = g_rdm_buffer + sizeof(RDMHeader);
  if (definition->product_detail_ids) {
    unsigned int i = 0;
    unsigned int count = min(definition->product_detail_ids->size,
                             MAX_PRODUCT_DETAILS);
    for (; i < count; i++) {
      ptr = PushUInt16(ptr, definition->product_detail_ids->ids[i]);
    }
  }

  return RDMResponder_AddHeaderAndChecksum(header, ACK, ptr - g_rdm_buffer);
}

int RDMResponder_GetDeviceModelDescription(const RDMHeader *header,
                                           UNUSED const uint8_t *param_data) {
  const ResponderDefinition *definition = g_responder->def;
  return RDMResponder_GenericReturnString(header, definition->model_description,
                                          RDM_DEFAULT_STRING_SIZE);
}

int RDMResponder_GetManufacturerLabel(const RDMHeader *header,
                                      UNUSED const uint8_t *param_data) {
  const ResponderDefinition *definition = g_responder->def;
  return RDMResponder_GenericReturnString(header,
                                          definition->manufacturer_label,
                                          RDM_DEFAULT_STRING_SIZE);
}

int RDMResponder_GetSoftwareVersionLabel(const RDMHeader *header,
                                         UNUSED const uint8_t *param_data) {
  const ResponderDefinition *definition = g_responder->def;
  return RDMResponder_GenericReturnString(header,
                                          definition->software_version_label,
                                          RDM_DEFAULT_STRING_SIZE);
}

int RDMResponder_GetBootSoftwareVersion(const RDMHeader *header,
                                        UNUSED const uint8_t *param_data) {
  return RDMResponder_GenericGetUInt32(header, BOOT_SOFTWARE_VERSION);
}

int RDMResponder_GetBootSoftwareVersionLabel(const RDMHeader *header,
                                             UNUSED const uint8_t *param_data) {
  return RDMResponder_GenericReturnString(header,
                                          BOOT_SOFTWARE_LABEL,
                                          RDM_DEFAULT_STRING_SIZE);
}

int RDMResponder_GetDeviceLabel(const RDMHeader *header,
                                UNUSED const uint8_t *param_data) {
  return RDMResponder_GenericReturnString(header, g_responder->device_label,
                                          RDM_DEFAULT_STRING_SIZE);
}

int RDMResponder_SetDeviceLabel(const RDMHeader *header,
                                const uint8_t *param_data) {
  if (header->param_data_length > RDM_DEFAULT_STRING_SIZE) {
    return RDMResponder_BuildNack(header, NR_FORMAT_ERROR);
  }
  RDMUtil_StringCopy(g_responder->device_label, RDM_DEFAULT_STRING_SIZE,
                     (const char*) param_data, header->param_data_length);
  g_responder->using_factory_defaults = false;
  return RDMResponder_BuildSetAck(header);
}

int RDMResponder_GetDMXPersonality(const RDMHeader *header,
                                   UNUSED const uint8_t *param_data) {
  uint8_t *ptr = g_rdm_buffer + sizeof(RDMHeader);
  *ptr++ = g_responder->current_personality;
  *ptr++ = g_responder->def->personality_count;
  return RDMResponder_AddHeaderAndChecksum(header, ACK, ptr - g_rdm_buffer);
}

int RDMResponder_SetDMXPersonality(const RDMHeader *header,
                                   const uint8_t *param_data) {
  if (header->param_data_length != sizeof(uint8_t)) {
    return RDMResponder_BuildNack(header, NR_FORMAT_ERROR);
  }

  uint8_t new_personality = param_data[0];
  if (new_personality == 0u ||
      new_personality > g_responder->def->personality_count) {
    return RDMResponder_BuildNack(header, NR_DATA_OUT_OF_RANGE);
  }

  if (g_responder->current_personality != new_personality) {
    g_responder->using_factory_defaults = false;
  }
  g_responder->current_personality = new_personality;
  return RDMResponder_BuildSetAck(header);
}

int RDMResponder_GetDMXPersonalityDescription(const RDMHeader *header,
                                              const uint8_t *param_data) {
  uint8_t index = param_data[0];
  if (index == 0u || index > g_responder->def->personality_count) {
    return RDMResponder_BuildNack(header, NR_DATA_OUT_OF_RANGE);
  }

  if (!g_responder->def->personalities) {
    return RDMResponder_BuildNack(header, NR_HARDWARE_FAULT);
  }

  const PersonalityDefinition *personality =
      &g_responder->def->personalities[index - 1];

  uint8_t *ptr = g_rdm_buffer + sizeof(RDMHeader);
  *ptr++ = index;
  ptr = PushUInt16(ptr, personality->dmx_footprint);
  ptr += RDMUtil_StringCopy((char*) ptr, RDM_DEFAULT_STRING_SIZE,
                            personality->description, RDM_DEFAULT_STRING_SIZE);
  return RDMResponder_AddHeaderAndChecksum(header, ACK, ptr - g_rdm_buffer);
}

int RDMResponder_GetDMXStartAddress(const RDMHeader *header,
                                    UNUSED const uint8_t *param_data) {
  uint8_t *ptr = g_rdm_buffer + sizeof(RDMHeader);
  ptr = PushUInt16(ptr, g_responder->dmx_start_address);
  return RDMResponder_AddHeaderAndChecksum(header, ACK, ptr - g_rdm_buffer);
}

int RDMResponder_SetDMXStartAddress(const RDMHeader *header,
                                    const uint8_t *param_data) {
  if (header->param_data_length != sizeof(uint16_t)) {
    return RDMResponder_BuildNack(header, NR_FORMAT_ERROR);
  }

  uint16_t address = JoinShort(param_data[0], param_data[1]);
  if (address == 0u || address > MAX_DMX_START_ADDRESS) {
    return RDMResponder_BuildNack(header, NR_DATA_OUT_OF_RANGE);
  }

  if (g_responder->dmx_start_address != address) {
    g_responder->using_factory_defaults = false;
  }
  g_responder->dmx_start_address = address;
  return RDMResponder_BuildSetAck(header);
}

int RDMResponder_GetSlotInfo(const RDMHeader *header,
                             UNUSED const uint8_t *param_data) {
  const PersonalityDefinition *personality = CurrentPersonality();
  if (!personality || !personality->slots) {
    return RDMResponder_BuildNack(header, NR_HARDWARE_FAULT);
  }

  // TODO(simon): If we have more than 46 slots we'll need to ACK_OVERFLOW
  unsigned int slot_count = min(MAX_SLOT_INFO_PER_FRAME,
                                personality->slot_count);
  uint8_t *ptr = g_rdm_buffer + sizeof(RDMHeader);
  unsigned int i = 0u;
  for (; i < slot_count; i++) {
    ptr = PushUInt16(ptr, i);
    *ptr++ = personality->slots[i].slot_type;
    ptr = PushUInt16(ptr, personality->slots[i].slot_label_id);
  }

  return RDMResponder_AddHeaderAndChecksum(header, ACK, ptr - g_rdm_buffer);
}

int RDMResponder_GetSlotDescription(const RDMHeader *header,
                                    UNUSED const uint8_t *param_data) {
  uint16_t slot_index = JoinShort(param_data[0], param_data[1]);

  const PersonalityDefinition *personality = CurrentPersonality();
  if (!personality || !personality->slots) {
    return RDMResponder_BuildNack(header, NR_HARDWARE_FAULT);
  }

  if (slot_index >= personality->slot_count) {
    return RDMResponder_BuildNack(header, NR_DATA_OUT_OF_RANGE);
  }

  uint8_t *ptr = g_rdm_buffer + sizeof(RDMHeader);
  ptr = PushUInt16(ptr, slot_index);
  ptr += RDMUtil_StringCopy((char*) ptr, RDM_DEFAULT_STRING_SIZE,
                            personality->slots[slot_index].description,
                            RDM_DEFAULT_STRING_SIZE);
  return RDMResponder_AddHeaderAndChecksum(header, ACK, ptr - g_rdm_buffer);
}

int RDMResponder_GetDefaultSlotValue(const RDMHeader *header,
                                     UNUSED const uint8_t *param_data) {
  const PersonalityDefinition *personality = CurrentPersonality();
  if (!personality || !personality->slots) {
    return RDMResponder_BuildNack(header, NR_HARDWARE_FAULT);
  }

  // TODO(simon): If we have more than 77 slots we'll need to ACK_OVERFLOW
  uint8_t *ptr = g_rdm_buffer + sizeof(RDMHeader);
  unsigned int slot_count = min(MAX_DEFAULT_SLOT_VALUE_PER_FRAME,
                                personality->slot_count);
  unsigned int i = 0u;
  for (; i < slot_count; i++) {
    ptr = PushUInt16(ptr, i);
    *ptr++ = personality->slots[i].default_value;
  }
  return RDMResponder_AddHeaderAndChecksum(header, ACK, ptr - g_rdm_buffer);
}

int RDMResponder_GetSensorDefinition(const RDMHeader *header,
                                     const uint8_t *param_data) {
  uint8_t sensor_index = param_data[0];

  if (sensor_index >= g_responder->def->sensor_count) {
    return RDMResponder_BuildNack(header, NR_DATA_OUT_OF_RANGE);
  }

  const SensorDefinition *sensor_ptr = &g_responder->def->sensors[sensor_index];
  uint8_t *ptr = g_rdm_buffer + sizeof(RDMHeader);
  *ptr++ = sensor_index;
  *ptr++ = sensor_ptr->type;
  *ptr++ = sensor_ptr->unit;
  *ptr++ = sensor_ptr->prefix;
  ptr = PushUInt16(ptr, sensor_ptr->range_minimum_value);
  ptr = PushUInt16(ptr, sensor_ptr->range_maximum_value);
  ptr = PushUInt16(ptr, sensor_ptr->normal_minimum_value);
  ptr = PushUInt16(ptr, sensor_ptr->normal_maximum_value);
  *ptr++ = sensor_ptr->recorded_value_support;

  ptr += RDMUtil_StringCopy((char*) ptr, RDM_DEFAULT_STRING_SIZE,
                            sensor_ptr->description, RDM_DEFAULT_STRING_SIZE);
  return RDMResponder_AddHeaderAndChecksum(header, ACK, ptr - g_rdm_buffer);
}

int RDMResponder_GetSensorValue(const RDMHeader *header,
                                const uint8_t *param_data) {
  uint8_t sensor_index = param_data[0];

  if (sensor_index >= g_responder->def->sensor_count) {
    return RDMResponder_BuildNack(header, NR_DATA_OUT_OF_RANGE);
  }

  SensorData *sensor_ptr = &g_responder->sensors[sensor_index];

  if (sensor_ptr->should_nack) {
    return RDMResponder_BuildNack(header, sensor_ptr->nack_reason);
  }

  uint8_t *ptr = BuildSensorValueResponse(g_rdm_buffer + sizeof(RDMHeader),
                                          sensor_index, sensor_ptr);
  return RDMResponder_AddHeaderAndChecksum(header, ACK, ptr - g_rdm_buffer);
}

int RDMResponder_SetSensorValue(const RDMHeader *header,
                                const uint8_t *param_data) {
  if (header->param_data_length != sizeof(uint8_t)) {
    return RDMResponder_BuildNack(header, NR_FORMAT_ERROR);
  }

  uint8_t sensor_index = param_data[0];
  if (sensor_index < g_responder->def->sensor_count) {
    ResetSensor(sensor_index);
  } else if (sensor_index == ALL_SENSORS) {
    unsigned int i = 0u;
    for (; i < g_responder->def->sensor_count; i++) {
      ResetSensor(i);
    }
  } else {
    return RDMResponder_BuildNack(header, NR_DATA_OUT_OF_RANGE);
  }

  ReturnUnlessUnicast(header);

  uint8_t *ptr = g_rdm_buffer + sizeof(RDMHeader);
  if (sensor_index == ALL_SENSORS) {
    memset(ptr, 0, SENSOR_VALUE_PARAM_DATA_LENGTH);
    ptr += SENSOR_VALUE_PARAM_DATA_LENGTH;
  } else {
    ptr = BuildSensorValueResponse(ptr, sensor_index,
                                   &g_responder->sensors[sensor_index]);
  }

  return RDMResponder_AddHeaderAndChecksum(header, ACK, ptr - g_rdm_buffer);
}

int RDMResponder_SetRecordSensor(const RDMHeader *header,
                                 const uint8_t *param_data) {
  if (header->param_data_length != sizeof(uint8_t)) {
    return RDMResponder_BuildNack(header, NR_FORMAT_ERROR);
  }

  uint8_t sensor_index = param_data[0];
  if (sensor_index < g_responder->def->sensor_count) {
    if (g_responder->def->sensors[sensor_index].recorded_value_support &
        SENSOR_SUPPORTS_RECORDING_MASK) {
      RecordSensor(sensor_index);
      return RDMResponder_BuildSetAck(header);
    } else {
      return RDMResponder_BuildNack(header, NR_DATA_OUT_OF_RANGE);
    }
  } else if (sensor_index == ALL_SENSORS) {
    unsigned int i = 0u;
    for (; i < g_responder->def->sensor_count; i++) {
      RecordSensor(i);
    }
    return RDMResponder_BuildSetAck(header);
  } else {
    return RDMResponder_BuildNack(header, NR_DATA_OUT_OF_RANGE);
  }
}

int RDMResponder_GetIdentifyDevice(const RDMHeader *header,
                                   UNUSED const uint8_t *param_data) {
  return RDMResponder_GenericGetBool(header, g_responder->identify_on);
}

int RDMResponder_SetIdentifyDevice(const RDMHeader *header,
                                   const uint8_t *param_data) {
  bool previous_identify = g_responder->identify_on;
  int r = RDMResponder_GenericSetBool(header, param_data,
                                      &g_responder->identify_on);
  if (g_responder->identify_on == previous_identify) {
    return r;
  }
  g_responder->using_factory_defaults = false;
  if (g_responder->identify_on) {
    g_internal_state.identify_timer = CoarseTimer_GetTime();
    PLIB_PORTS_PinSet(PORTS_ID_0, g_internal_state.identify_port,
                      g_internal_state.identify_bit);
  } else {
    PLIB_PORTS_PinClear(PORTS_ID_0, g_internal_state.identify_port,
                        g_internal_state.identify_bit);
  }
  return r;
}

int RDMResponder_HandleDiscovery(const RDMHeader *header,
                                 const uint8_t *param_data) {
  if (ntohs(header->sub_device) != SUBDEVICE_ROOT) {
    // The standard isn't clear on what should occur here.
    // We just silently drop the request, since we can't NACK it (6.3 of E1.20)
    return RDM_RESPONDER_NO_RESPONSE;
  }

  switch (ntohs(header->param_id)) {
    case PID_DISC_UNIQUE_BRANCH:
      return RDMResponder_HandleDUBRequest(param_data,
                                           header->param_data_length);
    case PID_DISC_MUTE:
      return RDMResponder_SetMute(header);
    case PID_DISC_UN_MUTE:
      return RDMResponder_SetUnMute(header);
    default:
      {}
  }
  return RDM_RESPONDER_NO_RESPONSE;
}
