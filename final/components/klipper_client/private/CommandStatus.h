#pragma once

enum class CommandStatus { Pending, Completed, Rejected, Unknown };

// Una respuesta ambigua nunca prueba que la orden no se haya ejecutado.
constexpr CommandStatus classifyPostResponse(int code, bool validJson, bool acknowledged, bool serverError) {
    if (code < 0) return CommandStatus::Unknown;
    if (code < 200 || code >= 300) return CommandStatus::Rejected;
    if (!validJson) return CommandStatus::Unknown;
    if (serverError) return CommandStatus::Rejected;
    return acknowledged ? CommandStatus::Completed : CommandStatus::Unknown;
}
