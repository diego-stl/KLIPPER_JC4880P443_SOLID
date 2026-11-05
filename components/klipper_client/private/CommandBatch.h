#pragma once
#include "CommandStatus.h"

// All command-worker exits (including loss of WiFi before or during HTTP)
// settle through this rule. A failed normal command ends its entire batch;
// priority traffic has an independent queue.
template <typename Publish, typename Discard>
constexpr void settleCommandBatch(CommandStatus status, bool priority,
                                  Publish publish, Discard discard) {
  publish(status);
  if (!priority &&
      (status == CommandStatus::Rejected || status == CommandStatus::Unknown))
    discard();
}
