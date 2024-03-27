#pragma once

#include <vector>
// TODO(fyp): remove after dubugging
#include <utility>
#include <set>
#include <unordered_map>
#include <iostream>

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
    finished_(false),
    duplicated_keys_(),
    exist_keys_() {}

  // Add a new point to trainer_. Increment num_data_blocks by 1.
  // This assumes AddPLRTrainingPoint() is called in the sorted order of
  // data blocks.
  // REQUIRES: !finished_
  void AddPLRTrainingPoint(const Slice& first_key_in_data_block) {
    assert(!finished_);
    
    long double first_key_floating_rep = Str2Double(
      first_key_in_data_block.data(), first_key_in_data_block.size());
    Point<long double> p(first_key_floating_rep, num_data_blocks_++);
    trainer_.process(p);
    // TODO(fyp): remove after debugging
    if (exist_keys_.count(first_key_floating_rep) > 0) {
      duplicated_keys_.insert(first_key_floating_rep);
    }
    else {
      exist_keys_[first_key_floating_rep] = std::vector<std::string>();
    }
    exist_keys_[first_key_floating_rep].emplace_back(first_key_in_data_block.ToString());
  }

  void AddPLRIntermediateTrainingPoint(const Slice& non_first_key) {
    assert(!finished_);

    long double key_floating_rep = Str2Double(
      non_first_key.data(), non_first_key.size());
    // TODO(fyp): remove after debugging
    if (exist_keys_.count(key_floating_rep) > 0) {
      duplicated_keys_.insert(key_floating_rep);
      exist_keys_[key_floating_rep].emplace_back(first_key_in_data_block.ToString());
      return;
    }
    else {
      exist_keys_[key_floating_rep] = std::vector<std::string>();
      exist_keys_[key_floating_rep].emplace_back(first_key_in_data_block.ToString());
    }

    trainer_.AddNonFirstKey(key_floating_rep);
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

    std::vector<Segment<long double, long double>> segments = trainer_.finish();
    auto plr_param_encoder = PLRDataRep<long double, long double>(gamma_, segments);

    buffer_ = plr_param_encoder.Encode();
    data_block_handles_encoder_.Encode(&buffer_);
    finished_ = true;

    // TODO(fyp): remove after debugging
    if (!duplicated_keys_.empty()) {
      std::cout << "Following keys are duplicated:" << std::endl;
      for (const auto& k: duplicated_keys_) {
        std::cout << "Key double representation: " << k << std::endl;
        for (const auto& key_string: exist_keys_[k]) {
          std::cout << "string representation: " << key_string << std::endl;
          std::cout << "byte representation: ";
          for (size_t i = 0; i < key_string.size(); ++i) {
            std::cout << (unsigned int) ((unsigned char) key_string[i]) << ";";
          }
          std::cout << std::endl;
        }
      }
    }

    return Slice(buffer_);
  }

 private:
  // TODO(fyp): reading non-active member from union is UB, although most
  // compiler defined its behavior as a non-standard extension?
  long double Str2Double(const char* str, size_t size) {
    // assert(size <= 8);
    // uint64_t int_rep = 0;
    // for (size_t i = 0; i < size; ++i) {
    //   int_rep <<= 8;
    //   int_rep += 0xff & str[i];
    // }
    // int_rep <<= 8 * (8 - size);
    std::string s(str, size);

    uint64_t int_rep = stringToNumber<uint64_t>(s);
    return static_cast<long double>(int_rep);
  }

  // GreedyPLR<uint64_t, double> trainer_;
  GreedyPLR<long double, long double> trainer_;
  DataBlockHandlesEncoder data_block_handles_encoder_;
  uint32_t num_data_blocks_;
  double gamma_;
  // Make sure this class is not deleted before buffer_ referenced by
  // return value of this->Finish() is written to disk; otherwise, that
  // slice (return value) contains a dangling pointer.
  std::string buffer_;
  bool added_first_data_block_offset_;
  bool finished_;
  // TODO(fyp): remove these after debugging
  std::set<long double> duplicated_keys_;
  std::unordered_map<long double, std::vector<std::string>> exist_keys_;
};
}