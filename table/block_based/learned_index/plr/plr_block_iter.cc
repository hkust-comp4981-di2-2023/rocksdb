#pragma once

#include <string>
#include "db/dbformat.h"
#include "rocksdb/comparator.h"
#include "rocksdb/iterator.h"
#include "rocksdb/status.h"
#include "table/format.h"
#include "table/internal_iterator.h"
#include "table/block_based/learned_index/plr/plr_block_iter.h"

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
	seek_mode_ = SeekMode::kLinearSeek;

	begin_block_ = 0;
	end_block_ = helper_->GetMaxDataBlockNumber();
	if (end_block_ < 0) {
		current_ = invalid_block_number_;
		return;
	}
	current_ = 0;
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
	seek_mode_ = SeekMode::kLinearSeek;

	begin_block_ = 0;
	end_block_ = helper_->GetMaxDataBlockNumber();
	if (end_block_ < 0) {
		current_ = invalid_block_number_;
		return;
	}
	current_ = end_block_;
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

	status_ = helper_->PredictBlockRange(target, &begin_block_, &end_block_);
	if (!status_.ok()) {
		seek_mode_ = SeekMode::kUnknown;
		return;
	}

	assert(end_block_ >= begin_block_);
	current_ = getMidpointBlockNumber();
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
	assert(seek_mode_ != SeekMode::kUnknown)

	switch(seek_mode_) {
		case SeekMode::kBinarySeek: {
			if (isLastBinarySeek()) {
				current_ = invalid_block_number_;
				return;
			}
			current_ = getMidpointBlockNumber();
		} break;
		case SeekMode::kLinearSeek: {
			if (isLastLinearSeek()) {
				current_ = invalid_block_number_;
				return;
			}
			++current_;
		} break;
		default: {
			status_ = Status::Aborted();
			assert(!"Impossible to fall into this branch");
		} break;
	}
}

// At this moment, Prev() is only supported if seek_mode_ is kLinearSeek.
// REQUIRES: Valid()
// REQUIRES: seek_mode_ is kLinearSeek.
void PLRBlockIter::Prev() {
	assert(Valid());
	assert(seek_mode_ == SeekMode::kLinearSeek)

	// if current is 0 (i.e. pointing to begin_block_), it becomes
	// invalid_block_number after --current_.
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

}

// Use helper_ to return a BlockHandle given current_.
// RQUIRES: Valid()
IndexValue PLRBlockIter::value() const {

}

Status PLRBlockIter::status() const {
	return status_;
}

Status PLRBlockHelper::DecodePLRBlock(const BlockContents& 
																				index_block_contents) {
    // index_block_contents->GetValue().data
}

Status PLRBlockHelper::PredictBlockRange(const Slice& target, int* begin_block, 
																					int* end_block) const {

}

Status PLRBlockHelper::GetBlockHandle(BlockHandle* block_handle) const {

}

} // namespace ROCKSDB_NAMESPACE


