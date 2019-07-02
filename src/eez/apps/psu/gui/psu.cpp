/*
* EEZ PSU Firmware
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

#if OPTION_DISPLAY

#include <eez/apps/psu/psu.h>

#include <eez/apps/psu/calibration.h>
#include <eez/apps/psu/channel_dispatcher.h>
#include <eez/apps/psu/devices.h>
#include <eez/apps/psu/event_queue.h>
#include <eez/apps/psu/gui/data.h>
#include <eez/apps/psu/gui/edit_mode.h>
#include <eez/apps/psu/gui/edit_mode_keypad.h>
#include <eez/apps/psu/gui/edit_mode_slider.h>
#include <eez/apps/psu/gui/edit_mode_step.h>
#include <eez/apps/psu/gui/numeric_keypad.h>
#include <eez/apps/psu/gui/page_ch_settings_adv.h>
#include <eez/apps/psu/gui/page_ch_settings_protection.h>
#include <eez/apps/psu/gui/page_ch_settings_trigger.h>
#include <eez/apps/psu/gui/page_event_queue.h>
#include <eez/apps/psu/gui/page_self_test_result.h>
#include <eez/apps/psu/gui/page_sys_settings.h>
#include <eez/apps/psu/gui/page_user_profiles.h>
#include <eez/apps/psu/gui/password.h>
#include <eez/apps/psu/gui/psu.h>
#include <eez/apps/psu/idle.h>
#include <eez/apps/psu/temperature.h>
#include <eez/apps/psu/trigger.h>
#include <eez/gui/dialogs.h>
#include <eez/gui/document.h>
#include <eez/gui/gui.h>
#include <eez/gui/touch.h>
#include <eez/modules/mcu/display.h>
#include <eez/sound.h>
#include <eez/system.h>

#if OPTION_ENCODER
#include <eez/modules/mcu/encoder.h>
#endif

#if OPTION_SD_CARD
#include <eez/apps/psu/dlog.h>
#endif

#define CONF_DLOG_COLOR 62464

namespace eez {
namespace psu {
namespace gui {

PsuAppContext g_psuAppContext;

static persist_conf::DeviceFlags2 g_deviceFlags2;
static bool g_showSetupWizardQuestionCalled;
Channel *g_channel;
static WidgetCursor g_toggleOutputWidgetCursor;

bool showSetupWizardQuestion();
void onEncoder(int tickCount, int counter, bool clicked);

////////////////////////////////////////////////////////////////////////////////

PsuAppContext::PsuAppContext() {
    showPage(PAGE_ID_MAIN);
}

void PsuAppContext::stateManagment() {
    AppContext::stateManagment();

    // TODO move this to some other place
#if OPTION_ENCODER
    uint32_t tickCount = micros();

    int counter;
    bool clicked;
    mcu::encoder::read(tickCount, counter, clicked);
    if (counter != 0 || clicked) {
        idle::noteEncoderActivity();
    }
    onEncoder(tickCount, counter, clicked);
#endif

    int activePageId = getActivePageId();

#if GUI_BACK_TO_MAIN_ENABLED
    uint32_t inactivityPeriod = psu::idle::getGuiAndEncoderInactivityPeriod();

    if (activePageId == PAGE_ID_EVENT_QUEUE || activePageId == PAGE_ID_USER_PROFILES ||
        activePageId == PAGE_ID_USER_PROFILES2 || activePageId == PAGE_ID_USER_PROFILE_0_SETTINGS ||
        activePageId == PAGE_ID_USER_PROFILE_SETTINGS) {
        if (inactivityPeriod >= GUI_BACK_TO_MAIN_DELAY * 1000UL) {
            showPage(PAGE_ID_MAIN);
        }
    }
#endif

    //
    if (g_rprogAlarm) {
        g_rprogAlarm = false;
        longErrorMessage("Max. remote prog. voltage exceeded.", "Please remove it immediately!");
    }

    // show startup wizard
    if (!isFrontPanelLocked() && activePageId == PAGE_ID_MAIN &&
        int32_t(tickCount - getShowPageTime()) >= 50000L) {
        if (showSetupWizardQuestion()) {
            return;
        }
    }
}

int PsuAppContext::getMainPageId() {
    return PAGE_ID_MAIN;
}

void PsuAppContext::onPageChanged() {
    AppContext::onPageChanged();

    g_focusEditValue = data::Value();
}

bool PsuAppContext::isFocusWidget(const WidgetCursor &widgetCursor) {
    if (isPageActiveOrOnStack(PAGE_ID_CH_SETTINGS_LISTS)) {
        return ((ChSettingsListsPage *)getActivePage())->isFocusWidget(widgetCursor);
    }

    // TODO this is not valid, how can we know cursor.i is channels index and not index of some other collection?
    int iChannel = widgetCursor.cursor.i >= 0 ? widgetCursor.cursor.i
                                              : (g_channel ? (g_channel->index - 1) : 0);
    if (iChannel >= 0 && iChannel < CH_NUM) {
		if (channel_dispatcher::getVoltageTriggerMode(Channel::get(iChannel)) != TRIGGER_MODE_FIXED &&
			!trigger::isIdle()) {
			return false;
		}
    }

    if (calibration::isEnabled()) {
        return false;
    }

    return (widgetCursor.cursor == -1 || widgetCursor.cursor == g_focusCursor) &&
           widgetCursor.widget->data == g_focusDataId;
}

bool PsuAppContext::isAutoRepeatAction(int action) {
    return action == ACTION_ID_CHANNEL_LISTS_PREVIOUS_PAGE ||
           action == ACTION_ID_CHANNEL_LISTS_NEXT_PAGE;
}

void PsuAppContext::onPageTouch(const WidgetCursor &foundWidget, Event &touchEvent) {
    int activePageId = getActivePageId();

    if (touchEvent.type == EVENT_TYPE_TOUCH_DOWN) {
        if (activePageId == PAGE_ID_EDIT_MODE_SLIDER) {
            edit_mode_slider::onTouchDown();
        } else if (activePageId == PAGE_ID_EDIT_MODE_STEP) {
            edit_mode_step::onTouchDown();
        }
    } else if (touchEvent.type == EVENT_TYPE_TOUCH_MOVE) {
        if (activePageId == PAGE_ID_EDIT_MODE_SLIDER) {
            edit_mode_slider::onTouchMove();
        } else if (activePageId == PAGE_ID_EDIT_MODE_STEP) {
            edit_mode_step::onTouchMove();
        }
    } else if (touchEvent.type == EVENT_TYPE_TOUCH_UP) {
        if (activePageId == PAGE_ID_EDIT_MODE_SLIDER) {
            edit_mode_slider::onTouchUp();
        } else if (activePageId == PAGE_ID_EDIT_MODE_STEP) {
            edit_mode_step::onTouchUp();
        }
    }
}

bool PsuAppContext::testExecuteActionOnTouchDown(int action) {
    return action == ACTION_ID_CHANNEL_TOGGLE_OUTPUT;
}

uint16_t PsuAppContext::getWidgetBackgroundColor(const WidgetCursor &widgetCursor,
                                                 const Style *style) {
#if OPTION_SD_CARD
    const Widget *widget = widgetCursor.widget;
    int iChannel = widgetCursor.cursor.i >= 0 ? widgetCursor.cursor.i
                                              : (g_channel ? (g_channel->index - 1) : 0);
    if (widget->data == DATA_ID_CHANNEL_U_EDIT || widget->data == DATA_ID_CHANNEL_U_MON_DAC) {
        if (dlog::g_logVoltage[iChannel]) {
            return CONF_DLOG_COLOR;
        }
    } else if (widget->data == DATA_ID_CHANNEL_I_EDIT) {
        if (dlog::g_logCurrent[iChannel]) {
            return CONF_DLOG_COLOR;
        }
    } else if (widget->data == DATA_ID_CHANNEL_P_MON) {
        if (dlog::g_logPower[iChannel]) {
            return CONF_DLOG_COLOR;
        }
    }
#endif

    return AppContext::getWidgetBackgroundColor(widgetCursor, style);
}

bool PsuAppContext::isBlinking(const data::Cursor &cursor, uint16_t id) {
    if ((g_focusCursor == cursor || channel_dispatcher::isCoupled()) && g_focusDataId == id &&
        g_focusEditValue.getType() != VALUE_TYPE_NONE) {
        return true;
    }

    return AppContext::isBlinking(cursor, id);
}

void PsuAppContext::onScaleUpdated(int dataId, bool scaleIsVertical, int scaleWidth,
                                   float scaleHeight) {
    if (dataId == DATA_ID_EDIT_VALUE) {
        edit_mode_slider::scale_is_vertical = scaleIsVertical;
        edit_mode_slider::scale_width = scaleWidth;
        edit_mode_slider::scale_height = scaleHeight;
    }
}

int PsuAppContext::getNumHistoryValues(uint16_t id) {
    return CHANNEL_HISTORY_SIZE;
}

int PsuAppContext::getCurrentHistoryValuePosition(const Cursor &cursor, uint16_t id) {
    int iChannel = cursor.i >= 0 ? cursor.i : (g_channel ? (g_channel->index - 1) : 0);
    return Channel::get(iChannel).getCurrentHistoryValuePosition();
}

Value PsuAppContext::getHistoryValue(const Cursor &cursor, uint16_t id, int position) {
    Value value = position;
    g_dataOperationsFunctions[id](data::DATA_OPERATION_GET_HISTORY_VALUE, (Cursor &)cursor, value);
    return value;
}

////////////////////////////////////////////////////////////////////////////////

bool isChannelCalibrationsDone() {
    for (int i = 0; i < CH_NUM; ++i) {
        if (!Channel::get(i).isCalibrationExists()) {
            return false;
        }
    }
    return true;
}

bool isDateTimeSetupDone() {
    return persist_conf::devConf.flags.dateValid && persist_conf::devConf.flags.timeValid;
}

void channelCalibrationsYes() {
    executeAction(ACTION_ID_SHOW_SYS_SETTINGS_CAL);
}

void channelCalibrationsNo() {
    persist_conf::devConf2.flags.skipChannelCalibrations = 1;
    persist_conf::saveDevice2();
}

void dateTimeYes() {
    executeAction(ACTION_ID_SHOW_SYS_SETTINGS_DATE_TIME);
}

void dateTimeNo() {
    persist_conf::devConf2.flags.skipDateTimeSetup = 1;
    persist_conf::saveDevice2();
}

void serialYes() {
    executeAction(ACTION_ID_SHOW_SYS_SETTINGS_SERIAL);
}

void serialNo() {
    persist_conf::devConf2.flags.skipSerialSetup = 1;
    persist_conf::saveDevice2();
}

void ethernetYes() {
    executeAction(ACTION_ID_SHOW_SYS_SETTINGS_ETHERNET);
}

void ethernetNo() {
    persist_conf::devConf2.flags.skipEthernetSetup = 1;
    persist_conf::saveDevice2();
}

bool showSetupWizardQuestion() {
    if (!g_showSetupWizardQuestionCalled) {
        g_showSetupWizardQuestionCalled = true;
        g_deviceFlags2 = persist_conf::devConf2.flags;
    }

    if (!channel_dispatcher::isCoupled() && !channel_dispatcher::isTracked()) {
        if (!g_deviceFlags2.skipChannelCalibrations) {
            g_deviceFlags2.skipChannelCalibrations = 1;
            if (!isChannelCalibrationsDone()) {
                yesNoLater("Do you want to calibrate channels?", channelCalibrationsYes,
                           channelCalibrationsNo);
                return true;
            }
        }
    }

    if (!g_deviceFlags2.skipSerialSetup) {
        g_deviceFlags2.skipSerialSetup = 1;
        if (!persist_conf::isSerialEnabled()) {
            yesNoLater("Do you want to setup serial port?", serialYes, serialNo);
            return true;
        }
    }

#if OPTION_ETHERNET
    if (!g_deviceFlags2.skipEthernetSetup) {
        g_deviceFlags2.skipEthernetSetup = 1;
        if (!persist_conf::isEthernetEnabled()) {
            yesNoLater("Do you want to setup ethernet?", ethernetYes, ethernetNo);
            return true;
        }
    }
#endif

    if (!g_deviceFlags2.skipDateTimeSetup) {
        g_deviceFlags2.skipDateTimeSetup = 1;
        if (!isDateTimeSetupDone()) {
            yesNoLater("Do you want to set date and time?", dateTimeYes, dateTimeNo);
            return true;
        }
    }

    return false;
}

void changeLimit(Channel &channel, const data::Value &value, float minLimit, float maxLimit,
                 float defLimit, void (*onSetLimit)(float)) {
    NumericKeypadOptions options;

    options.channelIndex = channel.index;

    options.editValueUnit = value.getUnit();

    options.min = minLimit;
    options.max = maxLimit;
    options.def = defLimit;

    options.enableMaxButton();
    options.enableDefButton();
    options.flags.signButtonEnabled = true;
    options.flags.dotButtonEnabled = true;

    NumericKeypad::start(0, value, options, onSetLimit, 0, 0);
}

// TODO find a better way to pass this argument to onSet[Voltahe|Current|Power]Limit function
static int g_iChannelSetLimit;

void onSetVoltageLimit(float limit) {
    Channel &channel = Channel::get(g_iChannelSetLimit);
    channel_dispatcher::setVoltageLimit(channel, limit);
    infoMessageP("Voltage limit changed!", popPage);
}

void changeVoltageLimit(int iChannel) {
    g_iChannelSetLimit = iChannel;
    Channel &channel = Channel::get(iChannel);
    float minLimit = channel_dispatcher::getUMin(channel);
    float maxLimit = channel_dispatcher::getUMax(channel);
    float defLimit = channel_dispatcher::getUMax(channel);
    changeLimit(channel,
                MakeValue(channel_dispatcher::getULimit(channel), UNIT_VOLT, channel.index - 1),
                minLimit, maxLimit, defLimit, onSetVoltageLimit);
}

void onSetCurrentLimit(float limit) {
    Channel &channel = Channel::get(g_iChannelSetLimit);
    channel_dispatcher::setCurrentLimit(channel, limit);
    infoMessageP("Current limit changed!", popPage);
}

void changeCurrentLimit(int iChannel) {
    g_iChannelSetLimit = iChannel;
    Channel &channel = Channel::get(iChannel);
    float minLimit = channel_dispatcher::getIMin(channel);
    float maxLimit = channel_dispatcher::getIMax(channel);
    float defLimit = channel_dispatcher::getIMax(channel);
    changeLimit(channel,
                MakeValue(channel_dispatcher::getILimit(channel), UNIT_AMPER, channel.index - 1),
                minLimit, maxLimit, defLimit, onSetCurrentLimit);
}

void onSetPowerLimit(float limit) {
    Channel &channel = Channel::get(g_iChannelSetLimit);
    channel_dispatcher::setPowerLimit(channel, limit);
    infoMessageP("Power limit changed!", popPage);
}

void changePowerLimit(int iChannel) {
    g_iChannelSetLimit = iChannel;
    Channel &channel = Channel::get(iChannel);
    float minLimit = channel_dispatcher::getPowerMinLimit(channel);
    float maxLimit = channel_dispatcher::getPowerMaxLimit(channel);
    float defLimit = channel_dispatcher::getPowerDefaultLimit(channel);
    changeLimit(channel,
                MakeValue(channel_dispatcher::getPowerLimit(channel), UNIT_WATT, channel.index - 1),
                minLimit, maxLimit, defLimit, onSetPowerLimit);
}

void psuErrorMessage(const data::Cursor &cursor, data::Value value, void (*ok_callback)()) {
    int errorPageId = PAGE_ID_ERROR_ALERT;
    void (*action)(int param) = 0;
    const char *actionLabel = 0;
    int actionParam = 0;

    if (value.getType() == VALUE_TYPE_SCPI_ERROR) {

        int iChannel = cursor.i >= 0 ? cursor.i : (g_channel ? (g_channel->index - 1) : 0);
        Channel &channel = Channel::get(iChannel);

        if (value.getScpiError() == SCPI_ERROR_VOLTAGE_LIMIT_EXCEEDED) {
            if (channel_dispatcher::getULimit(channel) <
                channel_dispatcher::getUMaxLimit(channel)) {
                action = changeVoltageLimit;
                actionLabel = "Change voltage limit";
                actionParam = iChannel;
            } else {
                errorPageId = PAGE_ID_ERROR_TOAST_ALERT;
            }
        } else if (value.getScpiError() == SCPI_ERROR_CURRENT_LIMIT_EXCEEDED) {
            if (channel_dispatcher::getILimit(channel) <
                channel_dispatcher::getIMaxLimit(channel)) {
                action = changeCurrentLimit;
                actionLabel = "Change current limit";
                actionParam = iChannel;
            } else {
                errorPageId = PAGE_ID_ERROR_TOAST_ALERT;
            }
        } else if (value.getScpiError() == SCPI_ERROR_POWER_LIMIT_EXCEEDED) {
            if (channel_dispatcher::getPowerLimit(channel) <
                channel_dispatcher::getPowerMaxLimit(channel)) {
                action = changePowerLimit;
                actionLabel = "Change power limit";
                actionParam = iChannel;
            } else {
                errorPageId = PAGE_ID_ERROR_TOAST_ALERT;
            }
        }
    }

    errorMessageWithAction(errorPageId, value, ok_callback, action, actionLabel, actionParam);
    sound::playBeep();
}

////////////////////////////////////////////////////////////////////////////////

data::Cursor g_focusCursor = Cursor(0);
uint16_t g_focusDataId = DATA_ID_CHANNEL_U_EDIT;
data::Value g_focusEditValue;

void setFocusCursor(const data::Cursor &cursor, uint16_t dataId) {
    g_focusCursor = cursor;
    g_focusDataId = dataId;
    g_focusEditValue = data::Value();
}

bool isFocusChanged() {
    return g_focusEditValue.getType() != VALUE_TYPE_NONE;
}

////////////////////////////////////////////////////////////////////////////////

#if OPTION_ENCODER

static bool g_isEncoderEnabledInActivePage;
uint32_t g_focusEditValueChangedTime;

bool isEncoderEnabledForWidget(const Widget *widget) {
    return widget->action == ACTION_ID_EDIT;
}

bool isEnabledFocusCursor(data::Cursor &cursor, uint16_t dataId) {
    int iChannel = cursor.i >= 0 ? cursor.i : (g_channel ? (g_channel->index - 1) : 0);
    if (iChannel < 0 || iChannel >= CH_NUM) {
    	return false;
    }
    Channel &channel = Channel::get(iChannel);
    return channel.isOk() &&
           (channel_dispatcher::getVoltageTriggerMode(channel) == TRIGGER_MODE_FIXED ||
            trigger::isIdle()) &&
           !(dataId == DATA_ID_CHANNEL_U_EDIT && channel.isRemoteProgrammingEnabled());
}

void isEncoderEnabledInActivePageCheckWidget(const WidgetCursor &widgetCursor) {
    if (isEncoderEnabledForWidget(widgetCursor.widget)) {
        g_isEncoderEnabledInActivePage = true;
    }
}

bool isEncoderEnabledInActivePage() {
    // encoder is enabled if active page contains widget with "edit" action
    g_isEncoderEnabledInActivePage = false;
    enumWidgets(isEncoderEnabledInActivePageCheckWidget);
    return g_isEncoderEnabledInActivePage;
}

void moveToNextFocusCursor() {
    data::Cursor newCursor = g_focusCursor;
    uint16_t newDataId = g_focusDataId;

    for (int i = 0; i < CH_NUM * 2; ++i) {
        if (newDataId == DATA_ID_CHANNEL_U_EDIT) {
            newDataId = DATA_ID_CHANNEL_I_EDIT;
        } else {
            if (channel_dispatcher::isCoupled() || channel_dispatcher::isTracked()) {
                newCursor.i = 0;
            } else {
                newCursor.i = (newCursor.i + 1) % CH_NUM;
            }
            newDataId = DATA_ID_CHANNEL_U_EDIT;
        }

        if (isEnabledFocusCursor(newCursor, newDataId)) {
            setFocusCursor(newCursor, newDataId);
            if (edit_mode::isActive()) {
                edit_mode::update();
            }
            return;
        }
    }
}

bool onEncoderConfirmation() {
    if (edit_mode::isActive() && !edit_mode::isInteractiveMode() &&
        edit_mode::getEditValue() != edit_mode::getCurrentValue()) {
        edit_mode::nonInteractiveSet();
        return true;
    }
    return false;
}

void onEncoder(int tickCount, int counter, bool clicked) {
    // wait for confirmation of changed value ...
    if (isFocusChanged() &&
        tickCount - g_focusEditValueChangedTime >= ENCODER_CHANGE_TIMEOUT * 1000000L) {
        // ... on timeout discard changed value
        g_focusEditValue = data::Value();
    }

    if (!isEnabledFocusCursor(g_focusCursor, g_focusDataId)) {
        moveToNextFocusCursor();
    }

    if (counter != 0) {
        if (isFrontPanelLocked()) {
            return;
        }

        if (!isEnabledFocusCursor(g_focusCursor, g_focusDataId)) {
            moveToNextFocusCursor();
        }

        mcu::encoder::enableAcceleration(true);

        if (isEncoderEnabledInActivePage()) {
            data::Value value;
            if (persist_conf::devConf2.flags.encoderConfirmationMode &&
                g_focusEditValue.getType() != VALUE_TYPE_NONE) {
                value = g_focusEditValue;
            } else {
                value = data::getEditValue(g_focusCursor, g_focusDataId);
            }

            float newValue =
                value.getFloat() + (value.getUnit() == UNIT_AMPER ? 0.001f : 0.01f) * counter;

            float min = data::getMin(g_focusCursor, g_focusDataId).getFloat();
            if (newValue < min) {
                newValue = min;
            }

            float max = data::getMax(g_focusCursor, g_focusDataId).getFloat();
            if (newValue > max) {
                newValue = max;
            }

            if (persist_conf::devConf2.flags.encoderConfirmationMode) {
                g_focusEditValue =
                    MakeValue(newValue, value.getUnit(), g_focusCursor.i > 0 ? g_focusCursor.i : 0);
                g_focusEditValueChangedTime = micros();
            } else {
                int16_t error;
                if (!data::set(g_focusCursor, g_focusDataId,
                               MakeValue(newValue, value.getUnit(), g_focusCursor.i), &error)) {
                    psuErrorMessage(g_focusCursor, data::MakeScpiErrorValue(error));
                }
            }
        }

        int activePageId = getActivePageId();

        if (activePageId == PAGE_ID_EDIT_MODE_KEYPAD || activePageId == PAGE_ID_NUMERIC_KEYPAD) {
            if (((NumericKeypad *)getActiveKeypad())->onEncoder(counter)) {
                return;
            }
        }
#if defined(EEZ_PLATFORM_SIMULATOR)
        if (activePageId == PAGE_ID_NUMERIC_KEYPAD2) {
            if (((NumericKeypad *)getActiveKeypad())->onEncoder(counter)) {
                return;
            }
        }
#endif

        if (activePageId == PAGE_ID_EDIT_MODE_STEP) {
            edit_mode_step::onEncoder(counter);
            return;
        }

        mcu::encoder::enableAcceleration(true);

        if (activePageId == PAGE_ID_EDIT_MODE_SLIDER) {
            edit_mode_slider::increment(counter);
            return;
        }
    }

    if (clicked) {
        if (isEncoderEnabledInActivePage()) {
            if (isFocusChanged()) {
                // confirmation
                int16_t error;
                if (!data::set(g_focusCursor, g_focusDataId, g_focusEditValue, &error)) {
                    psuErrorMessage(g_focusCursor, data::MakeScpiErrorValue(error));
                } else {
                    g_focusEditValue = data::Value();
                }
            } else if (!onEncoderConfirmation()) {
                moveToNextFocusCursor();
            }

            sound::playClick();
        }

        int activePageId = getActivePageId();
        if (activePageId == PAGE_ID_EDIT_MODE_KEYPAD || activePageId == PAGE_ID_NUMERIC_KEYPAD) {
            ((NumericKeypad *)getActiveKeypad())->onEncoderClicked();
        }
#if defined(EEZ_PLATFORM_SIMULATOR)
        if (activePageId == PAGE_ID_NUMERIC_KEYPAD2) {
            ((NumericKeypad *)getActiveKeypad())->onEncoderClicked();
        }
#endif
    }

    Page *activePage = getActivePage();
    if (activePage) {
        if (counter) {
            activePage->onEncoder(counter);
        }

        if (clicked) {
            activePage->onEncoderClicked();
        }
    }
}

#endif

void channelReinitiateTrigger() {
    trigger::abort();
    channelInitiateTrigger();
}

void channelToggleOutput() {
    Channel &channel =
        Channel::get(getFoundWidgetAtDown().cursor.i >= 0 ? getFoundWidgetAtDown().cursor.i : 0);
    if (channel_dispatcher::isTripped(channel)) {
        errorMessageP("Channel is tripped!");
    } else {
        bool triggerModeEnabled =
            channel_dispatcher::getVoltageTriggerMode(channel) != TRIGGER_MODE_FIXED ||
            channel_dispatcher::getCurrentTriggerMode(channel) != TRIGGER_MODE_FIXED;

        if (channel.isOutputEnabled()) {
            if (triggerModeEnabled) {
                trigger::abort();
                for (int i = 0; i < CH_NUM; ++i) {
                    Channel &channel = Channel::get(i);
                    if (channel_dispatcher::getVoltageTriggerMode(channel) != TRIGGER_MODE_FIXED ||
                        channel_dispatcher::getCurrentTriggerMode(channel) != TRIGGER_MODE_FIXED) {
                        channel_dispatcher::outputEnable(Channel::get(i), false);
                    }
                }
            } else {
                channel_dispatcher::outputEnable(channel, false);
            }
        } else {
            if (triggerModeEnabled) {
                if (trigger::isIdle()) {
                    g_toggleOutputWidgetCursor = getFoundWidgetAtDown();
                    pushPage(PAGE_ID_CH_START_LIST);
                } else if (trigger::isInitiated()) {
                    trigger::abort();
                    g_toggleOutputWidgetCursor = getFoundWidgetAtDown();
                    pushPage(PAGE_ID_CH_START_LIST);
                } else {
                    yesNoDialog(PAGE_ID_YES_NO, "Trigger is active. Re-initiate trigger?",
                                channelReinitiateTrigger, 0, 0);
                }
            } else {
                channel_dispatcher::outputEnable(channel, true);
            }
        }
    }
}

void channelInitiateTrigger() {
    popPage();
    int err = trigger::initiate();
    if (err == SCPI_RES_OK) {
        Channel &channel = Channel::get(
            g_toggleOutputWidgetCursor.cursor.i >= 0 ? g_toggleOutputWidgetCursor.cursor.i : 0);
        channel_dispatcher::outputEnable(channel, true);
    } else {
        psuErrorMessage(g_toggleOutputWidgetCursor.cursor, data::MakeScpiErrorValue(err));
    }
}

void channelSetToFixed() {
    popPage();

    Channel &channel = Channel::get(
        g_toggleOutputWidgetCursor.cursor.i >= 0 ? g_toggleOutputWidgetCursor.cursor.i : 0);
    if (channel_dispatcher::getVoltageTriggerMode(channel) != TRIGGER_MODE_FIXED) {
        channel_dispatcher::setVoltageTriggerMode(channel, TRIGGER_MODE_FIXED);
    }
    if (channel_dispatcher::getCurrentTriggerMode(channel) != TRIGGER_MODE_FIXED) {
        channel_dispatcher::setCurrentTriggerMode(channel, TRIGGER_MODE_FIXED);
    }
    channel_dispatcher::outputEnable(channel, true);
}

void channelEnableOutput() {
    popPage();
    Channel &channel = Channel::get(
        g_toggleOutputWidgetCursor.cursor.i >= 0 ? g_toggleOutputWidgetCursor.cursor.i : 0);
    channel_dispatcher::outputEnable(channel, true);
}

void selectChannel() {
    if (getFoundWidgetAtDown().cursor.i >= 0) {
        g_channel = &Channel::get(getFoundWidgetAtDown().cursor.i);
    } else if (!g_channel || channel_dispatcher::isCoupled() || channel_dispatcher::isTracked()) {
        g_channel = &Channel::get(0);
    }
}

static bool isChannelTripLastEvent(int i, event_queue::Event &lastEvent) {
    if (lastEvent.eventId == (event_queue::EVENT_ERROR_CH1_OVP_TRIPPED + i * 3) ||
        lastEvent.eventId == (event_queue::EVENT_ERROR_CH1_OCP_TRIPPED + i * 3) ||
        lastEvent.eventId == (event_queue::EVENT_ERROR_CH1_OPP_TRIPPED + i * 3) ||
        lastEvent.eventId == (event_queue::EVENT_ERROR_CH1_OTP_TRIPPED + i)) {
        return Channel::get(i).isTripped();
    }

    return false;
}

void onLastErrorEventAction() {
    event_queue::Event lastEvent;
    event_queue::getLastErrorEvent(&lastEvent);

    if (lastEvent.eventId == event_queue::EVENT_ERROR_AUX_OTP_TRIPPED &&
        temperature::sensors[temp_sensor::AUX].isTripped()) {
        showPage(PAGE_ID_SYS_SETTINGS_AUX_OTP);
    } else if (isChannelTripLastEvent(0, lastEvent)) {
        g_channel = &Channel::get(0);
        showPage(PAGE_ID_CH_SETTINGS_PROT_CLEAR);
    } else if (isChannelTripLastEvent(1, lastEvent)) {
        g_channel = &Channel::get(1);
        showPage(PAGE_ID_CH_SETTINGS_PROT_CLEAR);
    } else {
        showPage(PAGE_ID_EVENT_QUEUE);
    }
}

} // namespace gui
} // namespace psu
} // namespace eez

#endif