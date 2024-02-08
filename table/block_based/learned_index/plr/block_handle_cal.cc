#pragma once

#include <string>
#include "db/dbformat.h"
#include "rocksdb/comparator.h"
#include "rocksdb/iterator.h"
#include "rocksdb/status.h"
#include "table/format.h"
#include "table/internal_iterator.h"
#include "table/block_based/learned_index/plr/plr_block_iter.h"
#include "table/block_based/learned_index/plr/block_handle_cal.h"
#include "table/block_fetcher.h"
#include "coding.h"
#include <algorithm>

/**TODO: 1. every write encode, every read decode
         2. every datablock creation call add offset, at the same time update the array
         3. everytime adding a new block will update the update handle_ and num_data_blocks_ **/

namespace ROCKSDB_NAMESPACE {

void BlockHandleCalculatorStub::AddBlockToBlockHandle(uint64_t offset, uint64_t size) /* add blocks into the particular block*/
{
    /* need to check anything?*/   
	BlockHandle handler(offset,size); 
    handles_.push_back(handler); /*do i put BlockHandle of all data block in handles_?*/
	total_num_data_blocks++;
}

uint64_t BlockHandleCalculatorStub::GetTotalNumDataBlock()
{
	return total_num_data_blocks;
}


Status BlockHandleCalculatorStub::GetBlockHandle(uint64_t current, BlockHandle& block_handle) const {
	// Get block handle from stub
	assert(current < handles_.size());
	block_handle = handles_[current];
	return Status::OK();
}

Status BlockHandleCalculatorStub::GetAllDataBlockHandles(std::shared_ptr<uint64_t[]> block_sizes,
													uint64_t& begin_block, uint64_t& end_block) {
    // Need: range, data block sizes
	// Assuming data block is 0-indexed
	// Assuming the index block data is stored as [offset1, size1, offset2, size2, ...]
	assert(block_sizes != nullptr);
	assert(end_block < num_data_blocks_);
	uint64_t offset = 0;
	int i = 0;
	for (; i < begin_block; i++) {
		offset += block_sizes[i];
	}
	for (; i < end_block; i++) {
		BlockHandle block_handle;
		block_handle.set_offset(offset);
		block_handle.set_size(block_sizes[i]);
		handles_.push_back(block_handle);
		offset += block_sizes[i];
	}
	return Status::OK();
  }

Status Encode(std::string* encoded_ptr,std::vector<BlockHandle> handle) /*encode size into string*/
{
	for(std::vector<BlockHandle>::iterator it = handle.begin() ; it != handle.end(); ++it)
	{
		// Sanity check that all fields have been set
		assert(it->size() != ~static_cast<uint64_t>(0));
		PutFixed64(encoded_ptr, it->size());
	}
	return Status::OK();
}

Status BlockHandleCalculatorStub::Decode_size(const std::string encoded_data, std::shared_ptr<uint64_t[]> block_sizes)
{
	/*not sure do we have to decode all num_data_blocks*/
	for(uint64_t i=0; i<total_num_data_blocks; i++)
	{
		// Handle the case where the input string is smaller than 8 bytes
        // Return an error or an appropriate response
		assert(encoded_data.size() < 8);

		// Extract 8 bytes from the string
		uint64_t count = i*8;
		std::string temp = encoded_data.substr(0+count, 7+count);
		//cast it to char*
		const char* charPtr = temp.c_str();
		//decode and put into block_sizes
		DecodeFixed64(charPtr, block_sizes[i]);
	}
	return Status::OK();
}

} // namespace ROCKSDB_NAMESPACE
