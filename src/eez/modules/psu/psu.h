/*
 * EEZ Modular Firmware
 * Copyright (C) 2015-present, Envox d.o.o.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <stdint.h>

#if !defined(EEZ_PLATFORM_SIMULATOR) && !defined(EEZ_PLATFORM_STM32)
#define EEZ_PLATFORM_STM32
#endif

#include <eez/system.h>

#include <eez/modules/psu/conf.h>
#include <eez/modules/psu/conf_advanced.h>
#include <eez/modules/psu/conf_user.h>

#include <eez/index.h>
#include <eez/util.h>

#include <eez/modules/psu/channel.h>
#include <eez/modules/psu/debug.h>
#include <eez/modules/psu/serial.h>

/// Namespace for the everything from the EEZ.
namespace eez {

extern TestResult g_masterTestResult;

#if defined(EEZ_PLATFORM_SIMULATOR)
char *getConfFilePath(const char *file_name);
#endif

void generateError(int16_t error);

void onSdCardFileChangeHook(const char *filePath1, const char *filePath2 = nullptr);

/// PSU firmware.
namespace psu {

void startThread();

extern osThreadId g_psuTaskHandle;
extern osMessageQId g_psuMessageQueueId;

enum {
    PSU_QUEUE_MESSAGE_TYPE_CHANGE_POWER_STATE,
    PSU_QUEUE_MESSAGE_TYPE_RESET,
    PSU_QUEUE_MESSAGE_TYPE_TEST,
    PSU_QUEUE_MESSAGE_SPI_IRQ,
    PSU_QUEUE_MESSAGE_ADC_MEASURE_ALL,
    PSU_QUEUE_TRIGGER_START_IMMEDIATELY,
    PSU_QUEUE_TRIGGER_ABORT,
    PSU_QUEUE_TRIGGER_CHANNEL_SAVE_AND_DISABLE_OE,
    PSU_QUEUE_TRIGGER_CHANNEL_RESTORE_OE,
    PSU_QUEUE_SET_COUPLING_TYPE,
    PSU_QUEUE_SET_TRACKING_CHANNELS,
    PSU_QUEUE_CHANNEL_OUTPUT_ENABLE,
    PSU_QUEUE_SYNC_OUTPUT_ENABLE,
    PSU_QUEUE_MESSAGE_TYPE_HARD_RESET,
    PSU_QUEUE_MESSAGE_TYPE_SHUTDOWN,
    PSU_QUEUE_MESSAGE_TYPE_SET_VOLTAGE,
    PSU_QUEUE_MESSAGE_TYPE_SET_CURRENT
};

#define PSU_QUEUE_MESSAGE(type, param) (((param) << 8) | (type))
#define PSU_QUEUE_MESSAGE_TYPE(message) ((message) & 0xFF)
#define PSU_QUEUE_MESSAGE_PARAM(param) ((message) >> 8)

bool measureAllAdcValuesOnChannel(int channelIndex);

void initChannels();
bool testChannels();

bool powerUp();
void powerDown();
void powerDownChannels();
bool isPowerUp();
void changePowerState(bool up);
void powerDownBySensor();

bool psuReset();

bool autoRecall(int recallOptions = 0);

void onProtectionTripped();

void tick();

void setQuesBits(int bit_mask, bool on);
void setOperBits(int bit_mask, bool on);

const char *getCpuModel();
const char *getCpuType();

bool isMaxCurrentLimited();
MaxCurrentLimitCause getMaxCurrentLimitCause();
void limitMaxCurrent(MaxCurrentLimitCause cause);
void unlimitMaxCurrent();

extern volatile bool g_insideInterruptHandler;

enum RLState { RL_STATE_LOCAL = 0, RL_STATE_REMOTE = 1, RL_STATE_RW_LOCK = 2 };

extern RLState g_rlState;

extern bool g_rprogAlarm;

extern void (*g_diagCallback)();

#if defined(EEZ_PLATFORM_SIMULATOR)
namespace simulator {

void init();
void tick();

void setTemperature(int sensor, float value);
float getTemperature(int sensor);

bool getPwrgood(int pin);
void setPwrgood(int pin, bool on);

bool getRPol(int pin);
void setRPol(int pin, bool on);

bool getCV(int pin);
void setCV(int pin, bool on);

bool getCC(int pin);
void setCC(int pin, bool on);

void exit();

} // namespace simulator
#endif

} // namespace psu
} // namespace eez
