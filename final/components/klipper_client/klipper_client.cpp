#include "klipper_client.h"
#include "CommandBatch.h"
#include "KlipperTransfer.h"
#include "NumericRules.h"
#include "MetadataRules.h"
#include "QueryPolicy.h"
#include "client_core.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace klipper {
Client client;
static uint64_t now_ms() { return esp_timer_get_time() / 1000; }
static cJSON *item(cJSON *p, const char *key) {
  return cJSON_GetObjectItemCaseSensitive(p, key);
}
static const char *text(cJSON *p) {
  return cJSON_IsString(p) && p->valuestring ? p->valuestring : "";
}
static StatusField<std::string> status_text(cJSON *p, const char *key) {
  auto value = item(p, key);
  return cJSON_IsString(value) ? StatusField<std::string>{true, text(value)}
                              : StatusField<std::string>{};
}
static double number(cJSON *p, double fallback = 0) {
  return cJSON_IsNumber(p) && std::isfinite(p->valuedouble) ? p->valuedouble
                                                            : fallback;
}
static bool coordinate(cJSON *p, float &out) {
  return cJSON_IsNumber(p) && KlipperNumeric::coordinate(p->valuedouble, out);
}
static std::string encode(const std::string &s, bool slash = false) {
  static const char hex[] = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : s) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
        c == '~' || (slash && c == '/'))
      out += char(c);
    else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}

static void copy_text(char *dst, size_t cap, const std::string &value) {
  if (cap) {
    size_t n = std::min(cap - 1, value.size());
    std::memcpy(dst, value.data(), n);
    dst[n] = 0;
  }
}
static bool safe_name(const char *name, bool path) {
  if (!name || !*name || std::strlen(name) > (path ? 255u : 127u))
    return false;
  for (const unsigned char *p = reinterpret_cast<const unsigned char *>(name);
       *p; ++p)
    if (*p < 32 || *p == 127 || *p == '"' || *p == '\\' || (!path && *p == ' '))
      return false;
  if (path && (name[0] == '/' || std::strstr(name, "..")))
    return false;
  return true;
}
static klipper_state_t public_state(const std::string &s, bool connected) {
  if (s == "error" || s == "shutdown") return KLIPPER_ERROR;
  if (!connected) return s == "startup" ? KLIPPER_STARTUP : KLIPPER_OFFLINE;
  if (s == "printing") return KLIPPER_PRINTING;
  if (s == "paused") return KLIPPER_PAUSED;
  if (s == "complete") return KLIPPER_COMPLETE;
  if (s == "cancelled") return KLIPPER_CANCELLED;
  return KLIPPER_STANDBY;
}
extern "C" esp_err_t klipper_client_start(void) {
  return klipper::client.start() ? ESP_OK : ESP_ERR_NO_MEM;
}
extern "C" void klipper_client_snapshot(klipper_snapshot_t *out) {
  if (!out)
    return;
  auto &c = klipper::client;
  device_network_snapshot_t net{};
  device_network_snapshot(&net);
  c.sync_endpoint(net);
  std::memset(out, 0, sizeof(*out));
  std::lock_guard<std::mutex> lock(c.mutex);
  out->revision = c.revision + c.files_revision + c.macros_revision +
                  c.objects_revision + c.metadata_revision;
  out->endpoint_generation = c.generation;
  out->error_revision = c.status.klippy.revision;
  out->metadata_revision = c.metadata_revision;
  out->socket_connected = c.socket_open;
  out->available = c.connected;
  out->initial_snapshot = c.subscribed;
  auto raw = c.status.printState();
  const auto previous_visible = c.pause.visible;
  auto visible = c.pause.update(raw, now_ms());
  if (visible != previous_visible) out->revision = ++c.revision + c.files_revision + c.macros_revision + c.objects_revision + c.metadata_revision;
  out->raw_state = public_state(raw, c.connected);
  out->state = public_state(visible, c.connected);
  out->pause_pending = c.pause.pending;
  out->job_id = c.job_id;
  out->hotend_temperature = c.status.hotendTemp;
  out->hotend_target = c.status.hotendTarget;
  out->bed_temperature = c.status.bedTemp;
  out->bed_target = c.status.bedTarget;
  out->progress = c.status.progress * 100;
  out->speed_factor_valid = c.status.speedFactorValid;
  out->flow_factor_valid = c.status.flowFactorValid;
  out->fan_speed_valid = c.status.fanSpeedValid;
  out->speed_percent = c.status.speedPercent;
  out->flow_percent = c.status.flowPercent;
  out->fan_percent = c.status.fanPercent;
  out->print_duration = c.status.printDuration;
  out->time_remaining = c.status.timeRemaining;
  copy_text(out->filename, sizeof(out->filename), c.status.filename);
  copy_text(out->error, sizeof(out->error), c.status.errorMessage());
  copy_text(out->connection_detail, sizeof(out->connection_detail), c.error);
  out->homed_x = c.homed_x;
  out->homed_y = c.homed_y;
  out->homed_z = c.homed_z;
  out->bed_bounds_valid = c.bed_bounds_valid;
  out->position_x = c.position_x;
  out->position_y = c.position_y;
  out->position_z = c.position_z;
  out->bed_min_x = c.bed_min_x;
  out->bed_max_x = c.bed_max_x;
  out->bed_min_y = c.bed_min_y;
  out->bed_max_y = c.bed_max_y;
  out->files_loading = c.fetching_files;
  out->macros_loading = c.fetching_macros;
  out->objects_loading = c.fetching_objects;
  out->metadata_loading = c.fetching_metadata;
  out->files_count = c.files.size();
  out->macros_count = c.macros.size();
  out->objects_count = c.objects.size();
  for (size_t i = 0; i < c.files.size(); ++i)
    copy_text(out->files[i], sizeof(out->files[i]), c.files[i]);
  for (size_t i = 0; i < c.macros.size(); ++i)
    copy_text(out->macros[i], sizeof(out->macros[i]), c.macros[i]);
  for (size_t i = 0; i < c.objects.size(); ++i) {
    auto &dst = out->objects[i];
    const auto &src = c.objects[i];
    copy_text(dst.name, sizeof(dst.name), src.name);
    dst.active = src.active;
    dst.excluded = src.excluded;
    dst.bounds_valid = src.has_box;
    dst.min_x = src.minx;
    dst.max_x = src.maxx;
    dst.min_y = src.miny;
    dst.max_y = src.maxy;
    dst.center_x = src.cx;
    dst.center_y = src.cy;
  }
}
extern "C" char *klipper_client_copy_error_detail(uint32_t expected_generation,
                                                   uint32_t expected_error_revision) {
  auto &c = klipper::client;
  device_network_snapshot_t net{};
  device_network_snapshot(&net);
  c.sync_endpoint(net);
  std::lock_guard<std::mutex> lock(c.mutex);
  const auto &klippy = c.status.klippy;
  if (c.generation != expected_generation || klippy.revision != expected_error_revision ||
      !klippy.faulted() || klippy.message.empty())
    return nullptr;
  const auto size = klippy.message.size() + 1;
  auto copy = static_cast<char *>(std::malloc(size));
  if (copy) std::memcpy(copy, klippy.message.c_str(), size);
  return copy;
}
static bool copy_metadata(klipper_metadata_t *out, const std::shared_ptr<const Metadata> &m,
                          uint32_t revision, uint32_t generation) {
  std::memset(out, 0, sizeof(*out));
  out->revision = revision;
  out->endpoint_generation = generation;
  if (!m) return false;
  out->valid = true;
  copy_text(out->filename, sizeof(out->filename), m->filename);
  out->has_estimated_time = m->estimated_seconds > 0;
  out->estimated_seconds = m->estimated_seconds;
  out->has_weight = m->has_weight;
  out->weight_grams = m->weight;
  out->has_length = m->length >= 0;
  out->filament_length_mm = m->length;
  out->colors_count = m->colors.size();
  for (size_t i = 0; i < m->colors.size(); ++i)
    copy_text(out->colors[i], sizeof(out->colors[i]), m->colors[i]);
  out->tools_count = m->tools.size();
  for (size_t i = 0; i < m->tools.size(); ++i) out->tools[i] = m->tools[i];
  out->thumbnail_width = m->width;
  out->thumbnail_height = m->height;
  out->thumbnail_size = m->png.size();
  if (!m->png.empty()) std::memcpy(out->thumbnail_png, m->png.data(), m->png.size());
  return true;
}
extern "C" bool klipper_client_metadata(klipper_metadata_t *out) {
  if (!out) return false;
  auto &c = klipper::client;
  device_network_snapshot_t net{};
  device_network_snapshot(&net);
  c.sync_endpoint(net);
  std::shared_ptr<const Metadata> metadata;
  uint32_t revision, generation;
  {
    std::lock_guard<std::mutex> lock(c.mutex);
    metadata = c.metadata;
    revision = c.metadata_revision;
    generation = c.generation;
  }
  return copy_metadata(out, metadata, revision, generation);
}
extern "C" bool klipper_client_last_job(klipper_last_job_t *out) {
  if (!out) return false;
  auto &c = klipper::client;
  device_network_snapshot_t net{};
  device_network_snapshot(&net);
  c.sync_endpoint(net);
  std::lock_guard<std::mutex> lock(c.mutex);
  *out = c.last_job;
  return out->valid;
}
extern "C" bool klipper_client_last_job_metadata(klipper_metadata_t *out) {
  if (!out) return false;
  auto &c = klipper::client;
  device_network_snapshot_t net{};
  device_network_snapshot(&net);
  c.sync_endpoint(net);
  std::shared_ptr<const Metadata> metadata;
  uint32_t revision, generation;
  {
    std::lock_guard<std::mutex> lock(c.mutex);
    metadata = c.last_job_metadata;
    revision = c.last_job_revision;
    generation = c.generation;
  }
  return copy_metadata(out, metadata, revision, generation);
}
extern "C" bool klipper_client_pop_result(klipper_command_result_t *out) {
  if (!out)
    return false;
  klipper::Result r;
  if (!klipper::client.pop(r))
    return false;
  out->id = r.id;
  out->generation = r.generation;
  out->context = r.context;
  out->status = static_cast<klipper_command_status_t>(r.status);
  copy_text(out->command, sizeof(out->command), r.command);
  copy_text(out->message, sizeof(out->message), r.message);
  return true;
}
extern "C" bool klipper_client_send_gcode(const char *script) {
  return script && klipper::client.enqueue(script);
}
extern "C" bool klipper_client_send_gcode_at(const char *script,
                                             uint32_t expected_generation,
                                             uint32_t context) {
  return script && klipper::client.enqueue(script, nullptr, false,
                                           expected_generation, 0, -1, 0, context);
}
extern "C" bool klipper_client_set_print_tuning(klipper_tuning_t control,
                                                 unsigned percent,
                                                 uint32_t expected_generation,
                                                 uint32_t expected_job_id) {
  return klipper_client_set_print_tuning_with_context(control, percent,
                                                       expected_generation, expected_job_id, 0);
}
extern "C" bool klipper_client_set_print_tuning_with_context(
    klipper_tuning_t control, unsigned percent, uint32_t expected_generation,
    uint32_t expected_job_id, uint32_t context) {
  if (!PrintTuning::percent(control, percent))
    return false;
  const unsigned value = PrintTuning::commandValue(control, percent);
  return klipper::client.enqueue(std::string(PrintTuning::prefix(control)) + std::to_string(value),
                                nullptr, false, expected_generation, 0,
                                int(control), expected_job_id, context);
}
extern "C" bool klipper_client_emergency_stop(void) {
  return klipper::client.enqueue("EMERGENCIA", "/printer/emergency_stop", true);
}
extern "C" bool klipper_client_shutdown_host(void) {
  return klipper::client.enqueue("HOST_SHUTDOWN", "/machine/shutdown", true);
}
extern "C" bool klipper_client_print_file(const char *filename) {
  return safe_name(filename, true) &&
         klipper::client.enqueue(std::string("SDCARD_PRINT_FILE FILENAME=\"") +
                                 filename + "\"");
}
extern "C" bool klipper_client_run_macro(const char *name) {
  if (!safe_name(name, false))
    return false;
  for (const char *p = name; *p; ++p)
    if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
          (*p >= '0' && *p <= '9') || *p == '_'))
      return false;
  return klipper::client.enqueue(name);
}
extern "C" bool klipper_client_exclude_object(const char *name) {
  return safe_name(name, false) &&
         klipper::client.enqueue(std::string("EXCLUDE_OBJECT NAME=\"") + name +
                                 "\"");
}
extern "C" void klipper_client_fetch_files(void) { klipper::client.request(1); }
extern "C" void klipper_client_fetch_macros(void) {
  klipper::client.request(2);
}
extern "C" void klipper_client_fetch_objects(void) {
  klipper::client.request(4);
}
extern "C" void klipper_client_fetch_metadata(const char *filename) {
  if (safe_name(filename, true))
    klipper::client.request(8, filename);
}
extern "C" bool klipper_client_action(klipper_action_t action,
                                      const char *value,
                                      uint32_t expected_generation) {
  auto &c = klipper::client;
  switch (action) {
  case K_ACTION_GCODE:
    return klipper_client_send_gcode_at(value, expected_generation, 0);
  case K_ACTION_EMERGENCY:
    return c.enqueue("EMERGENCIA", "/printer/emergency_stop", true,
                     expected_generation);
  case K_ACTION_SHUTDOWN:
    return c.enqueue("HOST_SHUTDOWN", "/machine/shutdown", true,
                     expected_generation);
  case K_ACTION_PRINT_FILE:
    return safe_name(value, true) &&
           c.enqueue(std::string("SDCARD_PRINT_FILE FILENAME=\"") + value +
                         "\"",
                     nullptr, false, expected_generation);
  case K_ACTION_MACRO:
    if (!safe_name(value, false))
      return false;
    for (const char *p = value; *p; ++p)
      if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') || *p == '_'))
        return false;
    return c.enqueue(value, nullptr, false, expected_generation);
  case K_ACTION_RESET_JOB: {
    if (!value || !*value) return false;
    uint64_t job = 0;
    for (const char *p = value; *p; ++p) {
      if (*p < '0' || *p > '9' || job > UINT32_MAX / 10) return false;
      job = job * 10 + (*p - '0');
      if (job > UINT32_MAX) return false;
    }
    return job && c.enqueue("SDCARD_RESET_FILE", nullptr, false,
                             expected_generation, uint32_t(job));
  }
  case K_ACTION_EXCLUDE:
    return safe_name(value, false) &&
           c.enqueue(std::string("EXCLUDE_OBJECT NAME=\"") + value + "\"",
                     nullptr, false, expected_generation);
  default:
    return false;
  }
}
static std::string json_string(cJSON *p) {
  char *raw = cJSON_PrintUnformatted(p);
  std::string s = raw ? raw : "";
  cJSON_free(raw);
  return s;
}

