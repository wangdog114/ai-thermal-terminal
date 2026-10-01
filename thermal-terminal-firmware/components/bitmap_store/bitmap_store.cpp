#include "bitmap_store/bitmap_store.hpp"

#include <cstdlib>
#include <cstring>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

namespace thermal_terminal {
namespace {

std::uint8_t *allocate_bitmap(std::size_t size) {
#ifdef ESP_PLATFORM
  return static_cast<std::uint8_t *>(
      heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#else
  return static_cast<std::uint8_t *>(std::malloc(size));
#endif
}

void free_bitmap(void *pointer) {
#ifdef ESP_PLATFORM
  heap_caps_free(pointer);
#else
  std::free(pointer);
#endif
}

} // namespace

BitmapStore::~BitmapStore() { reset(); }

bool BitmapStore::begin_document(const tpb1::DocumentInfo &info) {
  reset();
  if (info.raw_length == 0 || info.raw_length > tpb1::kMaximumBitmapBytes) {
    return false;
  }
  bytes_ = allocate_bitmap(info.raw_length);
  if (bytes_ == nullptr)
    return false;
  std::memset(bytes_, 0, info.raw_length);
  size_ = info.raw_length;
  document_ = info;
  return true;
}

bool BitmapStore::write_band(const tpb1::BandInfo &info,
                             const std::uint8_t *data, std::size_t size) {
  if (bytes_ == nullptr || ready_ || data == nullptr ||
      info.index != next_band_ || size != info.raw_length) {
    return false;
  }
  const std::size_t offset = static_cast<std::size_t>(info.y) * tpb1::kRowBytes;
  if (offset != written_ || size > size_ - written_)
    return false;
  std::memcpy(bytes_ + offset, data, size);
  written_ += size;
  ++next_band_;
  return true;
}

bool BitmapStore::end_document() {
  if (bytes_ == nullptr || written_ != size_ ||
      next_band_ != document_.band_count) {
    return false;
  }
  ready_ = true;
  return true;
}

void BitmapStore::reset() {
  if (bytes_ != nullptr)
    free_bitmap(bytes_);
  bytes_ = nullptr;
  size_ = 0;
  written_ = 0;
  next_band_ = 0;
  ready_ = false;
  document_ = {};
}

bool BitmapStore::ready() const { return ready_; }
const std::uint8_t *BitmapStore::data() const {
  return ready_ ? bytes_ : nullptr;
}
std::size_t BitmapStore::size() const { return ready_ ? size_ : 0; }
const tpb1::DocumentInfo &BitmapStore::document() const { return document_; }

const std::uint8_t *BitmapStore::row(std::uint32_t y) const {
  if (!ready_ || y >= document_.height)
    return nullptr;
  return bytes_ + static_cast<std::size_t>(y) * tpb1::kRowBytes;
}

} // namespace thermal_terminal
