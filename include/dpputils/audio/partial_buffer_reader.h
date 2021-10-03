// Based on BufferReader from libwebm under the license:
//
// Copyright (c) 2016 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.
#ifndef INCLUDE_WEBM_P_BUFFER_READER_H_
#define INCLUDE_WEBM_P_BUFFER_READER_H_

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <vector>

#include "webm/reader.h"
#include "webm/status.h"

/**
 \file
 A `Reader` implementation that reads from a `std::vector<std::uint8_t>`.
 */

namespace webm {

/**
 \addtogroup PUBLIC_API
 @{
 */

/**
 A simple reader that reads data from a buffer of bytes.
 */
class PartialBufferReader : public Reader {
 public:
  /**
   Constructs a new, empty reader.
   */
  PartialBufferReader() = default;

  /**
   Constructs a new reader by copying the provided reader into the new reader.

   \param other The source reader to copy.
   */
  PartialBufferReader(const PartialBufferReader& other) = default;

  /**
   Copies the provided reader into this reader.

   \param other The source reader to copy. May be equal to `*this`, in which
   case this is a no-op.
   \return `*this`.
   */
  PartialBufferReader& operator=(const PartialBufferReader& other) = default;

  /**
   Constructs a new reader by moving the provided reader into the new reader.

   \param other The source reader to move. After moving, it will be reset to an
   empty stream.
   */
  PartialBufferReader(PartialBufferReader&&);

  /**
   Moves the provided reader into this reader.

   \param other The source reader to move. After moving, it will be reset to an
   empty stream. May be equal to `*this`, in which case this is a no-op.
   \return `*this`.
   */
  PartialBufferReader& operator=(PartialBufferReader&&);

  /**
   Creates a new `PartialBufferReader` populated with the provided bytes.

   \param bytes Bytes that are assigned to the internal buffer and used as the
   source which is read from.
   */
  PartialBufferReader(std::initializer_list<std::uint8_t> bytes);

  /**
   Creates a new `PartialBufferReader` populated with the provided data.

   \param vector A vector of bytes that is copied to the internal buffer and
   used as the source which is read from.
   */
  explicit PartialBufferReader(const std::vector<std::uint8_t>& vector);

  /**
   Creates a new `PartialBufferReader` populated with the provided data.

   \param vector A vector of bytes that is moved to the internal buffer and used
   as the source which is read from.
   */
  explicit PartialBufferReader(std::vector<std::uint8_t>&& vector);

  /**
   Resets the reader to read from the given list of bytes, starting at the
   beginning.

   This makes `reader = {1, 2, 3};` effectively equivalent to `reader =
   PartialBufferReader({1, 2, 3});`.

   \param bytes Bytes that are assigned to the internal buffer and used as the
   source which is read from.
   \return `*this`.
   */
  PartialBufferReader& operator=(std::initializer_list<std::uint8_t> bytes);

  Status Read(std::size_t num_to_read, std::uint8_t* buffer,
              std::uint64_t* num_actually_read) override;

  Status Skip(std::uint64_t num_to_skip,
              std::uint64_t* num_actually_skipped) override;

  std::uint64_t Position() const override;

  void PushChunk(const std::uint8_t *chunk, std::size_t size);

  /**
   Gets the total size of the buffer.
   */
  std::size_t size() { return data_.size(); }

  void SetComplete() { complete_ = true; }

 private:
  // Stores the byte buffer from which data is read.
  std::vector<std::uint8_t> data_;

  // The position of the reader in the byte buffer.
  std::size_t pos_ = 0;

  bool complete_ = false;
};

/**
 @}
 */

}  // namespace webm

#endif  // INCLUDE_WEBM_P_BUFFER_READER_H_
