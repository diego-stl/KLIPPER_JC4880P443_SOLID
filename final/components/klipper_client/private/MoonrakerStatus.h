#pragma once
#include <stdint.h>
#include <float.h>
#include "PrintTuning.h"

// El servidor envia deltas: un campo ausente conserva su valor; cero es un dato.
template <typename T> struct StatusField {
    bool present = false;
    T value{};
};

template <typename Text> struct MoonrakerStatusDelta {
    StatusField<float> hotendTemp, hotendTarget, bedTemp, bedTarget, progress;
    StatusField<uint32_t> printDuration;
    StatusField<Text> filename, printState, klippyState, stateMessage;
    PrintTuning::Field speedPercent, flowPercent, fanPercent;
};

template <typename Text> struct MoonrakerStatus {
    float hotendTemp = 0, hotendTarget = 0, bedTemp = 0, bedTarget = 0;
    float progress = 0; // fraccion 0..1 en Moonraker
    bool speedFactorValid = false, flowFactorValid = false, fanSpeedValid = false;
    float speedPercent = 0, flowPercent = 0, fanPercent = 0;
    uint32_t printDuration = 0, timeRemaining = 0;
    Text filename{}, jobState = "standby", klippyState = "startup", stateMessage{};

    constexpr void apply(const MoonrakerStatusDelta<Text>& delta) {
        if (delta.filename.present && delta.filename.value != filename) {
            filename = delta.filename.value;
            progress = 0;
            printDuration = 0;
        }
        const auto assign = [](float& target, const StatusField<float>& field) {
            if (field.present && field.value >= -FLT_MAX && field.value <= FLT_MAX) target = field.value;
        };
        assign(hotendTemp, delta.hotendTemp);
        assign(hotendTarget, delta.hotendTarget);
        assign(bedTemp, delta.bedTemp);
        assign(bedTarget, delta.bedTarget);
        const auto tuning = [](bool &valid, float &value, const PrintTuning::Field &field) {
            if (field.present) { valid = field.valid; value = field.valid ? field.percent : 0; }
        };
        tuning(speedFactorValid, speedPercent, delta.speedPercent);
        tuning(flowFactorValid, flowPercent, delta.flowPercent);
        tuning(fanSpeedValid, fanPercent, delta.fanPercent);
        if (delta.progress.present && delta.progress.value >= 0 && delta.progress.value <= 1) progress = delta.progress.value;
        if (delta.printDuration.present) printDuration = delta.printDuration.value;
        if (delta.printState.present) jobState = delta.printState.value;
        if (delta.klippyState.present) klippyState = delta.klippyState.value;
        if (delta.stateMessage.present) stateMessage = delta.stateMessage.value;
        // Recalcular con estado acumulado, aunque duracion y progreso lleguen separados.
        const double remaining = progress > 0.005f ? double(printDuration) * (1.0 / progress - 1.0) : 0;
        timeRemaining = remaining >= UINT32_MAX ? UINT32_MAX : remaining > 0 ? uint32_t(remaining) : 0;
    }

    constexpr Text printState() const {
        return klippyState == "shutdown" || klippyState == "error" || klippyState == "startup" ? klippyState : jobState;
    }

    constexpr Text errorMessage() const {
        return klippyState == "ready" ? Text{} : stateMessage;
    }
};
