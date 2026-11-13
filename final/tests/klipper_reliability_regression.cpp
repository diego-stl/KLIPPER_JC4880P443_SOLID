// Ejecutar con el compilador del proyecto:
// xtensa-esp32s3-elf-g++ -std=gnu++20 -fsyntax-only tests/reliability_regression.cpp
// static_assert evalua el lector real con sockets y reloj simulados, sin placa.
#include "../components/klipper_client/private/KlipperTransfer.h"
#include "../components/klipper_client/private/CommandStatus.h"

struct MockStream {
    uint8_t source[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    size_t bytes = 8;
    size_t position = 0;
    int advertised = 100;
    int fragment = 8;
    bool failRead = false;

    constexpr int available() { return position < bytes ? advertised : 0; }
    constexpr int read(uint8_t* output, size_t length) {
        if (failRead) return -1;
        size_t count = length < bytes - position ? length : bytes - position;
        if (count > static_cast<size_t>(fragment)) count = fragment;
        for (size_t i = 0; i < count; ++i) output[i] = source[position++];
        return count;
    }
};

constexpr bool exactAndBounded() {
    MockStream stream;
    uint8_t guarded[6] = {99, 0, 0, 0, 0, 88};
    uint32_t now = 0;
    const bool ok = KlipperTransfer::readExact(stream, guarded + 1, 4,
        [] { return true; }, [&] { return now; }, [&] { ++now; }, 100);
    return ok && guarded[0] == 99 && guarded[5] == 88 && guarded[4] == 4 && stream.position == 4;
}

constexpr bool fragmented() {
    MockStream stream;
    stream.fragment = 1;
    uint8_t output[8] = {};
    uint32_t now = 0;
    const bool ok = KlipperTransfer::readExact(stream, output, 8,
        [] { return true; }, [&] { return now; }, [&] { ++now; }, 100);
    return ok && output[0] == 1 && output[7] == 8 && now == 8;
}

constexpr bool stalled(bool partial, uint32_t started) {
    MockStream stream;
    stream.bytes = partial ? 2 : 0;
    uint8_t output[8] = {};
    uint32_t now = started;
    const bool ok = KlipperTransfer::readExact(stream, output, 8,
        [] { return true; }, [&] { return now; }, [&] { ++now; }, 10);
    return !ok && static_cast<uint32_t>(now - started) == 10 && stream.position == stream.bytes;
}

constexpr bool disconnected() {
    MockStream stream;
    stream.bytes = 0;
    uint8_t output[4] = {};
    return !KlipperTransfer::readExact(stream, output, 4,
        [] { return false; }, [] { return 0u; }, [] {}, 10);
}

constexpr bool readFailure() {
    MockStream stream;
    stream.failRead = true;
    uint8_t output[4] = {};
    return !KlipperTransfer::readExact(stream, output, 4,
        [] { return true; }, [] { return 0u; }, [] {}, 10);
}

constexpr bool pngFraming() {
    uint8_t png[45] = {137,80,78,71,13,10,26,10,0,0,0,13,'I','H','D','R'};
    png[19] = 100;
    png[23] = 100;
    png[37] = 'I'; png[38] = 'E'; png[39] = 'N'; png[40] = 'D';
    if (!KlipperTransfer::isThumbnailPng(png, sizeof(png))) return false;
    png[18] = 1; png[19] = 44; png[22] = 1; png[23] = 44;
    if (!KlipperTransfer::isThumbnailPng(png, sizeof(png))) return false;
    png[18] = png[22] = 0; png[19] = png[23] = 48;
    if (KlipperTransfer::isThumbnailPng(png, sizeof(png))) return false;
    png[19] = png[23] = 100;
    if (KlipperTransfer::isThumbnailPng(png, 32)) return false;
    if (KlipperTransfer::isThumbnailPng(png, 128 * 1024 + 1)) return false;
    png[23] = 99;
    if (KlipperTransfer::isThumbnailPng(png, sizeof(png))) return false;
    png[23] = 100;
    png[40] = 0;
    return !KlipperTransfer::isThumbnailPng(png, sizeof(png));
}

static_assert(exactAndBounded(), "El servidor no debe poder desbordar el buffer");
static_assert(fragmented(), "Se deben aceptar lecturas parciales completas");
static_assert(stalled(false, 0), "Un socket abierto sin datos debe caducar");
static_assert(stalled(true, 0), "Una imagen truncada debe caducar y descartarse");
static_assert(stalled(false, UINT32_MAX - 3), "El plazo debe resistir el rollover de millis");
static_assert(disconnected(), "La desconexion debe abortar la transferencia");
static_assert(readFailure(), "Un error de lectura debe abortar la transferencia");
static_assert(pngFraming(), "No publicar PNG incompletos o con dimensiones incompatibles");
static_assert(classifyPostResponse(200, true, true, false) == CommandStatus::Completed);
static_assert(classifyPostResponse(200, true, false, true) == CommandStatus::Rejected);
static_assert(classifyPostResponse(403, true, false, true) == CommandStatus::Rejected);
static_assert(classifyPostResponse(-11, false, false, false) == CommandStatus::Unknown);
static_assert(classifyPostResponse(200, false, false, false) == CommandStatus::Unknown);
static_assert(classifyPostResponse(200, true, false, false) == CommandStatus::Unknown);

