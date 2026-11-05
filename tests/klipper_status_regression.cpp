// Ejecutar con el compilador del proyecto:
// xtensa-esp32s3-elf-g++ -std=gnu++20 -fsyntax-only tests/moonraker_status_regression.cpp
// Evalua el modelo utilizado por el firmware, sin red, UI ni impresora.
#include "../components/klipper_client/private/MoonrakerStatus.h"
#include <limits>
#include <string_view>

using Text = std::string_view;
using Status = MoonrakerStatus<Text>;
using Delta = MoonrakerStatusDelta<Text>;

constexpr Status initialSnapshot() {
    Status status;
    Delta snapshot;
    snapshot.hotendTemp = {true, 205};
    snapshot.hotendTarget = {true, 210};
    snapshot.bedTemp = {true, 58};
    snapshot.bedTarget = {true, 60};
    snapshot.filename = {true, "pieza.gcode"};
    snapshot.printState = {true, "printing"};
    snapshot.klippyState = {true, "ready"};
    snapshot.stateMessage = {true, "Printer is ready"};
    snapshot.progress = {true, 0.25f};
    snapshot.printDuration = {true, 1200};
    status.apply(snapshot);
    return status;
}

constexpr bool snapshotPopulatesAllFields() {
    const auto status = initialSnapshot();
    return status.hotendTemp == 205 && status.hotendTarget == 210 &&
        status.bedTemp == 58 && status.bedTarget == 60 &&
        status.filename == "pieza.gcode" && status.printState() == "printing" &&
        status.klippyState == "ready" && status.errorMessage().empty() &&
        status.progress == 0.25f && status.printDuration == 1200 &&
        status.timeRemaining == 3600;
}

constexpr bool partialDeltaPreservesMissingFields() {
    auto status = initialSnapshot();
    Delta delta;
    delta.hotendTemp = {true, 206};
    // Un valor en un campo ausente nunca debe sobrescribir el dato anterior.
    delta.filename = {false, "otro.gcode"};
    delta.hotendTarget = {false, 0};
    status.apply(delta);
    return status.hotendTemp == 206 && status.hotendTarget == 210 &&
        status.bedTemp == 58 && status.bedTarget == 60 &&
        status.filename == "pieza.gcode" && status.progress == 0.25f &&
        status.printDuration == 1200 && status.timeRemaining == 3600 &&
        status.printState() == "printing";
}

constexpr bool explicitZeroIsAnUpdate() {
    auto status = initialSnapshot();
    Delta delta;
    delta.hotendTemp = {true, 0};
    delta.hotendTarget = {true, 0};
    delta.bedTemp = {true, 0};
    delta.bedTarget = {true, 0};
    delta.progress = {true, 0};
    delta.printDuration = {true, 0};
    status.apply(delta);
    return status.hotendTemp == 0 && status.hotendTarget == 0 &&
        status.bedTemp == 0 && status.bedTarget == 0 &&
        status.progress == 0 && status.printDuration == 0 && status.timeRemaining == 0;
}

constexpr bool progressAndDurationArriveSeparately(bool durationFirst) {
    Status status;
    Delta progress, duration;
    progress.progress = {true, 0.25f};
    duration.printDuration = {true, 1200};
    status.apply(durationFirst ? duration : progress);
    status.apply(durationFirst ? progress : duration);
    return status.progress == 0.25f && status.printDuration == 1200 &&
        status.timeRemaining == 3600;
}

constexpr bool filenameChangeResetsPreviousEstimate() {
    auto status = initialSnapshot();
    Delta delta;
    delta.filename = {true, "nueva.gcode"};
    status.apply(delta);
    return status.filename == "nueva.gcode" && status.progress == 0 &&
        status.printDuration == 0 && status.timeRemaining == 0;
}

