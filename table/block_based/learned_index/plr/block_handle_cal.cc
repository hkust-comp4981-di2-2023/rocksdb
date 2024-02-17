#include "table/block_based/learned_index/plr/block_handle_cal.h"

namespace ROCKSDB_NAMESPACE {

// Retrieve the data block handle given a data block number.
//
// REQUIRES: input block number should be within [0, num_data_blocks_).
Status BlockHandleCalculator::GetBlockHandle(const uint64_t data_block_number, 
																							BlockHandle& block_handle) const {
	assert(data_block_number < num_data_blocks_);
	assert(data_block_handles_.size() == num_data_blocks_);

	block_handle.set_offset(data_block_handles_[data_block_number].offset());
	block_handle.set_size(data_block_handles_[data_block_number].size());

	return Status::OK();
}

// Decode encoded_string to get the list of data block sizes. Then calculate
// the corresponding block handle per data block. Update the offset of the next
// data block after each iteration.
// The first uint64_t is the offset of the first block. The remaining uint64_t
// are block sizes.
//
// TODO(fyp): check if DecodeFixed64() work expectedly: expect yes because we 
// are passing address by pointers, so even null characters in between has 
// no effect
Status BlockHandleCalculator::Decode(const std::string& encoded_string) {
	assert(encoded_string.size() == 
					sizeof(uint64_t) * num_data_blocks_ + sizeof(uint64_t));

	std::string first_offset_substr = encoded_string.substr(0u, sizeof(uint64_t));
	first_data_block_offset_ = DecodeFixed64(first_offset_substr.c_str());

	uint64_t current_data_block_size;
	uint64_t current_offset = first_data_block_offset_;

	for (uint64_t i = 0; i < num_data_blocks_; ++i) {
		// (i + 1) takes first data block offset into account.
		uint64_t start = (i + 1) * sizeof(uint64_t);
		std::string handle_size = encoded_string.substr(start, sizeof(uint64_t));
		current_data_block_size = DecodeFixed64(handle_size.c_str());
		
		BlockHandle handle;
		handle.set_offset(current_offset);
		handle.set_offset(current_data_block_size);
		data_block_handles_[i] = handle;

		current_offset += current_data_block_size + kBlockTrailerSize;
	}

	return Status::OK();
}
} // namespace ROCKSDB_NAMESPACE