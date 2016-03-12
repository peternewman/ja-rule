/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * TransceiverMock.h
 * The Mock Transceiver module.
 * Copyright (C) 2015 Simon Newton
 */

#ifndef TESTS_MOCKS_TRANSCEIVERMOCK_H_
#define TESTS_MOCKS_TRANSCEIVERMOCK_H_

#include <gmock/gmock.h>
#include <stdint.h>

#include "transceiver.h"

class MockTransceiver {
 public:
  MOCK_METHOD3(Initialize, void(const TransceiverHardwareSettings* settings,
                                TransceiverEventCallback tx_callback,
                                TransceiverEventCallback rx_callback));
  MOCK_METHOD2(SetMode, bool(TransceiverMode mode, int16_t token));
  MOCK_METHOD0(GetMode, TransceiverMode());
  MOCK_METHOD0(Tasks, void());
  MOCK_METHOD3(QueueDMX, bool(int16_t token, const uint8_t* data,
                              unsigned int size));
  MOCK_METHOD4(QueueASC, bool(int16_t token, uint8_t start_code,
                              const uint8_t* data, unsigned int size));
  MOCK_METHOD3(QueueRDMDUB, bool(int16_t token, const uint8_t* data,
                                 unsigned int size));
  MOCK_METHOD4(QueueRDMRequest, bool(int16_t token, const uint8_t* data,
                                     unsigned int size, bool is_broadcast));
  MOCK_METHOD1(QueueSelfTest, bool(int16_t token));
  MOCK_METHOD0(Transceiver_Reset, void());
  MOCK_METHOD1(SetBreakTime, bool(uint16_t break_time_us));
  MOCK_METHOD0(GetBreakTime, uint16_t());
  MOCK_METHOD1(SetMarkTime, bool(uint16_t break_time_us));
  MOCK_METHOD0(GetMarkTime, uint16_t());
  MOCK_METHOD1(SetRDMBroadcastTimeout, bool(uint16_t break_time_us));
  MOCK_METHOD0(GetRDMBroadcastTimeout, uint16_t());
  MOCK_METHOD1(SetRDMResponseTimeout, bool(uint16_t break_time_us));
  MOCK_METHOD0(GetRDMResponseTimeout, uint16_t());
  MOCK_METHOD1(SetRDMDUBResponseLimit, bool(uint16_t limit));
  MOCK_METHOD0(GetRDMDUBResponseLimit, uint16_t());
  MOCK_METHOD1(SetRDMResponderDelay, bool(uint16_t delay));
  MOCK_METHOD0(GetRDMResponderDelay, uint16_t());
  MOCK_METHOD1(SetRDMResponderJitter, bool(uint16_t max_jitter));
  MOCK_METHOD0(GetRDMResponderJitter, uint16_t());
};

void Transceiver_SetMock(MockTransceiver* mock);

#endif  // TESTS_MOCKS_TRANSCEIVERMOCK_H_
