#pragma once
#include "esphome/core/defines.h"
#ifdef USE_ELERO_PAPER_STORAGE
#include "esphome/components/paper_mono_storage/paper_mono_storage.h"
#include "esphome/components/elero/elero.h"

namespace esphome::elero_paper {
class PaperUi;
// JSON/registry access stays on Core 1; the hardware worker handles file I/O only.
class PaperStorage {
 public:
  void setup(paper_mono::PaperMono *board, elero::Elero *hub) {
    storage_.set_board(board);
    hub_ = hub;
  }
  std::string files();
  std::string transfer(bool restore, const std::string &filename);
  void loop(PaperUi &ui);

 private:
  std::string import_(const std::string &data);
  paper_mono_storage::Storage storage_;
  elero::Elero *hub_{nullptr};
};
}  // namespace esphome::elero_paper
#endif
