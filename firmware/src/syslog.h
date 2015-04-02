/*
 * File:   syslog.h
 * Author: Simon Newton
 */

/**
 * @defgroup logging Logging
 * @brief The Logging Subsystem.
 *
 * The logging system is made up of two parts. The upper layer provides message
 * formatting, and discards log messages that are less than the current log
 * level.
 *
 * The bottom layer is the logging transport. This can be
 *  - Via LOG messages using the vendor class USB device.
 *  - Over a CDC class USB device (serial console)
 *
 * The low level implementation is determined by the callback function passed
 * to SysLog_Initialize (or via PIPELINE_LOG_WRITE).
 *
 * @addtogroup logging
 * @{
 * @file syslog.h
 * @brief The upper layer of the Logging subsystem.
 *
 * This module is the top half of the logging system. It's responsible for
 * formatting messages and discarding messages that are less than the current
 * log level.
 */

#ifndef FIRMWARE_SRC_SYSLOG_H_
#define FIRMWARE_SRC_SYSLOG_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The system log levels.
 */
typedef enum {
  SYSLOG_DEBUG,  //!< Debug messages
  SYSLOG_INFO,  //!< Info messages
  SYSLOG_WARN,  //!< Warnings
  SYSLOG_ERROR,  //!< Errors
  SYSLOG_FATAL,  //!< Fatal events
  SYSLOG_ALWAYS  //!< Always logged regardless of log level.
} SysLogLevel;

/**
 * @brief A function pointer to log a message.
 * @param msg The message to log, must be NULL terminated.
 */
typedef void (*SysLogWriteFn)(const char*);

/**
 * @brief Initialize the System Logging module.
 * @param write_fn The function to use for logging messages.
 *
 * If PIPELINE_LOG_WRITE is defined in system_pipeline.h, the macro
 * will override the write_fn argument.
 */
void SysLog_Initialize(SysLogWriteFn write_fn);

/**
 * @brief Log a message.
 * @param level the log level of the message
 * @param msg the message to log.
 */
void SysLog_Message(SysLogLevel level, const char* msg);

/**
 * @brief Format and log a message.
 * @param level the log level of the message
 * @param format The format string.
 */
void SysLog_Print(SysLogLevel level, const char* format, ...);

/**
 * @brief Return the current log level.
 * @return The current log level.
 */
SysLogLevel SysLog_GetLevel();

/**
 * @brief Set the log level.
 * @param level the new log level.
 */
void SysLog_SetLevel(SysLogLevel level);

/**
 * @brief Increase the verbosity of the logging.
 */
void SysLog_Increment();

/**
 * @brief Decrease the verbosity of the logging.
 */
void SysLog_Decrement();

/**
 * @brief Return the string description of a log level.
 * @param level The log level.
 * @returns A statically allocated string which contains the text description
 * of the log level.
 */
const char* SysLog_LevelToString(SysLogLevel level);

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif  // FIRMWARE_SRC_SYSLOG_H_