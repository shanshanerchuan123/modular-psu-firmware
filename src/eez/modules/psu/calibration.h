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

namespace eez {
namespace psu {
/// Channel calibration procedure.
namespace calibration {

enum CalibrationValueType {
    CALIBRATION_VALUE_U,
    CALIBRATION_VALUE_I_HI_RANGE,
    CALIBRATION_VALUE_I_LOW_RANGE
};

/// Calibration parameters for the voltage or current during calibration procedure.
struct Value {
    CalibrationValueType type;
    int currentPointIndex;
    bool isPointSet[MAX_CALIBRATION_POINTS];
    Channel::CalibrationValueConfiguration configuration;

    Value(CalibrationValueType type);

    void reset();

    void setCurrentPointIndex(int currentPointIndex);

    void setDacValue(float value);
    float getDacValue();

    float readAdcValue();

    bool checkValueAndAdc(float value, float adc);
    void setValueAndAdc(float value, float adc);

    bool checkPoints();
};

bool isEnabled();
Channel &getCalibrationChannel();

/// Start calibration procedure on the channel.
/// /param channel Selected channel
void start(Channel &channel);

/// Stop calibration procedure.
void stop();

void copyValuesFromChannel();

void selectCurrentRange(int8_t range);

Value &getVoltage();
Value &getCurrent();

/// Is calibration remark is set.
bool isRemarkSet();

/// Get currently set remark.
const char *getRemark();

/// Set calibration remark.
void setRemark(const char *value, size_t len);

/// Are all calibration parameters entered?
bool canSave(int16_t &scpiErr, int16_t *uiErr = nullptr);

/// Save calibration parameters entered during calibration procedure.
bool save();

/// Clear calibration parameters for the currently selected channel.
/// /param channel Selected channel
bool clear(Channel *channel);

} // namespace calibration
} // namespace psu
} // namespace eez
