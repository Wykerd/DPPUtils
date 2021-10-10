// Copyright (c) 2016 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.
#include "dpputils/audio/partial_buffer_reader.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <utility>
#include <vector>

#include "webm/status.h"

namespace webm
{

    PartialBufferReader::PartialBufferReader(std::initializer_list<std::uint8_t> bytes)
            : data_(bytes) {}

    PartialBufferReader::PartialBufferReader(const std::vector<std::uint8_t> &vector)
            : data_(vector) {}

    PartialBufferReader::PartialBufferReader(std::vector<std::uint8_t> &&vector)
            : data_(std::move(vector)) {}

    PartialBufferReader::PartialBufferReader(PartialBufferReader &&other)
            : data_(std::move(other.data_)), pos_(other.pos_)
    {
        other.pos_ = 0;
    }

    PartialBufferReader &PartialBufferReader::operator=(PartialBufferReader &&other)
    {
        if (this != &other)
        {
            data_ = std::move(other.data_);
            pos_ = other.pos_;
            other.pos_ = 0;
        }
        return *this;
    }

    PartialBufferReader &PartialBufferReader::operator=(
            std::initializer_list<std::uint8_t> bytes)
    {
        data_ = std::vector<std::uint8_t>(bytes);
        pos_ = 0;
        return *this;
    }

    void PartialBufferReader::PushChunk(const std::uint8_t *chunk, std::size_t size)
    {
        for (std::size_t i = 0; i < size; i++)
            this->data_.push_back(chunk[i]);
    }

    Status PartialBufferReader::Read(std::size_t num_to_read, std::uint8_t *buffer,
                                                                     std::uint64_t *num_actually_read)
    {
        assert(num_to_read > 0);
        assert(buffer != nullptr);
        assert(num_actually_read != nullptr);

        *num_actually_read = 0;
        std::size_t expected = num_to_read;

        std::size_t num_remaining = data_.size();
        if (num_remaining == 0)
        {
            if (this->complete_)
                return Status(Status::kEndOfFile);
            else
                return Status(Status::kWouldBlock);
        }

        if (num_to_read > num_remaining)
        {
            if (!this->complete_)
                return Status(Status::kWouldBlock);
            num_to_read = static_cast<std::size_t>(num_remaining);
        }

        std::copy_n(data_.data(), num_to_read, buffer);
        *num_actually_read = num_to_read;
        pos_ += num_to_read;

        if (*num_actually_read != expected)
        {
            for (uint64_t i = 0; i < *num_actually_read; ++i)
                data_.erase(data_.begin());
            return Status(Status::kOkPartial);
        }

        for (uint64_t i = 0; i < *num_actually_read; ++i)
            data_.erase(data_.begin());
        return Status(Status::kOkCompleted);
    }

    Status PartialBufferReader::Skip(std::uint64_t num_to_skip,
                                                                     std::uint64_t *num_actually_skipped)
    {
        assert(num_to_skip > 0);
        assert(num_actually_skipped != nullptr);

        *num_actually_skipped = 0;
        std::uint64_t expected = num_to_skip;

        std::size_t num_remaining = data_.size();
        if (num_remaining == 0)
        {
            if (this->complete_)
                return Status(Status::kEndOfFile);
            else
                return Status(Status::kWouldBlock);
        }

        if (num_to_skip > num_remaining)
        {
            if (!this->complete_)
                return Status(Status::kWouldBlock);
            num_to_skip = static_cast<std::uint64_t>(num_remaining);
        }

        *num_actually_skipped = num_to_skip;
        pos_ += num_to_skip;
        for (uint64_t i = 0; i < *num_actually_skipped; ++i)
            data_.erase(data_.begin());

        if (*num_actually_skipped != expected)
        {
            return Status(Status::kOkPartial);
        }

        return Status(Status::kOkCompleted);
    }

    std::uint64_t PartialBufferReader::Position() const { return pos_; }

} // namespace webm
