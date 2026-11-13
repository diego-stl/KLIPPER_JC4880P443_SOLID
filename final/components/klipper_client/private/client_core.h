#pragma once
#include "CommandStatus.h"
#include "MoonrakerStatus.h"
#include "JobPolicy.h"
#include "klipper_client.h"
#include "cJSON.h"
#include "device_network.h"
#include "esp_websocket_client.h"
#include "esp_heap_caps.h"
#include <cstring>
#include <array>
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace klipper {
// Encoded thumbnails can be numerous and small. Allocate them explicitly in
// PSRAM, including <16KiB images which malloc would otherwise put in DRAM.
class PngBytes {
  struct Free { void operator()(uint8_t *p) const { heap_caps_free(p); } };
  std::unique_ptr<uint8_t, Free> bytes;
  size_t length{};
public:
  bool assign(const uint8_t *data, size_t size) {
    auto p = static_cast<uint8_t *>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!p) return false;
    std::memcpy(p, data, size);
    bytes.reset(p); length = size;
    return true;
  }
  bool empty() const { return !length; }
  size_t size() const { return length; }
  const uint8_t *data() const { return bytes.get(); }
};
struct Result {
  uint32_t id{};
  CommandStatus status{};
  std::string command, message;
  uint32_t generation{}, context{};
};
struct Metadata {
  std::string filename;
  double estimated_seconds{};
  float weight{}, length{-1};
  bool has_weight{};
  std::vector<std::string> colors;
  std::vector<klipper_tool_metadata_t> tools;
  uint16_t width{}, height{};
  PngBytes png;
};
struct Object {
  std::string name;
  bool excluded{}, active{}, has_box{};
  float cx{}, cy{}, minx{}, maxx{}, miny{}, maxy{};
};
struct Command {
  uint32_t id{}, generation{};
  std::string script, path;
  uint32_t reset_job{};
  uint32_t tuning_job{}, tuning_epoch{};
  int tuning_control{-1};
  std::string tuning_state{};
  uint32_t context{};
};
struct HttpResponse {
  int code{-1};
  bool complete{};
  std::string data;
};
struct BufferedDelta {
  double time;
  MoonrakerStatusDelta<std::string> delta;
};

class Client {
public:
  std::mutex mutex;
  MoonrakerStatus<std::string> status;
  PauseFilter<std::string> pause;
  uint32_t job_id{}, last_job_revision{};
  std::string job_filename;
  uint32_t job_duration{};
  float job_progress{};
  klipper_last_job_t last_job{};
  std::shared_ptr<const Metadata> job_metadata, last_job_metadata;
  std::string error{"Configure Moonraker por consola"};
  bool connected{};
  std::atomic<bool> socket_open{false}, subscribed{false};
  bool homed_x{}, homed_y{}, homed_z{}, bed_bounds_valid{};
  float position_x{}, position_y{}, position_z{}, bed_min_x{}, bed_max_x{},
      bed_min_y{}, bed_max_y{};
  uint32_t generation{}, revision{}, files_revision{}, macros_revision{},
      objects_revision{}, metadata_revision{};
  std::vector<std::string> files, macros;
  std::vector<Object> objects;
  std::shared_ptr<const Metadata> metadata;
  std::deque<Result> results;
  bool fetching_files{}, fetching_macros{}, fetching_objects{},
      fetching_metadata{};
  bool started{};
  bool start();
  bool enqueue(const std::string &script, const char *path = nullptr,
               bool priority = false,
               uint32_t expected_generation = UINT32_MAX,
               uint32_t reset_job = 0, int tuning_control = -1,
               uint32_t tuning_job = 0, uint32_t context = 0);
  void request(unsigned kind, const std::string &filename = {});
  bool pop(Result &result);
  void sync_endpoint(const device_network_snapshot_t &);

private:
  bool emergency_active{}, shutdown_active{};
  std::array<Command, 3> in_flight{};
  std::deque<Command> commands, priorities;
  uint32_t next_command{}, metadata_request{};
  uint32_t tuning_epoch{};
  std::string requested_file;
  unsigned requests{};
  std::string job_metadata_file;
  std::atomic<bool> restart{false};
  std::recursive_mutex protocol;
  std::atomic<uint32_t> ws_generation{0};
  bool timelapse_supported{}, timelapse_taking{}, timelapse_paused{};
  esp_websocket_client_handle_t ws{};
  uint32_t rpc_next{}, server_id{}, printer_id{}, subscribe_id{}, timelapse_id{};
  uint64_t rpc_sent{}, bootstrap{}, fragment_started{};
  double last_event{-1};
  std::vector<BufferedDelta> before_snapshot;
  std::string frame;
  bool fragmented{};
  size_t frame_received{};
  void publish(const Command &, CommandStatus, const std::string &);
  void finish_command(const Command &, CommandStatus, const std::string &,
                      bool priority);
  void discard(const std::string &);
  bool current(uint32_t gen);
  void unavailable(const std::string &);
  HttpResponse http(const device_network_snapshot_t &, const std::string &,
                    const std::string *body, size_t limit, unsigned timeout,
                    uint32_t reset_job = 0, uint32_t metadata_id = 0,
                    bool background_metadata = false,
                    const Command *tuning = nullptr);
  bool tuning_current(const Command &) const;
  void invalidate_tuning(); // Caller owns mutex.
  void command_worker(bool priority);
  void query_worker();
  void ws_worker();
  void ws_event(int32_t, esp_websocket_event_data_t *);
  static void event_cb(void *, esp_event_base_t, int32_t, void *);
  bool send_rpc(const char *, uint32_t &, cJSON *params = nullptr);
  void message(const std::string &);
  bool apply(cJSON *, double, bool);
  void subscribe(bool augment_timelapse = false);
  void discover_timelapse(const device_network_snapshot_t &);
  void fetch_files(const device_network_snapshot_t &);
  void fetch_macros(const device_network_snapshot_t &);
  void fetch_objects(const device_network_snapshot_t &);
  void fetch_metadata(const device_network_snapshot_t &, const std::string &,
                      uint32_t, bool job_only = false);
};
extern Client client;
} // namespace klipper
