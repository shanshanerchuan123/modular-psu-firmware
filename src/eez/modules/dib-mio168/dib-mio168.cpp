/*
 * EEZ DIB MIO168
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

#include <assert.h>
#include <stdlib.h>
#include <memory.h>

#include "eez/debug.h"
#include "eez/firmware.h"
#include "eez/index.h"
#include "eez/hmi.h"
#include "eez/gui/gui.h"
#include "eez/modules/psu/event_queue.h"
#include "eez/modules/bp3c/comm.h"

#include "./dib-mio168.h"

namespace eez {
namespace dib_mio168 {

struct Mio168ModuleInfo : public ModuleInfo {
public:
    Mio168ModuleInfo() 
        : ModuleInfo(MODULE_TYPE_DIB_MIO168, MODULE_CATEGORY_OTHER, "MIO168", "Envox", MODULE_REVISION_R1B2, FLASH_METHOD_STM32_BOOTLOADER_SPI)
    {}

    Module *createModule(uint8_t slotIndex, uint16_t moduleRevision) override;
    
    int getSlotView(SlotViewType slotViewType, int slotIndex, int cursor) override {
        if (slotViewType == SLOT_VIEW_TYPE_DEFAULT) {
            return gui::PAGE_ID_DIB_MIO168_SLOT_VIEW_DEF;
        }
        if (slotViewType == SLOT_VIEW_TYPE_MAX) {
            return gui::PAGE_ID_DIB_MIO168_SLOT_VIEW_MAX;
        }
        if (slotViewType == SLOT_VIEW_TYPE_MIN) {
            return gui::PAGE_ID_DIB_MIO168_SLOT_VIEW_MIN;
        }
        assert(slotViewType == SLOT_VIEW_TYPE_MICRO);
        return gui::PAGE_ID_DIB_MIO168_SLOT_VIEW_MICRO;
    }
};

#define BUFFER_SIZE 20

struct Mio168Module : public Module {
public:
    TestResult testResult = TEST_NONE;
    bool synchronized = false;
    int numCrcErrors = 0;
    uint8_t input[BUFFER_SIZE];
    uint8_t output[BUFFER_SIZE];

    uint8_t inputPinStates = 0;
    uint8_t outputPinStates = 0;

    Mio168Module(uint8_t slotIndex, ModuleInfo *moduleInfo, uint16_t moduleRevision)
        : Module(slotIndex, moduleInfo, moduleRevision)
    {
        memset(input, 0, sizeof(input));
        memset(output, 0, sizeof(output));
    }

    TestResult getTestResult() override {
        return testResult;
    }

    void initChannels() override {
        if (!synchronized) {
            if (bp3c::comm::masterSynchro(slotIndex)) {
                synchronized = true;
                numCrcErrors = 0;
                testResult = TEST_OK;
            } else {
                psu::event_queue::pushEvent(psu::event_queue::EVENT_ERROR_SLOT1_SYNC_ERROR + slotIndex);
                testResult = TEST_FAILED;
            }
        }
    }

    void tick() override {
        if (!synchronized) {
            return;
        }

        output[0] = inputPinStates;
        output[1] = outputPinStates;

        if (bp3c::comm::transfer(slotIndex, output, input, BUFFER_SIZE)) {
            numCrcErrors = 0;

            inputPinStates = input[0];
        } else {
            if (++numCrcErrors >= 100) {
                psu::event_queue::pushEvent(psu::event_queue::EVENT_ERROR_SLOT1_CRC_CHECK_ERROR + slotIndex);
                synchronized = false;
                testResult = TEST_FAILED;
            } else {
                DebugTrace("Slot %d CRC %d\n", slotIndex + 1, numCrcErrors);
            }
        }
    }

    void onPowerDown() override {
        synchronized = false;
    }

    int getInputPinState(int pin) {
        return inputPinStates & (1 << pin) ? 1 : 0;
    }

    int getOutputPinState(int pin) {
        return outputPinStates & (1 << pin) ? 1 : 0;
    }

    void setOutputPinState(int pin, int state) {
        if (state) {
            outputPinStates |= 1 << pin;
        } else {
            outputPinStates &= ~(1 << pin);
        }
    }
};

Module *Mio168ModuleInfo::createModule(uint8_t slotIndex, uint16_t moduleRevision) {
    return new Mio168Module(slotIndex, this, moduleRevision);
}

static Mio168ModuleInfo g_mio168ModuleInfo;
ModuleInfo *g_moduleInfo = &g_mio168ModuleInfo;

} // namespace dib_mio168

namespace gui {

void data_mio168_inputs(DataOperationEnum operation, Cursor cursor, Value &value) {
    if (operation == DATA_OPERATION_COUNT) {
        value = 8;
    } else if (operation == DATA_OPERATION_GET_CURSOR_VALUE) {
        value = hmi::g_selectedSlotIndex * 8 + value.getInt();
    }
}

void data_mio168_input_no(DataOperationEnum operation, Cursor cursor, Value &value) {
    if (operation == DATA_OPERATION_GET) {
        value = cursor % 8 + 1;
    }
}

void data_mio168_input_state(DataOperationEnum operation, Cursor cursor, Value &value) {
    if (operation == DATA_OPERATION_GET) {
        auto mio168Module = (dib_mio168::Mio168Module *)g_slots[cursor / 8];
        value = mio168Module->getInputPinState(cursor % 8);
    }
}

void data_mio168_outputs(DataOperationEnum operation, Cursor cursor, Value &value) {
    if (operation == DATA_OPERATION_COUNT) {
        value = 8;
    } else if (operation == DATA_OPERATION_GET_CURSOR_VALUE) {
        value = hmi::g_selectedSlotIndex * 8 + value.getInt();
    }
}

void data_mio168_output_no(DataOperationEnum operation, Cursor cursor, Value &value) {
    if (operation == DATA_OPERATION_GET) {
        value = cursor % 8 + 1;
    }
}

void data_mio168_output_state(DataOperationEnum operation, Cursor cursor, Value &value) {
    if (operation == DATA_OPERATION_GET) {
        auto mio168Module = (dib_mio168::Mio168Module *)g_slots[cursor / 8];
        value = mio168Module->getOutputPinState(cursor % 8);
    }
}

void action_mio168_toggle_output_state() {
    int cursor = getFoundWidgetAtDown().cursor;
    auto mio168Module = (dib_mio168::Mio168Module *)g_slots[cursor / 8];
    mio168Module->setOutputPinState(cursor % 8, !mio168Module->getOutputPinState(cursor % 8));
}

} // namespace gui

} // namespace eez
