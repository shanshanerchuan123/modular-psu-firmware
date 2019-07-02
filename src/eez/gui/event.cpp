/*
 * EEZ Generic Firmware
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

#include <eez/gui/event.h>

#include <stdint.h>

#include <eez/gui/app_context.h>
#include <eez/gui/gui.h>
#include <eez/gui/touch.h>
#include <eez/gui/update.h>
#include <eez/system.h>

// TODO this must be removed from here
#include <eez/apps/psu/psu.h>

#include <eez/apps/psu/idle.h>
#include <eez/apps/psu/persist_conf.h>
//

#define CONF_GUI_LONG_TOUCH_TIMEOUT 1000000L        // 1s
#define CONF_GUI_KEYPAD_AUTO_REPEAT_DELAY 250000L   // 250ms
#define CONF_GUI_EXTRA_LONG_TOUCH_TIMEOUT 30000000L // 30s

namespace eez {
namespace gui {

static WidgetCursor m_foundWidgetAtDown;
static WidgetCursor m_activeWidget;
static bool m_touchActionExecuted;
static bool m_touchActionExecutedAtDown;
static OnTouchFunctionType m_onTouchFunction;
static uint32_t m_touchDownTime;
static uint32_t m_lastAutoRepeatEventTime;
static bool m_longTouchGenerated;
static bool m_extraLongTouchGenerated;
static int m_lastTouchMoveX = -1;
static int m_lastTouchMoveY = -1;

WidgetCursor &getFoundWidgetAtDown() {
    return m_foundWidgetAtDown;
}

bool isActiveWidget(const WidgetCursor &widgetCursor) {
    if (widgetCursor.appContext->isActiveWidget(widgetCursor)) {
        return true;
    }

    return widgetCursor == m_activeWidget;
}

int getAction(const WidgetCursor &widgetCursor) {
    return (int16_t)widgetCursor.widget->action;
}

void onWidgetDefaultTouch(const WidgetCursor &widgetCursor, Event &touchEvent) {
    if (touchEvent.type == EVENT_TYPE_TOUCH_DOWN) {
        m_touchActionExecuted = false;
        m_touchActionExecutedAtDown = false;

        int action = getAction(widgetCursor);
        if (widgetCursor.appContext->testExecuteActionOnTouchDown(action)) {
            executeAction(action);
            m_touchActionExecutedAtDown = true;
        } else {
            m_activeWidget = widgetCursor;
        }
    } else if (touchEvent.type == EVENT_TYPE_AUTO_REPEAT) {
        int action = getAction(widgetCursor);
        if (action == ACTION_ID_KEYPAD_BACK || action == ACTION_ID_EVENT_QUEUE_PREVIOUS_PAGE ||
            action == ACTION_ID_EVENT_QUEUE_NEXT_PAGE ||
            widgetCursor.appContext->isAutoRepeatAction(action)) {
            m_touchActionExecuted = true;
            executeAction(action);
        }
    } else if (touchEvent.type == EVENT_TYPE_TOUCH_UP) {
        if (!m_touchActionExecutedAtDown) {
            m_activeWidget = 0;
            if (!m_touchActionExecuted) {
                int action = getAction(widgetCursor);
                executeAction(action);
            }
        }
    }
}

void onPageTouch(const WidgetCursor &foundWidget, Event &touchEvent) {
    int activePageId = getActivePageId();

    if (touchEvent.type == EVENT_TYPE_LONG_TOUCH) {
        if (activePageId == INTERNAL_PAGE_ID_NONE || activePageId == PAGE_ID_STANDBY) {
            // wake up on long touch
            psu::changePowerState(true);
        } else if (activePageId == PAGE_ID_DISPLAY_OFF) {
            // turn ON display on long touch
            psu::persist_conf::setDisplayState(1);
        }
    } else if (touchEvent.type == EVENT_TYPE_EXTRA_LONG_TOUCH) {
        // start touch screen calibration in case of really long touch
        showPage(PAGE_ID_TOUCH_CALIBRATION_INTRO);
    }

    g_appContext->onPageTouch(foundWidget, touchEvent);
}

void onInternalPageTouch(const WidgetCursor &widgetCursor, Event &touchEvent) {
    if (touchEvent.type == EVENT_TYPE_TOUCH_DOWN) {
        if (m_foundWidgetAtDown) {
            m_activeWidget = m_foundWidgetAtDown;
        }
    } else if (touchEvent.type == EVENT_TYPE_TOUCH_UP) {
        if (m_foundWidgetAtDown) {
            m_activeWidget = 0;
            int action = getAction(m_foundWidgetAtDown);
            executeAction(action);
        } else {
        	InternalPage *page = (InternalPage *)widgetCursor.appContext->getActivePage();
			if (!pointInsideRect(touchEvent.x, touchEvent.y, page->x, page->y, page->width,
								 page->height)) {
				widgetCursor.appContext->popPage();
			}
        }
    }
}

OnTouchFunctionType getTouchFunction(const WidgetCursor &widgetCursor) {
    if (widgetCursor.appContext && widgetCursor.appContext->isActivePageInternal()) {
        return onInternalPageTouch;
    }

    if (widgetCursor) {
		if (widgetCursor.widget->action) {
			if (widgetCursor.appContext->isWidgetActionEnabled(widgetCursor)) {
				return onWidgetDefaultTouch;
			}
		} else {
			return g_onTouchFunctions[widgetCursor.widget->type];
		}
    }

    return nullptr;
}

void processTouchEvent(EventType type) {
    int x = touch::getX();
    int y = touch::getY();

    if (type == EVENT_TYPE_TOUCH_DOWN) {
        m_lastTouchMoveX = x;
        m_lastTouchMoveX = y;
    } else if (type == EVENT_TYPE_TOUCH_MOVE) {
        // ignore EVENT_TYPE_TOUCH_MOVE if it is the same as the last event
        if (m_lastTouchMoveX == x && m_lastTouchMoveY == y) {
            return;
        }
        m_lastTouchMoveX = x;
        m_lastTouchMoveX = y;
    }

    if (type == EVENT_TYPE_TOUCH_DOWN) {
        m_foundWidgetAtDown = findWidget(x, y);

        m_onTouchFunction = getTouchFunction(m_foundWidgetAtDown);

        if (!m_onTouchFunction) {
            m_onTouchFunction = onPageTouch;
        }
    }

    if (type == EVENT_TYPE_TOUCH_UP) {
        if (m_activeWidget) {
            // deactivate active widget and make sure screen is updated
            m_activeWidget = 0;
            updateScreen();
            mcu::display::sync(true);
        }
    }

    if (m_onTouchFunction) {
        AppContext *saved = g_appContext;
        if (m_foundWidgetAtDown) {
            g_appContext = m_foundWidgetAtDown.appContext;
        }

        Event event;
        event.type = type;
        event.x = x;
        event.y = y;

        m_onTouchFunction(m_foundWidgetAtDown, event);

        g_appContext = saved;
    }
}

void touchHandling() {
    using namespace eez::gui::touch;

    if (g_eventType != EVENT_TYPE_TOUCH_NONE) {
        psu::idle::noteGuiActivity();
    }

    uint32_t tickCount = micros();

    if (g_eventType == EVENT_TYPE_TOUCH_DOWN) {
        m_touchDownTime = tickCount;
        m_lastAutoRepeatEventTime = tickCount;
        m_longTouchGenerated = false;
        m_extraLongTouchGenerated = false;
        processTouchEvent(EVENT_TYPE_TOUCH_DOWN);
    } else if (g_eventType == EVENT_TYPE_TOUCH_MOVE) {
        processTouchEvent(EVENT_TYPE_TOUCH_MOVE);

        if (!m_longTouchGenerated &&
            int32_t(tickCount - m_touchDownTime) >= CONF_GUI_LONG_TOUCH_TIMEOUT) {
            m_longTouchGenerated = true;
            processTouchEvent(EVENT_TYPE_LONG_TOUCH);
        }

        if (m_longTouchGenerated && !m_extraLongTouchGenerated &&
            int32_t(tickCount - m_touchDownTime) >= CONF_GUI_EXTRA_LONG_TOUCH_TIMEOUT) {
            m_extraLongTouchGenerated = true;
            processTouchEvent(EVENT_TYPE_EXTRA_LONG_TOUCH);
        }

        if (int32_t(tickCount - m_lastAutoRepeatEventTime) >= CONF_GUI_KEYPAD_AUTO_REPEAT_DELAY) {
            processTouchEvent(EVENT_TYPE_AUTO_REPEAT);
            m_lastAutoRepeatEventTime = tickCount;
        }

    } else if (g_eventType == EVENT_TYPE_TOUCH_UP) {
        processTouchEvent(EVENT_TYPE_TOUCH_UP);
    }
}

void eventHandling() {
    touchHandling();
}

} // namespace gui
} // namespace eez