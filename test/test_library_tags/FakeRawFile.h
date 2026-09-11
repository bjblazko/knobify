#pragma once

#include <cstring>
#include <vector>

#include "RawFile.h"

// In-memory RawFile for host tests -- lets tag parsers be exercised
// against hand-built byte buffers with no filesystem involved.
class FakeRawFile : public knobify::library::RawFile {
 public:
  explicit FakeRawFile(std::vector<uint8_t> data) : data_(std::move(data)) {}

  size_t size() const override { return data_.size(); }

  bool seek(size_t position) override {
    if (position > data_.size()) {
      return false;
    }
    pos_ = position;
    return true;
  }

  size_t read(uint8_t *buf, size_t n) override {
    size_t available = data_.size() - pos_;
    size_t toRead = n < available ? n : available;
    std::memcpy(buf, data_.data() + pos_, toRead);
    pos_ += toRead;
    return toRead;
  }

 private:
  std::vector<uint8_t> data_;
  size_t pos_ = 0;
};
