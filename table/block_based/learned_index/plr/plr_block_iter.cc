#pragma once

#include <string>
#include "db/dbformat.h"
#include "rocksdb/comparator.h"
#include "rocksdb/iterator.h"
#include "rocksdb/status.h"
#include "table/format.h"
#include "table/internal_iterator.h"
#include "table/block_based/learned_index/plr/plr_block_iter.h"
#include "table/block_fetcher.h"
#include <algorithm>
#include <sstream>
#include "table/block_based/learned_index/plr/plr_block_fetcher_params.h"

/*
status_ = file_->Read(handle_.offset(), block_size_ + kBlockTrailerSize,
                            &slice_, used_buf_, for_compaction_);
!!! Remember to add 5 bytes for kBlockTrailerSize for each block !!!
*/

namespace ROCKSDB_NAMESPACE {

bool PLRBlockIter::Valid() const {
	return current_ != invalid_block_number_;
}

// Change seek_mode_ to kLinearSeek. Update begin_block_ and end_block_ to full
// data block number range. 
// If max data block number >= 0, Update current_ to 0.
// Othewise, there is no data block in this sstable, then current_ becomes 
// invalid.
// REQUIRES: helper_ is initialized.
void PLRBlockIter::SeekToFirst() {
	TEST_SYNC_POINT("PLRBlockIter::SeekToFirst:0");
	assert(helper_ != nullptr);

	status_ = Status::OK();
	seek_mode_ = SeekMode::kUnknown;

	begin_block_ = 0;
	end_block_ = helper_->GetMaxDataBlockNumber();
	if (end_block_ < 0) {
		current_ = invalid_block_number_;
		return;
	}
	current_ = 0;

	SetCurrentIndexValue();
	if (!status_.ok()) {
		current_ = invalid_block_number_;
	}

	seek_mode_ = SeekMode::kLinearSeek;
}

// Change seek_mode_ to kLinearSeek. Update begin_block_ and end_block_ to full
// data block number range. 
// If max data block number >= 0, Update current_ to end_block_.
// Othewise, there is no data block in this sstable, then current_ becomes 
// invalid.
// REQUIRES: helper_ is initialized.
void PLRBlockIter::SeekToLast() {
	TEST_SYNC_POINT("PLRBlockIter::SeekToLast:0");
	assert(helper_ != nullptr);

	status_ = Status::OK();
	seek_mode_ = SeekMode::kUnknown;

	begin_block_ = 0;
	end_block_ = helper_->GetMaxDataBlockNumber();
	if (end_block_ < 0) {
		current_ = invalid_block_number_;
		return;
	}
	current_ = end_block_;

	SetCurrentIndexValue();
	if (!status_.ok()) {
		current_ = invalid_block_number_;
		return;
	}

	seek_mode_ = SeekMode::kLinearSeek;
}

// Take a key as input and uses helper_->PredictBlockRange() to update data
// member start_block_ and end_block_. Update current_ to be the midpoint. 
// Then change seek_mode_ to kBinarySeek.
//
// PredictBlockRange() uses model_ to predict a float-type block number with a
// gamma error bound, helper_ uses a function pointer to convert the block 
// number to an integer.
//
// REQUIRES: helper_ (and thus helper_->model_) is initialized.
void PLRBlockIter::Seek(const Slice& target) {
	TEST_SYNC_POINT("PLRBlockIter::Seek:0");
	assert(helper_ != nullptr);

	seek_mode_ = SeekMode::kUnknown;
	
	Slice seek_key = target;
	if (!key_includes_seq_) {
		seek_key = ExtractUserKey(target);
	}

	status_ = helper_->PredictBlockRange(seek_key, begin_block_, end_block_);
	if (!status_.ok()) {
		return;
	}
	assert(end_block_ >= begin_block_);

	current_ = GetMidpointBlockNumber();

	SetCurrentIndexValue();
	if (!status_.ok()) {
		current_ = invalid_block_number_;
		return;
	}

	seek_mode_ = SeekMode::kBinarySeek;
}

// Work differently based on seek_mode_.
// If seek_mode_ is kBinarySeek, this means the current chain of Next() 
// operations are triggered after a Seek() operation. We will do a binary seek
// in this case, where we will update current_ to midpoint of seek range.
//
// Note: This requires additional logic from the user of PLRBlockIter to update
// the seek range, i.e. adjusting begin_block_ or end_block_ based on the seek
// result, after they use value() or key() to look for seek result.
//
// If seek_mode_ is kLinearSeek, this means the current chain of Next()
// operations are triggered after a SeekToFirst() or SeekToLast() operation.
// We will do a linear seek in this case, where we will only update current_.
//
// Note: If current_ is logically invalid after the current Next(), update
// current_ to invalid_block_number_. i.e. Before the current Next(), current_
// points to the last block number possible, depending on seek_mode_.
// REQUIRES: Valid()
// REQUIRES: seek_mode_ is either kBinarySeek or kLinearSeek.
void PLRBlockIter::Next() {
	assert(Valid());
	assert(seek_mode_ != SeekMode::kUnknown);

	switch(seek_mode_) {
		case SeekMode::kBinarySeek: {
			if (IsLastBinarySeek()) {
				current_ = invalid_block_number_;
				break;
			}
			current_ = GetMidpointBlockNumber();
		} break;
		case SeekMode::kLinearSeek: {
			if (IsLastLinearSeek()) {
				current_ = invalid_block_number_;
				break;
			}
			++current_;
		} break;
		default: {
			status_ = Status::Aborted();
			assert(!"Impossible to fall into this branch");
		} break;
	}

	SetCurrentIndexValue();
	if (!status_.ok()) {
		current_ = invalid_block_number_;
		return;
	}
}

// At this moment, Prev() is only supported if seek_mode_ is kLinearSeek.
// REQUIRES: Valid()
// REQUIRES: seek_mode_ is kLinearSeek.
void PLRBlockIter::Prev() {
	assert(Valid());
	assert(seek_mode_ == SeekMode::kLinearSeek);

	if (current_ == 0) {
		current_ = invalid_block_number_;
		return;
	}
	--current_;
}

// Return an internal key that can be parsed by ExtractUserKey().
//
// Note: Our case is special because we can't backward compute the key for a 
// given block number indicated by current_. So we may need to return a dummy 
// key to indicate this operation is not supported and the key is not useful.
//
// REQUIRES: Valid()
Slice PLRBlockIter::key() const {
	assert(Valid());
	return key_extraction_not_supported_;
}

// Use helper_ to return a BlockHandle given current_.
// RQUIRES: Valid()
IndexValue PLRBlockIter::value() const {
	assert(Valid());
	return value_;
}

Status PLRBlockIter::status() const {
	return status_;
}

void PLRBlockIter::SetCurrentIndexValue() {
	if (!Valid()) {
		return;
	}
	
	IndexValue value_ = IndexValue();
	
	BlockHandle handle = BlockHandle();
	status_ = helper_->GetBlockHandle(current_, handle);

	if (!status_.ok()) {
		return;
	}

	value_.handle = handle;

	if (seek_mode_ == SeekMode::kBinarySeek) {
		// Update the begin_block_ or end_block_, based on current block value.
		// do nothing currently, unless have time to change SetBeginBlockAsCurrent()
	}
}

Status PLRBlockHelper::GetBlockSizes(const char* data) {
	assert(data != nullptr);

	static const uint64_t UINT64_T_SIZE = 8;
	char* data_ptr;
	
	// Starting position of data block sizes
    uint64_t startPos = std::max<uint64_t>(0, static_cast<uint64_t>(strlen(data)) - (UINT64_T_SIZE * num_data_blocks_));

    for (uint64_t i = 0; i < num_data_blocks_; i++) {
		// Starting position of current data block size
		const char* startPos_value = data + startPos + (i * UINT64_T_SIZE);
		std::stringstream ss(std::string(startPos_value, UINT64_T_SIZE));
        uint64_t value;
		// Extract as uint64_t 
        ss >> value;
        block_sizes_[i] = value;
    }

	return Status::OK();
}

Status PLRBlockHelper::DecodePLRBlock(const char* data) {
	// Extract data block sizes
	Status s = GetBlockSizes(data);
	if (!s.ok()) {
		return Status::NotSupported();
	}
	// model constructed in PLRDataRep
 	model_ = PLRDataRep<uint64_t, double>(data);
	
	return Status::OK();
}

Status PLRBlockHelper::PredictBlockRange(const Slice& target, uint64_t& begin_block, uint64_t& end_block) {
	// Check if target has uint64_t size
	assert(target.size() == sizeof(uint64_t));
	// Cast back to uint64_t
	uint64_t key = reinterpret_cast<uint64_t>(target.data());

	// Get range, check for invalid range
	std::pair<uint64_t, uint64_t> range =  model_.GetValue(key);
	assert(range.first <= range.second);

	begin_block = std::max<uint64_t>(0, range.first);
	end_block = std::min(num_data_blocks_, range.second);
	return Status::OK();
}

Status PLRBlockHelper::GetBlockHandle(uint64_t current, BlockHandle& block_handle) const {
	// Get block handle from stub
	handle_calculator_.GetBlockHandle(current, block_handle);
	return Status::OK();
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
} // namespace ROCKSDB_NAMESPACE


