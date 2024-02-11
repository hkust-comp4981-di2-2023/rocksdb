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

class BlockHandleCalculatorStub {
 public:
	BlockHandleCalculatorStub(uint64_t num_data_blocks,
								std::shared_ptr<uint64_t[]> block_sizes,
								uint64_t& begin_block, uint64_t& end_block): 
								num_data_blocks_(num_data_blocks) {
		Status s = GetAllDataBlockHandles(block_sizes, begin_block, end_block);
		assert(s == Status::OK());
	}

	Status GetAllDataBlockHandles(std::shared_ptr<uint64_t[]> block_sizes, 
									uint64_t& begin_block, uint64_t& end_block) {} /*get blockhandle of the plr range*/

	Status GetBlockHandle(uint64_t current, BlockHandle& block_handle) const {} /* get blockhandle of a particular block*/

	Status CalculateBlockHandle(uint64_t current, BlockHandle& block_handle) const {} /*very similar to GetBlockHandle*/

    void AddBlockToBlockHandle(uint64_t offset, uint64_t size) {} /* add blocks into the particular block*/

	uint64_t GetTotalNumDataBlock(){}

	Status Encode(std::string* encoded_ptr, std::vector<BlockHandle> handle) {} /*encode size and return the encoded string*/

	Status Decode_size(const std::string, std::shared_ptr<uint64_t[]> block_sizes) {} /**/
	
	friend class BlockBasedTableBuilder;

	private:
	std::vector<BlockHandle> handles_; /*the datablock size array*/
	const uint64_t num_data_blocks_ = 0; /* index starts from 0*/
	uint64_t total_num_data_blocks = 0;
};

}// namespace ROCKSDB_NAMESPACE