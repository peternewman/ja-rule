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
 * transceiver_timing.h
 * Copyright (C) 2015 Simon Newton
 */

#ifndef FIRMWARE_SRC_TRANSCEIVER_TIMING_H_
#define FIRMWARE_SRC_TRANSCEIVER_TIMING_H_

/**
 * @addtogroup transceiver
 * @{
 * @file transceiver_timing.h
 * @brief The timing constants for the 485 Transceiver.
 *
 * This header contains the timing constants from E1.11-2008 [DMX] and
 * E1.20-2010 [RDM]. In some cases we allow the user to configure values
 * outside the range in the standards; this is for testing purposes.
 */

// Common params
// ----------------------------------------------------------------------------

/**
 * @brief The minimum break time the user can configure, in microseconds.
 *
 * DMX1990 was 88us and later versions raised this to 92us. We allow down to
 * 44us because Sean says it's useful for testing.
 */
#define MINIMUM_TX_BREAK_TIME 44u

/**
 * @brief The maximum break time the user can configure, in microseconds.
 *
 * This needs to be kept within what a 16-bit timer can support. We'll probably
 * need to change this if the clock speed increases.
 */
#define MAXIMUM_TX_BREAK_TIME 800u

/**
 * @brief The minimum mark time the user can configure, in microseconds.
 *
 * DMX1986 apparently allows a 4us mark time.
 */
#define MINIMUM_TX_MARK_TIME 4u

/**
 * @brief The maximum mark time the user can configure, in microseconds.
 *
 * This needs to be kept within what a 16-bit timer can support. We'll probably
 * need to change this if the clock speed increases.
 */
#define MAXIMUM_TX_MARK_TIME 800u

// Controller params
// ----------------------------------------------------------------------------

/**
 * @brief The minimum break time for controllers to receive.
 *
 * Measured in 10ths of a microsecond. The value is from line 2 of Table 3-1
 * in E1.20.
 *
 * Note that some responders, like say the Robin 600 Spot, have a very short low
 * before the actual break. This is probably the transceiver turning around and
 * lasts on the order of 200nS.
 */
#define CONTROLLER_RX_BREAK_TIME_MIN 880u

/**
 * @brief The maximum break time for controllers to receive.
 *
 * Measured in 10ths of a microsecond. The value is from line 2 of Table 3-1
 * in E1.20.
 */
#define CONTROLLER_RX_BREAK_TIME_MAX 3520u

/**
 * @brief The maximum mark time for controllers to receive.
 *
 * Measured in 10ths of a microsecond. The value is from line 2 of Table 3-1
 * in E1.20.
 */
#define CONTROLLER_RX_MARK_TIME_MAX 880u  // 88us

/**
 * @brief The minimum break-to-break time at a controller.
 *
 * Measured in 10ths of a millisecond. The value is from Table 6
 * in E1.11 (2008). In this case we round 1.204ms to 1.3 ms.
 */
#define CONTROLLER_MIN_BREAK_TO_BREAK 13u

/**
 * @brief The back off time for a DUB command
 *
 * Measured in 10ths of a millisecond. The value is from line 2 of Table 3-2
 * in E1.20.
 */
#define CONTROLLER_DUB_BACKOFF 58u

/**
 * @brief The back off time for a broadcast command.
 *
 * Measured in 10ths of a millisecond. The value is from line 6 of Table 3-2
 * in E1.20. In this case we round 176us to 0.2 ms.
 */
#define CONTROLLER_BROADCAST_BACKOFF 2u

/**
 * @brief The back off time for a missing response.
 *
 * Measured in 10ths of a millisecond. The value is from line 5 of Table 3-2
 * in E1.20.
 */
#define CONTROLLER_MISSING_RESPONSE_BACKOFF 30u

/**
 * @brief The back off time for a non-RDM command
 *
 * Measured in 10ths of a millisecond. The value is from line 7 of Table 3-2
 * in E1.20. In this case we round 176us to 0.2 ms.
 */
#define CONTROLLER_NON_RDM_BACKOFF 2u

// Responder params
// ----------------------------------------------------------------------------

/**
 * @brief The minimum break time for responders to receive.
 *
 * Measured in 10ths of a microsecond. The value is from line 1 of Table 3-3
 * in E1.20.
 */
#define RESPONDER_RX_BREAK_TIME_MIN  880u

/**
 * @brief The maximum break time for responders to receive.
 *
 * Measured in 10ths of a millisecond. The value is from line 1 of Table 3-3
 * in E1.20.
 */
#define RESPONDER_RX_BREAK_TIME_MAX  10000u

/**
 * @brief The minimum RDM responder delay in 10ths of a microsecond, Table 3-4,
 * E1.20
 */
#define MINIMUM_RESPONDER_DELAY 1760u

/**
 * @brief The maximum RDM responder delay in 10ths of a microsecond. Table 3-4,
 * E1.20
 */
#define MAXIMUM_RESPONDER_DELAY 20000u

/**
 * @brief The minimum mark time for responders to receive
 *
 * Measured in 10ths of a microsecond. The value is from line 1 of Table 3-3
 * in E1.20.
 */
#define RESPONDER_RX_MARK_TIME_MIN  80u

/**
 * @brief The maximum mark time for responders to receive.
 *
 * Measured in 10ths of a millisecond. The value is from line 1 of Table 3-3
 * in E1.20.
 */
#define RESPONDER_RX_MARK_TIME_MAX  10000u  // 1s

/**
 * @brief The inter-slot timeout for RDM frames.
 *
 * Measured in 10ths of a millisecond. The value is from line 1 of Table 3-3
 * in E1.20.
 */
#define RESPONDER_RDM_INTERSLOT_TIMEOUT 21u  // 2.1ms

/**
 * @brief The inter-slot timeout for DMX and other ASC frames.
 *
 * Measured in 10ths of a millisecond. The value is from Table 6 of E1.11-2008
 */
#define RESPONDER_DMX_INTERSLOT_TIMEOUT 10000u  // 1s

/**
 * @brief The inter-slot timeout for RDM frames.
 *
 * Measured in 10ths of a millisecond. The value is from line 2 of Table 3-1
 * in E1.20.
 */
#define CONTROLLER_RECEIVE_RDM_INTERSLOT_TIMEOUT 21u  // 2.1ms


/**
 * @}
 */

#endif   // FIRMWARE_SRC_TRANSCEIVER_TIMING_H_
