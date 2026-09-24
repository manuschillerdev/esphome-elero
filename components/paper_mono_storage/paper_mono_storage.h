#pragma once

#include "esphome/components/paper_mono/paper_mono.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"
#include <memory>
#include <string>
#include <vector>

namespace esphome::paper_mono_storage {

enum class Operation : uint8_t { LIST, READ, WRITE };
struct Result {
  Operation operation;
  std::string filename;
  std::string data;
  std::vector<std::string> files;
  std::string error;
};

// Firmware-lifetime object. Only the worker accesses the filesystem; queues transfer
// exclusive ownership of jobs/results. No registry or frontend state crosses tasks.
class Storage {
 public:
  void set_board(paper_mono::PaperMono *board) { board_ = board; }
  bool start(Operation operation, const std::string &filename = "", std::string data = "");
  std::unique_ptr<Result> take_result();
  const std::string &error() const { return error_; }

 private:
  bool initialize_();
  bool mount_(Result &job);
  void run_();
  void execute_(Result &job);
  void list_(Result &job);
  void read_(Result &job);
  void write_(Result &job);
  paper_mono::PaperMono *board_{nullptr};
  QueueHandle_t requests_{nullptr};
  QueueHandle_t results_{nullptr};
  TaskHandle_t task_{nullptr};
  sdmmc_card_t *card_{nullptr};
  bool busy_{false};
  std::string error_;
};

}  // namespace esphome::paper_mono_storage
