#pragma once

#include <cstring>
#include <map>
#include <memory>
#include <vector>

#include "FileLister.h"
#include "FolderBrowser.h"
#include "LibraryScanner.h"

class InMemoryRawFile : public knobify::library::RawFile {
 public:
  explicit InMemoryRawFile(std::vector<uint8_t> data) : data_(std::move(data)) {}
  size_t size() const override { return data_.size(); }
  bool seek(size_t position) override {
    if (position > data_.size()) return false;
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

class FakeFileLister : public knobify::library::FileLister {
 public:
  explicit FakeFileLister(std::vector<knobify::library::FileEntry> entries)
      : entries_(std::move(entries)) {}

  void reset() override { index_ = 0; }

  bool next(knobify::library::FileEntry &out) override {
    if (index_ >= entries_.size()) return false;
    out = entries_[index_++];
    return true;
  }

 private:
  std::vector<knobify::library::FileEntry> entries_;
  size_t index_ = 0;
};

class FakeFileOpener : public knobify::library::FileOpener {
 public:
  void put(const std::string &path, std::vector<uint8_t> bytes) {
    files_[path] = std::move(bytes);
  }

  std::unique_ptr<knobify::library::RawFile> open(
      const std::string &path) override {
    auto it = files_.find(path);
    if (it == files_.end()) return nullptr;
    return std::make_unique<InMemoryRawFile>(it->second);
  }

 private:
  std::map<std::string, std::vector<uint8_t>> files_;
};

class FakeDirectoryReader : public knobify::library::DirectoryReader {
 public:
  void put(const std::string &path,
           std::vector<knobify::library::FolderEntry> entries) {
    dirs_[path] = std::move(entries);
  }

  std::vector<knobify::library::FolderEntry> listChildren(
      const std::string &path) override {
    auto it = dirs_.find(path);
    if (it == dirs_.end()) return {};
    return it->second;
  }

 private:
  std::map<std::string, std::vector<knobify::library::FolderEntry>> dirs_;
};
