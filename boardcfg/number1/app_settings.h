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
 * number1/app_settings.h
 * Copyright (C) 2015 Simon Newton
 */

#ifndef BOARDCFG_NUMBER1_APP_SETTINGS_H_
#define BOARDCFG_NUMBER1_APP_SETTINGS_H_

#include "board_init.h"
#include "common_settings.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @addtogroup boardcfg
 * @{
 * @file app_settings.h
 * @brief Configuration settings for the main application.
 *
 * These will need to be adjusted to suit the particular processor / board
 * used.
 *
 * @name Board Specific Hooks
 * These hooks can be used for board specific configuration at various stages
 * during the initialization sequence.
 * @{
 */

/**
 * @def PRE_APP_INIT_HOOK
 * @brief This hook is called prior to the initialization of the application
 * modules (APP_Initialize).
 *
 * Remember that pins will default to analog if they share a function with the
 * A/D Convertor. If any of your pins share with the ADC, you'll need to change
 * them to digital mode. This should be done using this hook.
 */
#define PRE_APP_INIT_HOOK Number1_PreAppHook

/**
 * @}
 *
 * @name Coarse Timer
 * Settings for the @ref timer. These are used to initialize
 * CoarseTimer_Settings.
 * @{
 */

/**
 * @brief The timer to use for the coarse timer.
 */
#define COARSE_TIMER_ID 2

/**
 * @}
 *
 * @name Transceiver
 * Settings for the @ref transceiver. These are used to initialize
 * TransceiverHardwareSettings.
 * @{
 */

/**
 * @brief The USART to use for the DMX/RDM transceiver.
 */
#define TRANSCEIVER_UART 5

/**
 * @brief The Timer module id to use for the DMX/RDM transceiver.
 */
#define TRANSCEIVER_TIMER 3

/**
 * @brief The input capture module id to use for the DMX/RDM transceiver.
 */
#define TRANSCEIVER_IC 5

/**
 * @brief The port to use for the direction & break pins.
 */
#define TRANSCEIVER_PORT PORT_CHANNEL_B

/**
 * @brief The bit position of the I/O pin used to create the break.
 */
#define TRANSCEIVER_PORT_BIT PORTS_BIT_POS_14

/**
 * @brief The bit position of the TX enable pin.
 */
#define TRANSCEIVER_TX_ENABLE_PORT_BIT PORTS_BIT_POS_9

/**
 * @brief The bit position of the RX enable pin.
 */
#define TRANSCEIVER_RX_ENABLE_PORT_BIT PORTS_BIT_POS_10

/**
 * @}
 *
 * @name RDM Responder
 * Settings for the @ref rdm_responder.h "RDM Responder". These are used to
 * initialize RDMResponderSettings.
 * @{
 */

/**
 * @brief The port containing the RDM identify LED.
 */
#define RDM_RESPONDER_IDENTIFY_PORT PORT_CHANNEL_D

/**
 * @brief The bit position of the RDM identify LED.
 */
#define RDM_RESPONDER_IDENTIFY_PORT_BIT PORTS_BIT_POS_1

/**
 * @brief The port containing the RDM mute status LED.
 */
#define RDM_RESPONDER_MUTE_PORT PORT_CHANNEL_C

/**
 * @brief The bit position of the RDM mute status LED.
 */
#define RDM_RESPONDER_MUTE_PORT_BIT PORTS_BIT_POS_14

/**
 * @brief The pin to use for the RDM temperature sensor.
 *
 * If undefined, we use a 'dummy' sensor that generates random values.
 */
#define RDM_RESPONDER_TEMPERATURE_SENSOR ADC_INPUT_POSITIVE_AN2

/**
 * @}
 *
 * @name SPI DMX
 * Settings for the @ref spi_dmx. These are used to initialize
 * SPIRGBConfiguration.
 * @{
 */

/**
 * @brief The SPI module to use for output.
 */
#define SPI_MODULE_ID SPI_ID_2

/**
 * @brief The baud rate of the SPI output.
 */
#define SPI_BAUD_RATE 1000000u

/**
 * @brief Use enhanced buffering.
 */
#define SPI_USE_ENHANCED_BUFFERING true

/**
 * @}
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif  // BOARDCFG_NUMBER1_APP_SETTINGS_H_
