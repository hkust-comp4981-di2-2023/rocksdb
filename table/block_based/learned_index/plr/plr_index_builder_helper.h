#pragma once

#include <vector>

#include "table/format.h"
#include "util/coding.h"

namespace ROCKSDB_NAMESPACE {

class DataBlockHandlesEncoder {
 public:
  DataBlockHandlesEncoder() = default;

  DataBlockHandlesEncoder(uint64_t num_data_blocks): 
    data_block_sizes_(num_data_blocks) {}

  void AddHandle(const BlockHandle& handle) {
    data_block_sizes_.emplace_back(handle.size());
  }

  void Encode(std::string* encoded_string) {
    for (std::vector<uint64_t>::const_iterator it = data_block_sizes_.begin();
          it != data_block_sizes_.end(); it++) {
      assert(*it != 0);
      PutFixed64(encoded_string, *it);
    }
  }

 private:
  std::vector<uint64_t> data_block_sizes_;
};

}