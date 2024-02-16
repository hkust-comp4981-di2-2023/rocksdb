//TODO: the whole class
#pragma once

#include <algorithm>
#include <string>

#include "db/dbformat.h"
#include "include/rocksdb/status.h"
#include "rocksdb/comparator.h"
#include "rocksdb/iterator.h"
#include "rocksdb/status.h"
#include "table/format.h"
#include "table/internal_iterator.h"
#include "table/block_based/learned_index/plr/external/plr/library.h"
#include "coding.h"
#include "test_util/sync_point.h"
#include "stdint.h"

namespace ROCKSDB_NAMESPACE {

class BlockHandleCalculator {
 public:
	BlockHandleCalculator(const std::string& encoded_string, 
												const uint64_t num_data_blocks):
												data_block_handles_(),
												num_data_blocks_(num_data_blocks) {
		data_block_handles_.reserve(num_data_blocks_);
		Status s = Decode(encoded_string);
		assert(s.ok());
	}
	
	Status GetBlockHandle(const uint64_t data_block_number, 
												BlockHandle& block_handle) const;

	Status Decode(const std::string& encoded_string);

 private:
	std::vector<BlockHandle> data_block_handles_;
	const uint64_t num_data_blocks_;
	uint64_t first_data_block_offset_;
};
}// namespace ROCKSDB_NAMESPACE