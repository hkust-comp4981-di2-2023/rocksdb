#pragma once

#include <vector>

#include "db/dbformat.h"
#include "table/format.h"
#include "table/block_based/learned_index/plr/external/plr/library.h"
#include "util/coding.h"

namespace ROCKSDB_NAMESPACE {

class DataBlockHandlesEncoder {
 public:
  DataBlockHandlesEncoder() = default;
  
  void AddHandleOffset(const BlockHandle& handle) {
    first_data_block_offset_ = handle.offset();
  }

  void AddHandleSize(const BlockHandle& handle) {
    data_block_sizes_.emplace_back(handle.size());
  }

  void Encode(std::string* encoded_string) {
    PutFixed64(encoded_string, first_data_block_offset_);
    for (std::vector<uint64_t>::const_iterator it = data_block_sizes_.begin();
          it != data_block_sizes_.end(); it++) {
      assert(*it != 0);
      PutFixed64(encoded_string, *it);
    }
  }

 private:
  uint64_t first_data_block_offset_;
  std::vector<uint64_t> data_block_sizes_;
};

// This is a wrapper class of our standalone PLR module.
// It takes inputs from PLRIndexBuilder::AddIndexEntry() etc and outputs
// the actual index block content as a Slice.
//
// The helper instance should stay alive until the Slice is actually written
// to the file/disk, because the Slice returned relies on this->buffer_.
//
// Format of actual index block content:
// [Encoded string from PLRDataRep]
// [Encoded string from DataBlockHandlesEncoder]
//
// TODO(fyp): We only support uint64_t <-> 8 chars. Currently, we inserted a
// dirty hack to get rid of extra chars, inc. seq# in internal key, chars after
// the 8th char in user key. To remove this code debt, we need to enforce option
// to only disable include_seq# if using plr, and find some ways to ensure at
// most 8 chars for user key.
class PLRBuilderHelper {
 public:
  PLRBuilderHelper() = delete;

  PLRBuilderHelper(double gamma): 
    trainer_(gamma),
    data_block_handles_encoder_(),
    num_data_blocks_(0),
    gamma_(gamma),
    buffer_(),
    added_first_data_block_offset_(false),
    finished_(false) {}

  // Add a new point to trainer_. Increment num_data_blocks by 1.
  // This assumes AddPLRTrainingPoint() is called in the sorted order of
  // data blocks.
  // REQUIRES: !finished_
  void AddPLRTrainingPoint(const Slice& first_key_in_data_block) {
    assert(!finished_);
    
    double first_key_floating_rep = Str2Double(
      first_key_in_data_block.data(), first_key_in_data_block.size());
    Point<double> p(first_key_floating_rep, num_data_blocks_++);
    trainer_.process(p);
  }

  // Add the current data block handle's size() to encoder
  void AddHandle(const BlockHandle& block_handle) {
    if (!added_first_data_block_offset_) {
      data_block_handles_encoder_.AddHandleOffset(block_handle);
      added_first_data_block_offset_ = true;
    }
    data_block_handles_encoder_.AddHandleSize(block_handle);
  }

  // Create a function-scoped PLRDataRep for Encode() and return a Slice.
  // The returned Slice is the actual index block content.
  // REQUIRES: !finished_
  Slice Finish() {
    assert(!finished_);

    std::vector<Segment<uint64_t, double>> segments = trainer_.finish();
    auto plr_param_encoder = PLRDataRep<uint64_t, double>(gamma_, segments);

    buffer_ = plr_param_encoder.Encode();
    data_block_handles_encoder_.Encode(&buffer_);
    finished_ = true;
    return Slice(buffer_);
  }

 private:
  // TODO(fyp): reading non-active member from union is UB, although most
  // compiler defined its behavior as a non-standard extension?
  // TODO(fyp): remove dirty hack
  double Str2Double(const char* str, size_t size) {
    // dirty hack
    if (size >= 8) {
      Slice user_key = ExtractUserKey(Slice(str, size));
      size = user_key.size() > 8 ? 8 : user_key.size();
    }
    assert(size <= 8);
    // uint64_t int_rep = 0;
    // for (size_t i = 0; i < size; ++i) {
    //   int_rep <<= 8;
    //   int_rep += 0xff & str[i];
    // }
    // int_rep <<= 8 * (8 - size);
    std::string s(str, size);

    uint64_t int_rep = stringToNumber<uint64_t>(s);
    return (double) int_rep;
  }

  GreedyPLR<uint64_t, double> trainer_;
  DataBlockHandlesEncoder data_block_handles_encoder_;
  uint32_t num_data_blocks_;
  double gamma_;
  // Make sure this class is not deleted before buffer_ referenced by
  // return value of this->Finish() is written to disk; otherwise, that
  // slice (return value) contains a dangling pointer.
  std::string buffer_;
  bool added_first_data_block_offset_;
  bool finished_;
};
}