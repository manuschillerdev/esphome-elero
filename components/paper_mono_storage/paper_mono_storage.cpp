#include "paper_mono_storage.h"
#include "esphome/core/hal.h"
#include "driver/sdmmc_host.h"
#include "esp_vfs_fat.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

namespace esphome::paper_mono_storage {
static const char *const TAG = "paper_mono.storage";
static constexpr size_t ELERO_MAX_FILE_SIZE = 128 * 1024;
static constexpr size_t ELERO_MAX_FILES = 64;
static constexpr const char *ELERO_MOUNT_PATH = "/paper_sd";

bool Storage::initialize_() {
  if (task_)
    return true;
  requests_ = xQueueCreate(1, sizeof(Result *));
  results_ = xQueueCreate(1, sizeof(Result *));
  if (requests_ && results_ &&
      xTaskCreate([](void *self) { static_cast<Storage *>(self)->run_(); }, "paper_sd", 6144, this, 1, &task_) ==
          pdPASS)
    return true;
  if (requests_)
    vQueueDelete(requests_);
  if (results_)
    vQueueDelete(results_);
  requests_ = results_ = nullptr;
  task_ = nullptr;
  error_ = "Cannot start SD worker";
  ESP_LOGE(TAG, "%s", error_.c_str());
  return false;
}

bool Storage::start(Operation operation, const std::string &filename, std::string data) {
  if (busy_) {
    error_ = "Storage busy";
    return false;
  }
  if (!initialize_())
    return false;
  auto job = std::make_unique<Result>();
  job->operation = operation;
  job->filename = filename;
  job->data = std::move(data);
  auto *pointer = job.get();
  if (xQueueSend(requests_, &pointer, 0) != pdPASS) {
    error_ = "Storage queue full";
    return false;
  }
  job.release();
  busy_ = true;
  error_.clear();
  return true;
}

std::unique_ptr<Result> Storage::take_result() {
  Result *result = nullptr;
  if (!results_ || xQueueReceive(results_, &result, 0) != pdPASS)
    return nullptr;
  busy_ = false;
  return std::unique_ptr<Result>(result);
}

void Storage::run_() {
  for (;;) {
    Result *job = nullptr;
    if (xQueueReceive(requests_, &job, portMAX_DELAY) != pdPASS)
      continue;
    const uint32_t started = millis();
    execute_(*job);
    ESP_LOGD(TAG, "SD operation %u completed in %ums", static_cast<unsigned>(job->operation), millis() - started);
    if (!job->error.empty())
      ESP_LOGW(TAG, "%s", job->error.c_str());
    // One outstanding operation: the result queue is empty until this worker sends.
    xQueueSend(results_, &job, portMAX_DELAY);
  }
}

bool Storage::mount_(Result &job) {
  if (!board_ || !board_->prepare_sd()) {
    job.error = "SD power unavailable";
    return false;
  }
  const auto inserted = board_->sd_inserted();
  if (!inserted.has_value()) {
    job.error = "SD card detection failed";
    return false;
  }
  if (!*inserted) {
    if (card_) {
      esp_vfs_fat_sdcard_unmount(ELERO_MOUNT_PATH, card_);
      card_ = nullptr;
    }
    job.error = "Insert microSD card";
    return false;
  }
  if (card_)
    return true;
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
  slot.width = 4;
  slot.clk = GPIO_NUM_13;
  slot.cmd = GPIO_NUM_12;
  slot.d0 = GPIO_NUM_11;
  slot.d1 = GPIO_NUM_10;
  slot.d2 = GPIO_NUM_9;
  slot.d3 = GPIO_NUM_8;
  esp_vfs_fat_sdmmc_mount_config_t config{};
  config.format_if_mount_failed = false;
  config.max_files = 3;
  config.allocation_unit_size = 16384;
  auto result = esp_vfs_fat_sdmmc_mount(ELERO_MOUNT_PATH, &host, &slot, &config, &card_);
  if (result == ESP_OK)
    return true;
  card_ = nullptr;
  job.error = std::string("SD mount: ") + esp_err_to_name(result);
  return false;
}

void Storage::execute_(Result &job) {
  if (!mount_(job))
    return;
  switch (job.operation) {
    case Operation::LIST:
      list_(job);
      break;
    case Operation::READ:
      read_(job);
      break;
    case Operation::WRITE:
      write_(job);
      break;
  }
  if (!job.error.empty() && card_) {
    esp_vfs_fat_sdcard_unmount(ELERO_MOUNT_PATH, card_);
    card_ = nullptr;  // Retry a fresh mount after removal or I/O failure.
  }
}

void Storage::list_(Result &job) {
  std::unique_ptr<DIR, decltype(&closedir)> dir(opendir(ELERO_MOUNT_PATH), closedir);
  if (!dir) {
    job.error = "Cannot read SD directory";
    return;
  }
  errno = 0;
  while (auto *entry = readdir(dir.get())) {
    std::string name = entry->d_name;
    if (name.size() > 5 && (name.ends_with(".json") || name.ends_with(".JSON"))) {
      if (job.files.size() == ELERO_MAX_FILES) {
        job.error = "Too many backups (maximum 64)";
        job.files.clear();
        return;
      }
      job.files.push_back(name);
    }
    errno = 0;
  }
  if (errno) {
    job.error = "SD directory read failed";
    job.files.clear();
  }
  std::sort(job.files.begin(), job.files.end());
}

void Storage::read_(Result &job) {
  if (job.filename.empty() || job.filename.find_first_of("/\\") != std::string::npos ||
      job.filename.find("..") != std::string::npos) {
    job.error = "Invalid backup filename";
    return;
  }
  const auto path = std::string(ELERO_MOUNT_PATH) + "/" + job.filename;
  std::unique_ptr<FILE, decltype(&fclose)> file(fopen(path.c_str(), "rb"), fclose);
  if (!file) {
    job.error = "Cannot open backup";
    return;
  }
  char chunk[1024];
  while (size_t count = fread(chunk, 1, sizeof(chunk), file.get())) {
    if (job.data.size() + count > ELERO_MAX_FILE_SIZE) {
      job.error = "Backup exceeds 128 KB limit";
      job.data.clear();
      return;
    }
    job.data.append(chunk, count);
    vTaskDelay(1);
  }
  if (ferror(file.get())) {
    job.error = "Backup read failed";
    job.data.clear();
  } else if (job.data.empty())
    job.error = "Backup is empty";
}

void Storage::write_(Result &job) {
  if (job.data.empty() || job.data.size() > ELERO_MAX_FILE_SIZE) {
    job.error = "Backup size invalid";
    return;
  }
  std::string path, temporary;
  int fd = -1;
  for (int attempt = 0; attempt < 16 && fd < 0; ++attempt) {
    char name[24];
    snprintf(name, sizeof(name), "%08X.json", random_uint32());
    job.filename = name;
    path = std::string(ELERO_MOUNT_PATH) + "/" + name;
    temporary = path + ".tmp";
    struct stat existing{};
    if (stat(path.c_str(), &existing) == 0)
      continue;
    if (errno != ENOENT)
      break;
    fd = open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0 && errno != EEXIST)
      break;
  }
  FILE *stream = fd < 0 ? nullptr : fdopen(fd, "wb");
  if (!stream) {
    if (fd >= 0) {
      close(fd);
      unlink(temporary.c_str());
    }
    job.error = "Cannot create backup";
    return;
  }
  std::unique_ptr<FILE, decltype(&fclose)> file(stream, fclose);
  bool ok = true;
  for (size_t offset = 0; offset < job.data.size() && ok; offset += 1024) {
    const size_t length = std::min(size_t{1024}, job.data.size() - offset);
    ok = fwrite(job.data.data() + offset, 1, length, file.get()) == length;
    vTaskDelay(1);
  }
  ok = fflush(file.get()) == 0 && ok;
  ok = fsync(fileno(file.get())) == 0 && ok;
  ok = fclose(file.release()) == 0 && ok;
  // ESP-IDF FAT VFS delegates to f_rename, which fails if the destination exists.
  // A completed file becomes visible only here; existing backups are never replaced.
  if (ok)
    ok = rename(temporary.c_str(), path.c_str()) == 0;
  if (!ok) {
    unlink(temporary.c_str());
    job.error = "Backup write failed";
  }
  job.data.clear();
}

}  // namespace esphome::paper_mono_storage