bool Client::start() {
  std::lock_guard<std::mutex> lock(mutex);
  if (started)
    return true;
  // Workers persist for device lifetime; no per-request task allocation.
  TaskHandle_t tasks[5]{};
  bool ok = xTaskCreate([](void *) { client.ws_worker(); }, "klipper_ws_ctl",
                        8192, nullptr, 3, &tasks[0]) == pdPASS;
  if (ok)
    ok = xTaskCreate([](void *) { client.command_worker(false); },
                     "klipper_commands", 8192, nullptr, 2, &tasks[1]) == pdPASS;
  if (ok)
    ok = xTaskCreate([](void *) { client.command_worker(true); },
                     "klipper_priority", 8192, nullptr, 4, &tasks[2]) == pdPASS;
  if (ok)
    ok = xTaskCreate([](void *) { client.query_worker(); }, "klipper_queries",
                     12288, nullptr, 1, &tasks[3]) == pdPASS;
  if (ok)
    ok =
        xTaskCreate([](void *) { client.command_worker(true); },
                    "klipper_priority2", 8192, nullptr, 4, &tasks[4]) == pdPASS;
  if (!ok) {
    for (auto task : tasks)
      if (task)
        vTaskDelete(task);
    return false;
  }
  started = true;
  return true;
}
void Client::publish(const Command &cmd, CommandStatus st,
                     const std::string &msg) {
  if (results.size() >= 64)
    results.pop_front();
  results.push_back({cmd.id, st, cmd.script, msg, cmd.generation, cmd.context});
  ++revision;
}
void Client::finish_command(const Command &command, CommandStatus status,
                            const std::string &message, bool priority) {
  settleCommandBatch(
      status, priority,
      [&](CommandStatus settled) { publish(command, settled, message); },
      [&] { discard("Lote detenido tras fallo: verificar antes de repetir"); });
}
void Client::discard(const std::string &reason) {
  for (const auto &c : commands)
    publish(c, CommandStatus::Rejected, reason);
  commands.clear();
}
bool Client::pop(Result &r) {
  std::lock_guard<std::mutex> lock(mutex);
  if (results.empty())
    return false;
  r = std::move(results.front());
  results.pop_front();
  return true;
}
bool Client::current(uint32_t gen) {
  device_network_snapshot_t net{};
  device_network_snapshot(&net);
  return net.has_ip && net.moonraker_host[0] && net.endpoint_generation == gen;
}
void Client::sync_endpoint(const device_network_snapshot_t &) {
  std::lock_guard<std::mutex> lock(mutex);
  // A caller's snapshot may have become stale while waiting for this lock.
  device_network_snapshot_t net{};
  device_network_snapshot(&net);
  if (generation == net.endpoint_generation)
    return;
  generation = net.endpoint_generation;
  // Resolve work from the previous endpoint once, at the switch. Its eventual
  // network response is then ignored, so no late success appears on the new
  // host.
  for (auto &command : in_flight) {
    if (command.id)
      publish(command, CommandStatus::Unknown,
              "Destino modificado durante envio: comprobar host anterior");
    command = {};
  }
  connected = false;
  invalidate_tuning();
  const auto error_revision = status.klippy.revision + 1;
  status = {};
  status.klippy.revision = error_revision;
  pause = {};
  last_job = {};
  job_metadata.reset();
  job_filename.clear();
  job_duration = 0; job_progress = 0;
  last_job_metadata.reset();
  ++last_job_revision;
  error = net.moonraker_host[0] ? "Destino modificado: conectando"
                                : "Configure Moonraker por consola";
  discard("Destino modificado: orden no enviada");
  for (const auto &c : priorities)
    publish(c, CommandStatus::Rejected, "Destino modificado: orden no enviada");
  priorities.clear();
  emergency_active = shutdown_active = false;
  files.clear();
  job_metadata_file.clear();
  macros.clear();
  objects.clear();
  metadata.reset();
  homed_x = homed_y = homed_z = bed_bounds_valid = false;
  position_x = position_y = position_z = bed_min_x = bed_max_x = bed_min_y =
      bed_max_y = 0;
  ++metadata_request;
  requests = 0;
  fetching_files = fetching_macros = fetching_objects = fetching_metadata =
      false;
  ++files_revision;
  ++macros_revision;
  ++objects_revision;
  ++metadata_revision;
  ++revision;
}
bool Client::enqueue(const std::string &script, const char *path, bool priority,
                     uint32_t expected_generation, uint32_t reset_job,
                     int tuning_control, uint32_t tuning_job, uint32_t context) {
  device_network_snapshot_t net{};
  device_network_snapshot(&net);
  sync_endpoint(net);
  std::lock_guard<std::mutex> lock(mutex);
  device_network_snapshot(&net);
  if ((tuning_control >= 0 && expected_generation != net.endpoint_generation) ||
      (expected_generation != UINT32_MAX &&
       expected_generation != net.endpoint_generation) ||
      generation != net.endpoint_generation) {
    publish({0, generation, script, {}}, CommandStatus::Rejected,
            "Destino modificado: vuelva a revisar y confirmar la orden");
    return false;
  }
  Command c{++next_command, net.endpoint_generation, script,
            path ? path : "/printer/gcode/script", reset_job};
  c.context = context;
  if (tuning_control >= 0) {
    c.tuning_control = tuning_control;
    c.tuning_job = tuning_job;
    c.tuning_epoch = tuning_epoch;
    c.tuning_state = status.printState();
    if (!tuning_current(c)) {
      publish(c, CommandStatus::Rejected,
              "Ajuste no disponible o trabajo modificado: revisar antes de enviar");
      return false;
    }
  }
  if (reset_job && !resetJobAllowed(connected, status.jobState, job_id, reset_job,
                                     generation, expected_generation)) {
    publish(c, CommandStatus::Rejected, "El trabajo ya no es el terminal confirmado");
    return false;
  }
  if (script.empty() || script.size() > 4096 || !net.has_ip ||
      !net.moonraker_host[0] ||
      (!priority && (commands.size() >= 20 || emergency_active)) ||
      (priority && priorities.size() >= 2)) {
    publish(c, CommandStatus::Rejected,
            "Sin destino/red, orden invalida o cola llena");
    return false;
  }
  if (priority) {
    bool &active = c.path == "/printer/emergency_stop" ? emergency_active
                                                       : shutdown_active;
    if (active) {
      publish(c, CommandStatus::Rejected, "Solicitud prioritaria ya pendiente");
      return false;
    }
    for (const auto &p : priorities)
      if (p.path == c.path) {
        publish(c, CommandStatus::Rejected,
                "Solicitud prioritaria ya pendiente");
        return false;
      }
    if (c.path == "/printer/emergency_stop")
      discard("Cancelada por parada de emergencia");
    active = true;
    if (c.path == "/printer/emergency_stop")
      priorities.push_front(c);
    else
      priorities.push_back(c);
  } else
    commands.push_back(c);
  publish(c, CommandStatus::Pending, "Orden pendiente");
  return true;
}
void Client::request(unsigned kind, const std::string &filename) {
  device_network_snapshot_t net{};
  device_network_snapshot(&net);
  sync_endpoint(net);
  std::lock_guard<std::mutex> lock(mutex);
  if (!net.has_ip || !net.moonraker_host[0]) {
    publish({0, generation, "Consulta", {}}, CommandStatus::Rejected,
            "Configure Moonraker y WiFi por consola");
    return;
  }
  if (kind == 8) {
    if (filename.empty() || filename.size() > 512)
      return;
    requested_file = filename;
    if (++metadata_request == 0) ++metadata_request;
    metadata.reset();
    ++metadata_revision;
    fetching_metadata = true;
  }
  if (kind == 1)
    fetching_files = true;
  if (kind == 2)
    fetching_macros = true;
  if (kind == 4)
    fetching_objects = true;
  requests |= kind;
  ++revision;
}
void Client::invalidate_tuning() {
  status.speedFactorValid = status.flowFactorValid = status.fanSpeedValid = false;
  status.speedPercent = status.flowPercent = status.fanPercent = 0;
  ++tuning_epoch;
}
bool Client::tuning_current(const Command &command) const {
  const bool valid = command.tuning_control == K_TUNING_SPEED ? status.speedFactorValid
                   : command.tuning_control == K_TUNING_FLOW ? status.flowFactorValid
                   : command.tuning_control == K_TUNING_FAN && status.fanSpeedValid;
  return PrintTuning::bindingCurrent(connected, status.printState(),
             command.tuning_state, tuning_epoch, command.tuning_epoch,
             generation, command.generation, job_id, command.tuning_job, valid);
}
HttpResponse Client::http(const device_network_snapshot_t &net,
                          const std::string &path, const std::string *body,
                          size_t limit, unsigned timeout, uint32_t reset_job,
                          uint32_t metadata_id, bool background_metadata,
                          const Command *tuning) {
  HttpResponse r;
  if (!current(net.endpoint_generation))
    return r;
  const auto query_still_current = [&] {
    if (!metadata_id && !background_metadata) return true;
    std::lock_guard<std::mutex> lock(mutex);
    return metadataQueryCurrent(metadata_id, metadata_request, background_metadata, requests);
  };
  if (!query_still_current()) return r;
  const auto reset_still_current = [&] {
    if (!reset_job) return true;
    std::lock_guard<std::mutex> lock(mutex);
    return resetJobAllowed(connected, status.jobState, job_id, reset_job,
                           generation, net.endpoint_generation);
  };
  const auto rejected_reset = [] {
    return HttpResponse{409, true, "{\"error\":{\"message\":\"Trabajo modificado antes de enviar reset\"}}"};
  };
  if (!reset_still_current()) return rejected_reset();
  const auto tuning_still_current = [&] {
    if (!tuning) return true;
    std::lock_guard<std::mutex> lock(mutex);
    return tuning_current(*tuning);
  };
  const auto rejected_tuning = [] {
    return HttpResponse{409, true, "{\"error\":{\"message\":\"Trabajo o ajuste modificado antes de enviar\"}}"};
  };
  if (!tuning_still_current()) return rejected_tuning();
  const std::string url = "http://" + std::string(net.moonraker_host) + ":" +
                          std::to_string(net.moonraker_port) + path;
  esp_http_client_config_t cfg{};
  cfg.url = url.c_str();
  cfg.timeout_ms = 3000;
  cfg.disable_auto_redirect = true;
  cfg.buffer_size = 1024;
  auto h = esp_http_client_init(&cfg);
  if (!h)
    return r;
  esp_http_client_set_method(h, body ? HTTP_METHOD_POST : HTTP_METHOD_GET);
  if (body)
    esp_http_client_set_header(h, "Content-Type", "application/json");
  const auto began = now_ms();
  if (!current(net.endpoint_generation) || !query_still_current() ||
      !tuning_still_current() ||
      esp_http_client_open(h, body ? body->size() : 0) != ESP_OK) {
    esp_http_client_cleanup(h);
    return r;
  }
  bool ok = true;
  if (body) {
    // Validate again after opening the socket, immediately before sending the
    // request body. A new print while queued/connecting cannot receive a reset.
    if (!reset_still_current()) {
      esp_http_client_close(h);
      esp_http_client_cleanup(h);
      return rejected_reset();
    }
    if (!current(net.endpoint_generation) || !tuning_still_current()) {
      esp_http_client_close(h);
      esp_http_client_cleanup(h);
      return rejected_tuning();
    }
    size_t sent = 0;
    while (sent < body->size() && now_ms() - began < timeout &&
           current(net.endpoint_generation)) {
      int n =
          esp_http_client_write(h, body->data() + sent, body->size() - sent);
      if (n <= 0) {
        ok = false;
        break;
      }
      sent += n;
    }
    if (sent != body->size())
      ok = false;
  }
  esp_http_client_set_timeout_ms(
      h, std::max(1, int(timeout) - int(now_ms() - began)));
  if (ok && query_still_current() && esp_http_client_fetch_headers(h) >= 0) {
    esp_http_client_set_timeout_ms(h, 1000);
    r.code = esp_http_client_get_status_code(h);
    const int64_t expected = esp_http_client_get_content_length(h);
    if (expected > int64_t(limit))
      ok = false;
    char buf[1024];
    while (ok && now_ms() - began < timeout &&
           current(net.endpoint_generation) && query_still_current()) {
      int n = esp_http_client_read(h, buf, sizeof(buf));
      if (n < 0) {
        ok = false;
        break;
      }
      if (!n) {
        r.complete = esp_http_client_is_complete_data_received(h);
        if (r.complete)
          break;
        vTaskDelay(pdMS_TO_TICKS(10));
        continue;
      }
      if (r.data.size() + n > limit) {
        ok = false;
        break;
      }
      r.data.append(buf, n);
    }
    r.complete =
        ok && query_still_current() && r.complete && (expected < 0 || size_t(expected) == r.data.size());
  }
  esp_http_client_close(h);
  esp_http_client_cleanup(h);
  return r;
}
void Client::command_worker(bool priority) {
  while (true) {
    Command c;
    unsigned slot = 0;
    bool found = false;
    {
      std::lock_guard<std::mutex> lock(mutex);
      auto &q = priority ? priorities : commands;
      if (!q.empty()) {
        c = std::move(q.front());
        slot = priority ? (c.path == "/printer/emergency_stop" ? 1 : 2) : 0;
        in_flight[slot] = c;
        q.pop_front();
        found = true;
      }
    }
    if (!found) {
      vTaskDelay(pdMS_TO_TICKS(30));
      continue;
    }
    device_network_snapshot_t net{};
    device_network_snapshot(&net);
    if (c.generation != net.endpoint_generation || !current(c.generation)) {
      std::lock_guard<std::mutex> lock(mutex);
      if (c.generation != generation)
        continue;
      in_flight[slot] = {};
      finish_command(c, CommandStatus::Rejected,
                     "Destino/red cambiado antes del envio", priority);
      if (priority) {
        if (c.path == "/printer/emergency_stop")
          emergency_active = false;
        else
          shutdown_active = false;
      }
      continue;
    }
    if (c.reset_job) {
      std::lock_guard<std::mutex> lock(mutex);
      if (!resetJobAllowed(connected, status.jobState, job_id, c.reset_job,
                           generation, c.generation)) {
        in_flight[slot] = {};
        finish_command(c, CommandStatus::Rejected,
                       "Trabajo modificado antes de restablecer", priority);
        continue;
      }
    }
    std::string payload;
    if (c.path == "/printer/gcode/script") {
      cJSON *obj = cJSON_CreateObject();
      cJSON_AddStringToObject(obj, "script", c.script.c_str());
      payload = json_string(obj);
      cJSON_Delete(obj);
    }
    auto r = http(net, c.path, &payload, 8192, priority ? 5000 : 120000,
                  c.reset_job, 0, false, c.tuning_control >= 0 ? &c : nullptr);
    cJSON *doc = r.complete
                     ? cJSON_ParseWithLength(r.data.data(), r.data.size())
                     : nullptr;
    const bool ack = doc && std::strcmp(text(item(doc, "result")), "ok") == 0;
    const auto st = classifyPostResponse(r.code, doc != nullptr, ack,
                                         doc && item(doc, "error"));
    std::string msg = st == CommandStatus::Completed ? "Orden completada"
                      : st == CommandStatus::Rejected
                          ? "Orden rechazada"
                          : "Sin confirmacion: comprobar antes de repetir";
    if (doc && item(doc, "error")) {
      const char *detail = text(item(item(doc, "error"), "message"));
      if (*detail)
        msg += std::string(": ") + detail;
    }
    cJSON_Delete(doc);
    std::lock_guard<std::mutex> lock(mutex);
    if (c.generation != generation)
      continue;
    in_flight[slot] = {};
    if (priority && c.generation == generation) {
      if (c.path == "/printer/emergency_stop")
        emergency_active = false;
      else
        shutdown_active = false;
    }
    // Never publish an acknowledgement from the previous printer as current
    // success.
    if (!current(c.generation)) {
      finish_command(c, CommandStatus::Unknown,
                     "Destino/red cambiado durante envio: resultado incierto",
                     priority);
      continue;
    }
    finish_command(c, st, msg, priority);
    if (st == CommandStatus::Completed &&
        c.script.rfind("EXCLUDE_OBJECT ", 0) == 0) {
      requests |= 4;
      fetching_objects = true;
    }
  }
}
void Client::query_worker() {
  while (true) {
    unsigned work = 0;
    uint32_t id = 0;
    std::string filename;
    {
      std::lock_guard<std::mutex> lock(mutex);
      if (requests) {
        work = nextKlipperQuery(requests);
        requests &= ~work;
      }
      if (work == 32) filename = job_metadata_file;
      id = metadata_request;
      if (work != 32) filename = requested_file;
    }
    if (!work) {
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    device_network_snapshot_t net{};
    device_network_snapshot(&net);
    if (work == 1)
      fetch_files(net);
    if (work == 2)
      fetch_macros(net);
    if (work == 4)
      fetch_objects(net);
    if (work == 8)
      fetch_metadata(net, filename, id);
    if (work == 32)
      fetch_metadata(net, filename, 0, true);
    if (work == 64)
      discover_timelapse(net);
  }
}
void Client::fetch_files(const device_network_snapshot_t &net) {
  auto r =
      http(net, "/server/files/list?root=gcodes", nullptr, 256 * 1024, 6000);
  cJSON *doc = r.complete && r.code == 200
                   ? cJSON_ParseWithLength(r.data.data(), r.data.size())
                   : nullptr;
  std::vector<std::pair<double, std::string>> sorted;
  cJSON *p = nullptr;
  cJSON_ArrayForEach(p, item(doc, "result")) {
    const char *name = text(item(p, "path"));
    if (*name && std::strlen(name) < 256)
      sorted.emplace_back(number(item(p, "modified")), name);
  }
  std::sort(sorted.begin(), sorted.end(),
            [](const auto &a, const auto &b) { return a.first > b.first; });
  const bool valid_response = doc != nullptr;
  cJSON_Delete(doc);
  std::lock_guard<std::mutex> lock(mutex);
  if (net.endpoint_generation == generation) {
    if (current(generation)) {
      files.clear();
      for (size_t i = 0; i < std::min(size_t(15), sorted.size()); ++i)
        files.push_back(sorted[i].second);
    }
    fetching_files = false;
    ++files_revision;
    if (!valid_response)
      publish({0, generation, "Archivos", {}}, CommandStatus::Rejected,
              "No se pudo consultar archivos");
  }
}
void Client::fetch_macros(const device_network_snapshot_t &net) {
  auto r = http(net, "/printer/objects/list", nullptr, 128 * 1024, 5000);
  cJSON *doc = r.complete && r.code == 200
                   ? cJSON_ParseWithLength(r.data.data(), r.data.size())
                   : nullptr;
  std::vector<std::string> candidates, names;
  cJSON *p = nullptr;
  if (cJSON_IsArray(item(item(doc, "result"), "objects")))
  cJSON_ArrayForEach(p, item(item(doc, "result"), "objects")) {
    std::string name = text(p);
    if (name.rfind("gcode_macro ", 0) == 0 && name.size() < 140 &&
        MetadataRules::macroVisible(name.c_str() + 12, false, true))
      candidates.push_back(name.substr(12));
  }
  // Query optional public macro variables in small requests. Klipper exposes
  // variable_portal_visible as portal_visible in the runtime object. Omitted
  // markers preserve existing public macros; _private helpers never consume cap.
  for (size_t begin = 0; begin < candidates.size() && names.size() < KLIPPER_MACROS_MAX; begin += 16) {
    std::string query = "/printer/objects/query?";
    const size_t end = std::min(begin + 16, candidates.size());
    for (size_t i = begin; i < end; ++i) {
      if (i != begin) query += '&';
      query += encode("gcode_macro " + candidates[i]) + "=portal_visible";
    }
    auto vars = http(net, query, nullptr, 64 * 1024, 5000);
    cJSON *vdoc = vars.complete && vars.code == 200
        ? cJSON_ParseWithLength(vars.data.data(), vars.data.size()) : nullptr;
    auto state = item(item(vdoc, "result"), "status");
    for (size_t i = begin; i < end && names.size() < KLIPPER_MACROS_MAX; ++i) {
      auto marker = item(item(state, ("gcode_macro " + candidates[i]).c_str()), "portal_visible");
      const bool present = cJSON_IsBool(marker) ||
          (cJSON_IsNumber(marker) && (marker->valuedouble == 0 || marker->valuedouble == 1));
      const bool visible = cJSON_IsTrue(marker) || (cJSON_IsNumber(marker) && marker->valuedouble == 1);
      if (MetadataRules::macroVisible(candidates[i].c_str(), present, visible)) names.push_back(candidates[i]);
    }
    cJSON_Delete(vdoc);
    if (!current(net.endpoint_generation)) break;
  }
  const bool valid_response = doc != nullptr;
  cJSON_Delete(doc);
  std::lock_guard<std::mutex> lock(mutex);
  if (net.endpoint_generation == generation) {
    if (current(generation))
      macros = std::move(names);
    fetching_macros = false;
    ++macros_revision;
    if (!valid_response)
      publish({0, generation, "Macros", {}}, CommandStatus::Rejected,
              "No se pudo consultar macros");
  }
}
void Client::fetch_objects(const device_network_snapshot_t &net) {
  auto r = http(net, "/printer/objects/query?exclude_object", nullptr,
                128 * 1024, 5000);
  cJSON *doc = r.complete && r.code == 200
                   ? cJSON_ParseWithLength(r.data.data(), r.data.size())
                   : nullptr;
  cJSON *eo = item(item(item(doc, "result"), "status"), "exclude_object");
  std::vector<Object> list;
  cJSON *p = nullptr;
  cJSON_ArrayForEach(p, item(eo, "objects")) {
    if (list.size() == 16)
      break;
    Object o;
    o.name = text(item(p, "name"));
    if (o.name.empty() || o.name.size() >= 128)
      continue;
    cJSON *center = item(p, "center");
    const bool has_center = coordinate(cJSON_GetArrayItem(center, 0), o.cx) &&
                            coordinate(cJSON_GetArrayItem(center, 1), o.cy);
    cJSON *point = nullptr;
    cJSON_ArrayForEach(point, item(p, "polygon")) {
      auto x = cJSON_GetArrayItem(point, 0), y = cJSON_GetArrayItem(point, 1);
      float px{}, py{};
      if (!coordinate(x, px) || !coordinate(y, py))
        continue;
      if (!o.has_box) {
        o.minx = o.maxx = px;
        o.miny = o.maxy = py;
        o.has_box = true;
      } else {
        o.minx = std::min(o.minx, px);
        o.maxx = std::max(o.maxx, px);
        o.miny = std::min(o.miny, py);
        o.maxy = std::max(o.maxy, py);
      }
    }
    if (o.has_box && !(o.maxx > o.minx && o.maxy > o.miny))
      o.has_box = false;
    if (!has_center) {
      o.cx = o.has_box ? (o.minx + o.maxx) / 2 : 0;
      o.cy = o.has_box ? (o.miny + o.maxy) / 2 : 0;
    }
    o.active = o.name == text(item(eo, "current_object"));
    cJSON *excluded = nullptr;
    cJSON_ArrayForEach(
        excluded, item(eo, "excluded_objects")) if (o.name == text(excluded))
        o.excluded = true;
    list.push_back(std::move(o));
  }
  const bool valid_response = eo != nullptr;
  cJSON_Delete(doc);
  std::lock_guard<std::mutex> lock(mutex);
  if (net.endpoint_generation == generation) {
    if (current(generation))
      objects = std::move(list);
    fetching_objects = false;
    ++objects_revision;
    if (!valid_response)
      publish({0, generation, "Objetos", {}}, CommandStatus::Rejected,
              "exclude_object no disponible");
  }
}
void Client::fetch_metadata(const device_network_snapshot_t &net,
                            const std::string &filename, uint32_t id, bool job_only) {
  if (job_only) {
    std::lock_guard<std::mutex> lock(mutex);
    if (job_metadata && job_metadata->filename == filename) return;
    if (requests & 8) { requests |= 32; return; }
  }
  auto r = http(net, "/server/files/metadata?filename=" + encode(filename),
                nullptr, 128 * 1024, 6000, 0, job_only ? 0 : id, job_only);
  cJSON *doc = r.complete && r.code == 200
                   ? cJSON_ParseWithLength(r.data.data(), r.data.size())
                   : nullptr;
  cJSON *result = item(doc, "result");
  std::shared_ptr<Metadata> m(new (std::nothrow) Metadata);
  if (m && cJSON_IsObject(result)) {
    m->filename = filename;
    auto estimated = item(result, "estimated_time");
    if (cJSON_IsNumber(estimated))
      KlipperNumeric::seconds(estimated->valuedouble, m->estimated_seconds);
    auto length = item(result, "filament_total");
    if (cJSON_IsNumber(length))
      KlipperNumeric::asFloat(length->valuedouble, m->length, 0, 1e9);
    auto weight = item(result, "filament_weight_total");
    m->has_weight =
        cJSON_IsNumber(weight) &&
        KlipperNumeric::asFloat(weight->valuedouble, m->weight, 0, 1e9);
    cJSON *colors = item(result, "filament_colors");
    cJSON *weights = item(result, "filament_weights");
    cJSON *referenced = item(result, "referenced_tools");
    if (!cJSON_IsArray(colors)) colors = nullptr;
    if (!cJSON_IsArray(weights)) weights = nullptr;
    const bool has_referenced = cJSON_IsArray(referenced);
    int referenced_count = 0;
    cJSON *ref = nullptr;
    if (has_referenced) cJSON_ArrayForEach(ref, referenced) {
      if (cJSON_IsNumber(ref) && std::isfinite(ref->valuedouble) &&
          ref->valuedouble >= 0 && ref->valuedouble < double(KLIPPER_TOOLS_MAX) &&
          std::floor(ref->valuedouble) == ref->valuedouble)
        referenced_count = std::max(referenced_count, int(ref->valuedouble) + 1);
    }
    const int count = std::min(int(KLIPPER_TOOLS_MAX), std::max(referenced_count,
        std::max(cJSON_GetArraySize(colors), cJSON_GetArraySize(weights))));
    for (int i = 0; i < count; ++i) {
      klipper_tool_metadata_t tool{};
      tool.tool_index = i;
      tool.referenced = false;
      const char *color = text(cJSON_GetArrayItem(colors, i));
      tool.has_color = MetadataRules::color(color);
      if (tool.has_color) {
        copy_text(tool.color, sizeof(tool.color), color);
        m->colors.emplace_back(color);
      }
      auto weight = cJSON_GetArrayItem(weights, i);
      tool.has_weight = cJSON_IsNumber(weight) &&
          KlipperNumeric::asFloat(weight->valuedouble, tool.weight_grams, 0, 1e9);
      cJSON *ref = nullptr;
      if (has_referenced) cJSON_ArrayForEach(ref, referenced) {
        if (cJSON_IsNumber(ref) && std::isfinite(ref->valuedouble) && ref->valuedouble == i)
          tool.referenced = true;
      }
      const auto normalized = MetadataRules::normalizeToolWeight(tool.has_weight, tool.weight_grams, tool.referenced);
      tool.has_weight = normalized.has_weight;
      tool.weight_grams = normalized.grams;
      tool.referenced = normalized.referenced;
      // Keep sparse indices; an absent value must never be invented as zero.
      m->tools.push_back(tool);
    }
    if (!m->has_weight && weights && cJSON_GetArraySize(weights) <= KLIPPER_TOOLS_MAX) {
      bool all_weights = cJSON_GetArraySize(weights) > 0;
      double total = 0;
      for (int i = 0; i < cJSON_GetArraySize(weights); ++i) {
        all_weights = all_weights && m->tools[i].has_weight;
        total += m->tools[i].weight_grams;
      }
      if (all_weights && KlipperNumeric::asFloat(total, m->weight, 0, 1e9)) m->has_weight = true;
    }
    // A lone confirmed tool can use the available aggregate weight. With
    // several colors/tool slots there is no defensible per-tool attribution.
    if (m->has_weight && m->tools.size() == 1 && !m->tools[0].has_weight) {
      m->tools[0].has_weight = true;
      m->tools[0].weight_grams = m->weight;
    }
    cJSON *thumb = nullptr;
    std::vector<std::pair<unsigned, std::string>> thumbnails;
    if (cJSON_IsArray(item(result, "thumbnails")))
    cJSON_ArrayForEach(thumb, item(result, "thumbnails")) {
      const double w = number(item(thumb, "width")), h = number(item(thumb, "height"));
      const unsigned rank = w == 300 && h == 300 ? 2 : w == 100 && h == 100 ? 1 : 0;
      const std::string path = text(item(thumb, "relative_path"));
      if (!rank || !safe_name(path.c_str(), true) || path.find(':') != std::string::npos) continue;
      // At most one path per supported size: bound work even for hostile metadata.
      if (std::none_of(thumbnails.begin(), thumbnails.end(), [rank](const auto &t) { return t.first == rank; }))
        thumbnails.emplace_back(rank, path);
    }
    std::sort(thumbnails.begin(), thumbnails.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    const auto slash = filename.find_last_of('/');
    const std::string dir = slash == std::string::npos ? "" : filename.substr(0, slash + 1);
    for (const auto &candidate : thumbnails) {
      auto image = http(net, "/server/files/gcodes/" + encode(dir + candidate.second, true),
                        nullptr, KLIPPER_THUMBNAIL_MAX, 5000, 0, job_only ? 0 : id, job_only);
      const auto bytes = reinterpret_cast<const uint8_t *>(image.data.data());
      if (image.code != 200 || !image.complete || !KlipperTransfer::isThumbnailPng(bytes, image.data.size())) continue;
      const auto width = KlipperTransfer::pngU32(bytes + 16), height = KlipperTransfer::pngU32(bytes + 20);
      if (MetadataRules::thumbnailRank(width, height) != candidate.first) continue;
      m->width = width; m->height = height;
      m->png.assign(bytes, image.data.size());
      break;
    }
  } else
    m.reset();
  cJSON_Delete(doc);
  std::lock_guard<std::mutex> lock(mutex);
  if (job_only) {
    if (net.endpoint_generation == generation && (requests & 8)) {
      requests |= 32;
      return;
    }
    if (net.endpoint_generation == generation && current(generation) && m) {
      if (m->filename == job_filename) job_metadata = m;
      if (last_job.valid && m->filename == last_job.filename &&
          (!last_job_metadata || last_job_metadata->filename != m->filename)) {
        last_job_metadata = m;
        last_job.revision = ++last_job_revision;
      }
    }
    return;
  }
  if (net.endpoint_generation == generation && id == metadata_request) {
    if (current(generation))
      metadata = m;
    if (current(generation) && m && m->filename == job_filename) {
      job_metadata = m;
      if (last_job.valid && last_job.job_id == job_id) {
        last_job_metadata = m;
        last_job.revision = ++last_job_revision;
      }
    }
    fetching_metadata = false;
    ++metadata_revision;
    if (!metadata)
      publish({0, generation, "Metadatos", {}}, CommandStatus::Rejected,
              "No se pudieron consultar metadatos");
  }
}

static MoonrakerStatusDelta<std::string> delta_from(cJSON *s) {
  MoonrakerStatusDelta<std::string> d;
  auto fp = [](cJSON *o, const char *key) {
    auto p = item(o, key);
    float value{};
    return cJSON_IsNumber(p) && KlipperNumeric::asFloat(p->valuedouble, value)
               ? StatusField<float>{true, value}
               : StatusField<float>{};
  };
  d.hotendTemp = fp(item(s, "extruder"), "temperature");
  d.hotendTarget = fp(item(s, "extruder"), "target");
  d.bedTemp = fp(item(s, "heater_bed"), "temperature");
  d.bedTarget = fp(item(s, "heater_bed"), "target");
  d.progress = fp(item(s, "virtual_sdcard"), "progress");
  d.filename = status_text(item(s, "print_stats"), "filename");
  d.printState = status_text(item(s, "print_stats"), "state");
  d.klippyState = status_text(item(s, "webhooks"), "state");
  d.stateMessage = status_text(item(s, "webhooks"), "state_message");
  const auto tuning = [&](const char *object_name, const char *field_name, bool fan) {
    auto object = item(s, object_name);
    if (!object) return PrintTuning::Field{};
    if (!cJSON_IsObject(object)) return PrintTuning::Field{true, false, 0};
    auto field = item(object, field_name);
    if (!field) return PrintTuning::Field{};
    return cJSON_IsNumber(field) ? PrintTuning::reading(field->valuedouble, fan)
                                 : PrintTuning::Field{true, false, 0};
  };
  d.speedPercent = tuning("gcode_move", "speed_factor", false);
  d.flowPercent = tuning("gcode_move", "extrude_factor", false);
  d.fanPercent = tuning("fan", "speed", true);
  auto duration = item(item(s, "print_stats"), "print_duration");
  if (cJSON_IsNumber(duration) && std::isfinite(duration->valuedouble) &&
      duration->valuedouble >= 0 && duration->valuedouble <= UINT32_MAX)
    d.printDuration = {true, uint32_t(duration->valuedouble)};
  return d;
}
void Client::unavailable(const std::string &reason) {
  std::lock_guard<std::mutex> lock(mutex);
  if (ws_generation != generation)
    return;
  connected = false;
  invalidate_tuning();
  error = reason;
  ++revision;
}
bool Client::send_rpc(const char *method, uint32_t &pending, cJSON *params) {
  if (!socket_open || pending) {
    cJSON_Delete(params);
    return false;
  }
  if (++rpc_next == 0)
    ++rpc_next;
  pending = rpc_next;
  rpc_sent = bootstrap = now_ms();
  auto doc = cJSON_CreateObject();
  cJSON_AddStringToObject(doc, "jsonrpc", "2.0");
  cJSON_AddStringToObject(doc, "method", method);
  cJSON_AddNumberToObject(doc, "id", pending);
  if (params)
    cJSON_AddItemToObject(doc, "params", params);
  auto payload = json_string(doc);
  cJSON_Delete(doc);
  if (esp_websocket_client_send_text(ws, payload.data(), payload.size(),
                                     pdMS_TO_TICKS(1000)) ==
      int(payload.size()))
    return true;
  pending = 0;
  restart = true;
  unavailable("No se pudo enviar consulta WebSocket");
  return false;
}
void Client::probe_klippy(bool detailed) {
  if (server_id || printer_id) return;
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (ws_generation != generation || !current(generation)) return;
    state_rpc_revision = status.klippy.revision;
  }
  if (detailed)
    send_rpc("printer.info", printer_id);
  else
    send_rpc("server.info", server_id);
}
static bool optional_bool(cJSON *p, bool &value) {
  if (cJSON_IsBool(p)) { value = cJSON_IsTrue(p); return true; }
  if (cJSON_IsNumber(p) && (p->valuedouble == 0 || p->valuedouble == 1)) {
    value = p->valuedouble == 1; return true;
  }
  return false;
}
void Client::discover_timelapse(const device_network_snapshot_t &net) {
  auto list = http(net, "/printer/objects/list", nullptr, 128 * 1024, 5000);
  cJSON *doc = list.complete && list.code == 200 ? cJSON_ParseWithLength(list.data.data(), list.data.size()) : nullptr;
  bool found = false;
  cJSON *object = nullptr;
  cJSON_ArrayForEach(object, item(item(doc, "result"), "objects"))
    if (std::strcmp(text(object), "gcode_macro TIMELAPSE_TAKE_FRAME") == 0) found = true;
  cJSON_Delete(doc);
  if (!found || !current(net.endpoint_generation)) return;
  auto query = http(net, "/printer/objects/query?gcode_macro%20TIMELAPSE_TAKE_FRAME=is_paused,takingframe", nullptr, 16384, 5000);
  doc = query.complete && query.code == 200 ? cJSON_ParseWithLength(query.data.data(), query.data.size()) : nullptr;
  auto macro = item(item(item(doc, "result"), "status"), "gcode_macro TIMELAPSE_TAKE_FRAME");
  bool a{}, b{};
  const bool compatible = optional_bool(item(macro, "is_paused"), a) |
                          optional_bool(item(macro, "takingframe"), b);
  cJSON_Delete(doc);
  if (!compatible) return;
  std::lock_guard<std::recursive_mutex> guard(protocol);
  if (ws_generation != net.endpoint_generation || !current(net.endpoint_generation)) return;
  timelapse_supported = true;
  // The query establishes schema compatibility only. The live subscription is
  // the freshness authority; an HTTP response must not manufacture a signal.
  if (subscribed) subscribe(true);
}
void Client::subscribe(bool augment_timelapse) {
  if ((!augment_timelapse && subscribed) || subscribe_id || timelapse_id)
    return;
  auto params = cJSON_CreateObject(), objects = cJSON_CreateObject();
  cJSON_AddItemToObject(params, "objects", objects);
  const char *names[] = {"extruder",       "heater_bed", "print_stats",
                         "virtual_sdcard", "webhooks",   "toolhead",
                         "gcode_move", "fan"};
  const char *fields[][5] = {
      {"temperature", "target", nullptr},
      {"temperature", "target", nullptr},
      {"filename", "state", "print_duration", nullptr},
      {"progress", nullptr},
      {"state", "state_message", nullptr},
      {"homed_axes", "position", "axis_minimum", "axis_maximum", nullptr},
      {"speed_factor", "extrude_factor", nullptr},
      {"speed", nullptr}};
  for (unsigned i = 0; i < 8; ++i) {
    auto a = cJSON_CreateArray();
    for (unsigned j = 0; fields[i][j]; ++j)
      cJSON_AddItemToArray(a, cJSON_CreateString(fields[i][j]));
    cJSON_AddItemToObject(objects, names[i], a);
  }
  if (timelapse_supported) {
    auto fields = cJSON_CreateArray();
    cJSON_AddItemToArray(fields, cJSON_CreateString("is_paused"));
    cJSON_AddItemToArray(fields, cJSON_CreateString("takingframe"));
    cJSON_AddItemToObject(objects, "gcode_macro TIMELAPSE_TAKE_FRAME", fields);
  }
  send_rpc("printer.objects.subscribe", augment_timelapse ? timelapse_id : subscribe_id, params);
}
bool Client::apply(cJSON *obj, double eventtime, bool snapshot) {
  if (!cJSON_IsObject(obj) || !std::isfinite(eventtime) || eventtime < 0)
    return false;
  auto delta = delta_from(obj);
  if (!snapshot && !subscribed && subscribe_id) {
    if (before_snapshot.size() == 8)
      return false;
    before_snapshot.push_back({eventtime, std::move(delta)});
    return true;
  }
  if (!snapshot && eventtime < last_event)
    return true;
  std::lock_guard<std::mutex> lock(mutex);
  if (ws_generation != generation || !current(generation))
    return true;
  if (!snapshot && !subscribed) {
    // A shutdown delta can follow its lifecycle notification before bootstrap.
    // A message-only delta needs an observed WebSocket baseline for this fault.
    const bool fault_update = delta.klippyState.present
        ? delta.klippyState.value == "shutdown" || delta.klippyState.value == "error"
        : delta.stateMessage.present && last_event >= 0;
    if (status.klippy.faulted() && fault_update) {
      status.klippy.apply(delta.klippyState, delta.stateMessage);
      last_event = eventtime;
      ++revision;
    }
    return true;
  }
  if (snapshot) {
    if (!delta.klippyState.present)
      return false;
    auto klippy = std::move(status.klippy);
    status = {};
    status.klippy = std::move(klippy);
    ++tuning_epoch;
  }
  cJSON *timelapse = item(obj, "gcode_macro TIMELAPSE_TAKE_FRAME");
  bool signal_changed = optional_bool(item(timelapse, "is_paused"), timelapse_paused);
  signal_changed = optional_bool(item(timelapse, "takingframe"), timelapse_taking) || signal_changed;
  if (signal_changed && timelapse_supported)
    pause.signal(timelapse_paused || timelapse_taking, now_ms());
  const auto previous_state = status.jobState;
  const auto previous_filename = status.filename;
  const bool previous_speed_valid = status.speedFactorValid,
             previous_flow_valid = status.flowFactorValid,
             previous_fan_valid = status.fanSpeedValid;
  const float previous_speed = status.speedPercent,
              previous_flow = status.flowPercent,
              previous_fan = status.fanPercent;
  const auto apply_delta = [&](const MoonrakerStatusDelta<std::string> &value) {
    const auto previous = status.printState();
    const bool speed_valid = status.speedFactorValid,
               flow_valid = status.flowFactorValid,
               fan_valid = status.fanSpeedValid;
    status.apply(value);
    if (previous != status.printState() ||
        (speed_valid && !status.speedFactorValid) ||
        (flow_valid && !status.flowFactorValid) ||
        (fan_valid && !status.fanSpeedValid)) ++tuning_epoch;
  };
  apply_delta(delta);
  last_event = eventtime;
  if (snapshot) {
    for (const auto &b : before_snapshot)
      if (b.time >= last_event) {
        apply_delta(b.delta);
        last_event = b.time;
      }
    before_snapshot.clear();
    subscribed = true;
    requests |= 64;
  }
  cJSON *th = item(obj, "toolhead");
  auto axes = item(th, "homed_axes");
  if (cJSON_IsString(axes)) {
    std::string a = text(axes);
    homed_x = a.find('x') != std::string::npos;
    homed_y = a.find('y') != std::string::npos;
    homed_z = a.find('z') != std::string::npos;
  }
  auto position = item(th, "position");
  float px{}, py{}, pz{};
  if (cJSON_GetArraySize(position) >= 3 &&
      coordinate(cJSON_GetArrayItem(position, 0), px) &&
      coordinate(cJSON_GetArrayItem(position, 1), py) &&
      coordinate(cJSON_GetArrayItem(position, 2), pz)) {
    position_x = px;
    position_y = py;
    position_z = pz;
  }
  auto min = item(th, "axis_minimum"), max = item(th, "axis_maximum");
  float minx{}, miny{}, maxx{}, maxy{};
  if (cJSON_GetArraySize(min) >= 2 && cJSON_GetArraySize(max) >= 2 &&
      coordinate(cJSON_GetArrayItem(min, 0), minx) &&
      coordinate(cJSON_GetArrayItem(min, 1), miny) &&
      coordinate(cJSON_GetArrayItem(max, 0), maxx) &&
      coordinate(cJSON_GetArrayItem(max, 1), maxy)) {
    bed_min_x = minx;
    bed_min_y = miny;
    bed_max_x = maxx;
    bed_max_y = maxy;
    bed_bounds_valid = bed_max_x > bed_min_x && bed_max_y > bed_min_y;
  }
  const bool new_job = status.filename.size() &&
      (job_id == 0 || previous_filename != status.filename ||
       ((status.jobState == "printing" || status.jobState == "paused") &&
        (terminalJob(previous_state) || previous_state == "standby")));
  if (new_job) {
    ++tuning_epoch;
    if (++job_id == 0) ++job_id;
    job_filename = status.filename;
    job_duration = status.printDuration;
    job_progress = status.progress * 100;
    job_metadata = metadata && metadata->filename == status.filename ? metadata : nullptr;
    if (!job_metadata) {
      job_metadata_file = status.filename;
      requests |= 32;
    }
  }
  if (!status.filename.empty() && status.filename == job_filename) {
    job_duration = status.printDuration;
    job_progress = status.progress * 100;
  }
  const auto effective = status.printState();
  const bool failed = (effective == "error" || effective == "shutdown") &&
      (status.jobState == "printing" || status.jobState == "paused" || status.jobState == "error");
  const auto result_state = public_state(failed ? "error" : status.jobState, true);
  if (!job_filename.empty() && (terminalJob(status.jobState) || failed) &&
      (!last_job.valid || last_job.job_id != job_id || last_job.result != result_state)) {
    last_job = {};
    last_job.valid = true;
    last_job.revision = ++last_job_revision;
    last_job.endpoint_generation = generation;
    last_job.job_id = job_id;
    last_job.result = result_state;
    copy_text(last_job.filename, sizeof(last_job.filename), job_filename);
    last_job.print_duration = job_duration;
    last_job.progress = job_progress;
    last_job_metadata = job_metadata;
  }
  pause.update(status.printState(), now_ms(), snapshot);
  connected = subscribed && status.klippy.state == "ready";
  if (!connected) invalidate_tuning();
  if (snapshot || previous_speed_valid != status.speedFactorValid ||
      previous_flow_valid != status.flowFactorValid || previous_fan_valid != status.fanSpeedValid ||
      previous_speed != status.speedPercent || previous_flow != status.flowPercent ||
      previous_fan != status.fanPercent)
    ESP_LOGI("klipper", "PANEL_TUNING speed_valid=%u speed=%.2f flow_valid=%u flow=%.2f fan_valid=%u fan=%.2f",
             unsigned(status.speedFactorValid), double(status.speedPercent),
             unsigned(status.flowFactorValid), double(status.flowPercent),
             unsigned(status.fanSpeedValid), double(status.fanPercent));
  error.clear();
  ++revision;
  return true;
}
void Client::message(const std::string &payload) {
  cJSON *doc = cJSON_ParseWithLength(payload.data(), payload.size());
  if (!cJSON_IsObject(doc) ||
      std::strcmp(text(item(doc, "jsonrpc")), "2.0") != 0) {
    cJSON_Delete(doc);
    restart = true;
    unavailable("JSON-RPC WebSocket invalido");
    return;
  }
  const std::string method = text(item(doc, "method"));
  if (method == "notify_status_update") {
    auto params = item(doc, "params"), time = cJSON_GetArrayItem(params, 1);
    if (!cJSON_IsNumber(time) ||
        !apply(cJSON_GetArrayItem(params, 0), number(time, -1), false)) {
      restart = true;
      unavailable("Actualizacion WebSocket invalida");
    }
  } else if (method == "notify_klippy_ready" ||
             method == "notify_klippy_shutdown" ||
             method == "notify_klippy_disconnected") {
    const bool shutdown = method == "notify_klippy_shutdown";
    {
      std::lock_guard<std::mutex> lock(mutex);
      if (ws_generation != generation || !current(generation)) {
        cJSON_Delete(doc);
        return;
      }
      if (!shutdown) {
        subscribed = false;
        timelapse_supported = timelapse_taking = timelapse_paused = false;
        last_event = -1;
      }
      server_id = printer_id = subscribe_id = timelapse_id = 0;
      connected = false;
      invalidate_tuning();
      status.klippy.apply({true, shutdown ? "shutdown" :
                          method == "notify_klippy_ready" ? "ready" : "disconnected"});
      if (shutdown) {
        for (const auto &b : before_snapshot) {
          const auto &state = b.delta.klippyState;
          if (b.time >= last_event && state.present &&
              (state.value == "shutdown" || state.value == "error")) {
            status.klippy.apply(state, b.delta.stateMessage);
            last_event = b.time;
          }
        }
      }
      before_snapshot.clear();
      if (shutdown && !job_filename.empty() &&
          (status.jobState == "printing" || status.jobState == "paused")) {
        last_job = {};
        last_job.valid = true;
        last_job.revision = ++last_job_revision;
        last_job.endpoint_generation = generation;
        last_job.job_id = job_id;
        last_job.result = KLIPPER_ERROR;
        copy_text(last_job.filename, sizeof(last_job.filename), job_filename);
        last_job.print_duration = job_duration;
        last_job.progress = job_progress;
        last_job_metadata = job_metadata;
      }
      ++revision;
    }
    if (method == "notify_klippy_ready")
      subscribe();
    else
      probe_klippy(shutdown);
  } else if (method.empty()) {
    uint32_t id = uint32_t(number(item(doc, "id")));
    bool si = server_id && id == server_id, pi = printer_id && id == printer_id,
         sub = subscribe_id && id == subscribe_id,
         ti = timelapse_id && id == timelapse_id;
    if (ti) {
      timelapse_id = 0;
      auto result = item(doc, "result"), time = item(result, "eventtime");
      if (item(doc, "error") || !cJSON_IsNumber(time) ||
          !apply(item(result, "status"), number(time, -1), false))
        timelapse_supported = false;
    } else if (si || pi || sub) {
      if (si)
        server_id = 0;
      if (pi)
        printer_id = 0;
      if (sub)
        subscribe_id = 0;
      auto result = item(doc, "result"), err = item(doc, "error");
      if (err) {
        unavailable(std::string("Moonraker: ") + text(item(err, "message")));
        if (!pi)
          restart = true;
      } else if (sub) {
        auto time = item(result, "eventtime");
        if (!cJSON_IsNumber(time) ||
            !apply(item(result, "status"), number(time, -1), true)) {
          restart = true;
          unavailable("Instantanea inicial invalida");
        } else
          printer_id = 0;
      } else {
        const auto state = status_text(result, si ? "klippy_state" : "state");
        if (!state.present) {
          restart = true;
          unavailable("Respuesta de estado invalida");
        } else {
          const auto detail = pi ? status_text(result, "state_message")
                                 : StatusField<std::string>{};
          {
            std::lock_guard<std::mutex> lock(mutex);
            const bool was_faulted = status.klippy.faulted();
            if (ws_generation != generation || !current(generation) ||
                !status.klippy.applyProbe(state_rpc_revision, state, detail, si)) {
              cJSON_Delete(doc);
              return;
            }
            connected = false;
            invalidate_tuning();
            if (status.klippy.state == "disconnected" || status.klippy.state == "startup" ||
                (status.klippy.state == "ready" && was_faulted)) {
              subscribed = false;
              timelapse_supported = timelapse_taking = timelapse_paused = false;
              last_event = -1;
              before_snapshot.clear();
            }
            error.clear();
            ++revision;
          }
          if (state.value == "ready")
            subscribe();
          else if (si && state.value != "disconnected")
            probe_klippy(true);
        }
      }
    }
  }
  cJSON_Delete(doc);
}
void Client::event_cb(void *arg, esp_event_base_t, int32_t id, void *data) {
  static_cast<Client *>(arg)->ws_event(
      id, static_cast<esp_websocket_event_data_t *>(data));
}
void Client::ws_event(int32_t id, esp_websocket_event_data_t *ev) {
  std::lock_guard<std::recursive_mutex> lock(protocol);
  if (!current(ws_generation))
    return;
  if (id == WEBSOCKET_EVENT_CONNECTED) {
    socket_open = true;
    subscribed = false;
    server_id = printer_id = subscribe_id = timelapse_id = 0;
    timelapse_supported = timelapse_taking = timelapse_paused = false;
    last_event = -1;
    before_snapshot.clear();
    frame.clear();
    fragmented = false;
    unavailable("WebSocket conectado: sincronizando");
    probe_klippy();
    return;
  }
  if (id == WEBSOCKET_EVENT_DISCONNECTED || id == WEBSOCKET_EVENT_ERROR ||
      id == WEBSOCKET_EVENT_CLOSED) {
    socket_open = false;
    subscribed = false;
    server_id = printer_id = subscribe_id = timelapse_id = 0;
    timelapse_supported = timelapse_taking = timelapse_paused = false;
    frame.clear();
    fragmented = false;
    unavailable("WebSocket desconectado: recuperando conexion");
    return;
  }
  if (id != WEBSOCKET_EVENT_DATA || !ev || restart)
    return;
  if (ev->op_code == 9 || ev->op_code == 10)
    return; // Control frames can interrupt text frames.
  if (ev->op_code == 8)
    return;
  if (ev->payload_offset == 0) {
    if (ev->op_code == 1) {
      if (fragmented) {
        restart = true;
        return;
      }
      frame.clear();
      fragmented = true;
      fragment_started = now_ms();
    } else if (ev->op_code != 0 || !fragmented) {
      restart = true;
      return;
    }
    frame_received = 0;
  }
  if (!fragmented || ev->data_len < 0 || ev->payload_len < 0 ||
      ev->payload_offset < 0 || size_t(ev->payload_offset) != frame_received ||
      frame.size() + ev->data_len > 32768) {
    restart = true;
    unavailable("Frame WebSocket invalido o demasiado grande");
    return;
  }
  if (ev->data_len)
    frame.append(ev->data_ptr, ev->data_len);
  frame_received += ev->data_len;
  if (frame_received == size_t(ev->payload_len) && ev->fin) {
    fragmented = false;
    message(frame);
    frame.clear();
  }
}
void Client::ws_worker() {
  uint64_t retry = 0;
  while (true) {
    device_network_snapshot_t net{};
    device_network_snapshot(&net);
    sync_endpoint(net);
    bool reset = restart || ws_generation != net.endpoint_generation ||
                 !net.has_ip || !net.moonraker_host[0];
    if (ws && reset) {
      esp_websocket_client_stop(ws);
      esp_websocket_client_destroy(ws);
      ws = nullptr;
      socket_open = false;
      subscribed = false;
      restart = false;
      retry = now_ms() + 1000;
    }
    if (!net.has_ip || !net.moonraker_host[0]) {
      std::lock_guard<std::mutex> lock(mutex);
      connected = false;
      invalidate_tuning();
      error = net.moonraker_host[0] ? "WiFi sin IP"
                                    : "Configure Moonraker por consola";
    }
    if (!ws && net.has_ip && net.moonraker_host[0] && now_ms() >= retry) {
      std::lock_guard<std::recursive_mutex> lock(protocol);
      ws_generation = net.endpoint_generation;
      std::string uri = "ws://" + std::string(net.moonraker_host) + ":" +
                        std::to_string(net.moonraker_port) + "/websocket";
      esp_websocket_client_config_t cfg{};
      cfg.uri = uri.c_str();
      cfg.task_stack = 8192;
      cfg.task_prio = 3;
      cfg.buffer_size = 2048;
      cfg.reconnect_timeout_ms = 5000;
      cfg.network_timeout_ms = 3000;
      cfg.ping_interval_sec = 15;
      cfg.pingpong_timeout_sec = 5;
      cfg.enable_close_reconnect = true;
      ws = esp_websocket_client_init(&cfg);
      if (ws) {
        esp_websocket_register_events(ws, WEBSOCKET_EVENT_ANY, event_cb, this);
        if (esp_websocket_client_start(ws) != ESP_OK) {
          esp_websocket_client_destroy(ws);
          ws = nullptr;
        }
      }
      if (!ws) {
        unavailable("Sin recursos para WebSocket");
        retry = now_ms() + 5000;
      }
    }
    if (ws) {
      std::lock_guard<std::recursive_mutex> lock(protocol);
      if (socket_open) {
        auto now = now_ms();
        if (timelapse_id && now - rpc_sent >= 5000) {
          timelapse_id = 0;
          timelapse_supported = false;
        }
        if (((server_id || printer_id || subscribe_id) &&
             now - rpc_sent >= 10000) ||
            (fragmented && now - fragment_started >= 5000)) {
          restart = true;
          unavailable("Respuesta WebSocket incompleta");
        } else if (!server_id && !printer_id && !subscribe_id &&
                   now - bootstrap >= 2000) {
          bool needs_probe;
          {
            std::lock_guard<std::mutex> lock(mutex);
            needs_probe = !subscribed || status.klippy.faulted();
          }
          if (needs_probe) probe_klippy();
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
} // namespace klipper
