#include "../components/klipper_client/private/CommandBatch.h"
#include <array>

struct BatchResult {
  unsigned queued = 20, terminal_count = 0, discarded = 0;
  CommandStatus last = CommandStatus::Pending;
};

constexpr BatchResult finish(CommandStatus result, bool priority = false) {
  BatchResult batch;
  settleCommandBatch(
      result, priority,
      [&](CommandStatus status) {
        ++batch.terminal_count;
        batch.last = status;
      },
      [&] {
        batch.discarded = batch.queued;
        batch.queued = 0;
      });
  return batch;
}

// Regression: a POST may already have executed when WiFi disappears. None of
// its twenty successors may remain available to run on reconnection.
static_assert(finish(CommandStatus::Unknown).queued == 0);
static_assert(finish(CommandStatus::Unknown).discarded == 20);
static_assert(finish(CommandStatus::Unknown).terminal_count == 1);
static_assert(finish(CommandStatus::Unknown).last == CommandStatus::Unknown);
// WiFi lost before transmission is a known rejection, but still ends the batch.
static_assert(finish(CommandStatus::Rejected).queued == 0);
static_assert(finish(CommandStatus::Rejected).discarded == 20);
// Success preserves FIFO successors; priority failures never settle that queue.
static_assert(finish(CommandStatus::Completed).queued == 20);
static_assert(finish(CommandStatus::Unknown, true).queued == 20);
static_assert(finish(CommandStatus::Rejected, true).queued == 20);
// Pending is a notification, never a reason to discard work.
static_assert(finish(CommandStatus::Pending).queued == 20);