constexpr bool filenameInSnapshotAcceptsItsOwnProgress() {
    auto status = initialSnapshot();
    Delta delta;
    delta.filename = {true, "nueva.gcode"};
    delta.progress = {true, 0.5f};
    delta.printDuration = {true, 300};
    status.apply(delta);
    return status.filename == "nueva.gcode" && status.progress == 0.5f &&
        status.printDuration == 300 && status.timeRemaining == 300;
}

constexpr bool sameFilenamePreservesProgress() {
    auto status = initialSnapshot();
    Delta delta;
    delta.filename = {true, "pieza.gcode"};
    status.apply(delta);
    return status.progress == 0.25f && status.printDuration == 1200 &&
        status.timeRemaining == 3600;
}

constexpr bool faultOverridesJobStateAndReadyRestoresIt(Text fault) {
    auto status = initialSnapshot();
    Delta delta;
    delta.klippyState = {true, fault};
    delta.stateMessage = {true, "Detalle de Klipper"};
    status.apply(delta);
    if (status.printState() != fault || status.errorMessage() != "Detalle de Klipper") return false;
    delta = {};
    delta.klippyState = {true, "ready"};
    status.apply(delta);
    return status.printState() == "printing" && status.errorMessage().empty();
}

constexpr bool invalidFloatingValuesPreservePreviousData() {
    auto status = initialSnapshot();
    Delta delta;
    delta.hotendTemp = {true, std::numeric_limits<float>::quiet_NaN()};
    delta.hotendTarget = {true, std::numeric_limits<float>::infinity()};
    delta.bedTemp = {true, -std::numeric_limits<float>::infinity()};
    delta.progress = {true, std::numeric_limits<float>::quiet_NaN()};
    status.apply(delta);
    if (status.hotendTemp != 205 || status.hotendTarget != 210 ||
        status.bedTemp != 58 || status.progress != 0.25f || status.timeRemaining != 3600) return false;
    delta = {};
    delta.progress = {true, -0.1f};
    status.apply(delta);
    if (status.progress != 0.25f) return false;
    delta.progress = {true, 1.1f};
    status.apply(delta);
    return status.progress == 0.25f && status.timeRemaining == 3600;
}

constexpr bool etaBoundsAreSafe() {
    Status status;
    Delta delta;
    delta.progress = {true, 0.125f};
    delta.printDuration = {true, UINT32_MAX};
    status.apply(delta);
    if (status.timeRemaining != UINT32_MAX) return false;
    delta.progress = {true, 0.005f};
    status.apply(delta);
    if (status.timeRemaining != 0) return false;
    delta.progress = {true, 1};
    status.apply(delta);
    return status.timeRemaining == 0;
}

static_assert(snapshotPopulatesAllFields(), "La suscripcion inicial debe poblar el estado completo");
static_assert(partialDeltaPreservesMissingFields(), "Los campos ausentes conservan su estado");
static_assert(explicitZeroIsAnUpdate(), "Cero debe aplicarse como un valor valido");
static_assert(progressAndDurationArriveSeparately(true), "Recalcular ETA cuando llega progreso");
static_assert(progressAndDurationArriveSeparately(false), "Recalcular ETA cuando llega duracion");
static_assert(filenameChangeResetsPreviousEstimate(), "Un archivo nuevo no hereda la ETA anterior");
static_assert(filenameInSnapshotAcceptsItsOwnProgress(), "El snapshot nuevo conserva sus propios datos");
static_assert(sameFilenamePreservesProgress(), "Repetir filename no reinicia el progreso");
static_assert(faultOverridesJobStateAndReadyRestoresIt("shutdown"), "Shutdown precede al trabajo");
static_assert(faultOverridesJobStateAndReadyRestoresIt("error"), "Error precede al trabajo");
static_assert(faultOverridesJobStateAndReadyRestoresIt("startup"), "Startup precede al trabajo");
static_assert(invalidFloatingValuesPreservePreviousData(), "No publicar NaN, infinito o progreso fuera de rango");
static_assert(etaBoundsAreSafe(), "Evitar desbordamiento y ETA sin progreso suficiente");

