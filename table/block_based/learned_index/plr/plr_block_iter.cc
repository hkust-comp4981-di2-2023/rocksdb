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
	
	value_ = IndexValue();
	
	BlockHandle handle = BlockHandle();
	status_ = helper_->GetBlockHandle(current_, handle);

	if (!status_.ok()) {
		return;
	}

	value_.handle = handle;

	// TODO(fyp): What is this??
	// if (seek_mode_ == SeekMode::kBinarySeek) {
	// 	// Update the begin_block_ or end_block_, based on current block value.
	// 	// do nothing currently, unless have time to change SetBeginBlockAsCurrent()
	// }
}

Status PLRBlockHelper::DecodePLRBlock(const Slice& data) {
	// Extract the substring corr. to PLR Segments and Data block sizes
	const size_t total_length = data.size();

	const size_t block_handles_length = kParamSize * num_data_blocks_;
	assert(total_length > block_handles_length);

	const size_t plr_segments_length = total_length - block_handles_length;
	assert(plr_segments_length % kParamSize == 0);
	
	const char* plr_segments_start = data.data();
	const char* block_handles_start = plr_segments_start + plr_segments_length;

	std::string encoded_plr_segments(plr_segments_start, plr_segments_length);
	std::string encoded_block_handles(block_handles_start, block_handles_length);

	// Initialize model_ and handle_calculator_
	model_->reset(
		new PLRDataRep<EncodedStrBaseType, double>(encoded_plr_segments));
	handle_calculator_->reset(
		new BlockHandleCalculator(encoded_block_handles, num_data_blocks_));
	
	return Status::OK();
}

Status PLRBlockHelper::PredictBlockRange(const Slice& target, 
																					uint64_t& begin_block, 
																					uint64_t& end_block) {
	// Check if target has uint64_t size
	assert(target.size() == kKeySize);
	
	// Cast back to uint64_t
	std::string target_str(target.data(), target.size());
	KeyInternalRep key = stringToNumber<KeyInternalRep>(target_str);

	// Get range, check for invalid range
	auto range =  model_->GetValue(key);
	assert(range.first <= range.second);

	begin_block = std::max<uint64_t>(0, range.first);
	end_block = std::min(num_data_blocks_, range.second);
	return Status::OK();
}

Status PLRBlockHelper::GetBlockHandle(const uint64_t data_block_number, 
																			BlockHandle& block_handle) const {
	assert(data_block_number < num_data_blocks_);

	return handle_calculator_->GetBlockHandle(data_block_number, block_handle);
}
} // namespace ROCKSDB_NAMESPACE