#pragma once

#include <cstddef>
#include <cstdint>

namespace knobify::library {

// Minimal file-reading surface tag parsers need. Deliberately not the
// Arduino File/SD API, so parsing logic can run against fake in-memory
// buffers on the host -- see docs/adr/0004-navigation-library-and-index-architecture.md.
class RawFile {
 public:
  virtual ~RawFile() = default;
  virtual size_t size() const = 0;
  virtual bool seek(size_t position) = 0;
  // Reads up to n bytes into buf, returns the number actually read
  // (fewer than n at end of file).
  virtual size_t read(uint8_t *buf, size_t n) = 0;
};

}  // namespace knobify::library
