/*
* EEZ Generic Firmware
* Copyright (C) 2018-present, Envox d.o.o.
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

#include <eez/firmware.h>

namespace eez {

static const uint16_t MODULE_TYPE_NONE = 0;
static const uint16_t MODULE_TYPE_DCP405 = 405;
static const uint16_t MODULE_TYPE_DCM220 = 220;
static const uint16_t MODULE_TYPE_DCM224 = 224;
static const uint16_t MODULE_TYPE_DIB_MIO168 = 168;
static const uint16_t MODULE_TYPE_DIB_PREL6 = 6;
static const uint16_t MODULE_TYPE_DIB_SMX46 = 46;

static const uint16_t MODULE_CATEGORY_NONE = 0;
static const uint16_t MODULE_CATEGORY_DCPSUPPLY = 1;
static const uint16_t MODULE_CATEGORY_OTHER = 2;

enum SlotViewType {
    SLOT_VIEW_TYPE_DEFAULT,
    SLOT_VIEW_TYPE_MAX,
    SLOT_VIEW_TYPE_MIN,
    SLOT_VIEW_TYPE_MICRO,
};

enum FlashMethod {
    FLASH_METHOD_NONE,
    FLASH_METHOD_STM32_BOOTLOADER_UART,
    FLASH_METHOD_STM32_BOOTLOADER_SPI
};

struct Module;

struct ModuleInfo {
    uint16_t moduleType;
    uint16_t moduleCategory;
    const char *moduleName;
    const char *moduleBrand;
    uint16_t latestModuleRevision;
    FlashMethod flashMethod;

    ModuleInfo(uint16_t moduleType, uint16_t moduleCategory, const char *moduleName, const char *moduleBrand, uint16_t latestModuleRevision, FlashMethod flashMethod);

    virtual Module *createModule(uint8_t slotIndex, uint16_t moduleRevision) = 0;
    virtual int getSlotView(SlotViewType slotViewType, int slotIndex, int cursor);
};

struct Module {
    uint8_t slotIndex;

    ModuleInfo *moduleInfo;
    uint16_t moduleRevision;

	uint8_t firmwareMajorVersion;
	uint8_t firmwareMinorVersion;
	uint32_t idw0;
	uint32_t idw1;
	uint32_t idw2;

    Module(uint8_t slotIndex, ModuleInfo *moduleInfo, uint16_t moduleRevision);

    virtual TestResult getTestResult();

    virtual void boot();
    virtual void initChannels();
    virtual void tick();
    virtual void onPowerDown();
};

static const int NUM_SLOTS = 3;
extern Module *g_slots[NUM_SLOTS];

ModuleInfo *getModuleInfo(uint16_t moduleType);

void getModuleSerialInfo(uint8_t slotIndex, char *text);

} // namespace eez
