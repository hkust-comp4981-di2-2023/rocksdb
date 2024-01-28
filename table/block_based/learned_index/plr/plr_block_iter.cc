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

void PLRBlockIter::SeekToFirst() {

}

void PLRBlockIter::SeekToLast() {

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

    Status s = helper_->PredictBlockRange(target, &begin_block_, &end_block_);
    if (!s.ok()) {
        status_ = s;
        seek_mode_ = SeekMode::kUnknown;
        return;
    }

    assert(end_block_ >= begin_block_);
    current_ = (begin_block_ + end_block_) / 2;
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
// REQUIRES: Valid()
// REQUIRES: seek_mode_ is either kBinarySeek or kLinearSeek.
void PLRBlockIter::Next() {
    ++it_;
    assert(Valid());
}

// At this moment, Prev() is only supported if seek_mode_ is kLinearSeek.
// REQUIRES: Valid()
// REQUIRES: seek_mode_ is kLinearSeek.
void PLRBlockIter::Prev() {

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

}

bool PLRBlockIter::IsOutOfBound() {

}

bool PLRBlockIter::MayBeOutOfLowerBound() {

}

bool PLRBlockIter::MayBeOutOfUpperBound() {

}

void PLRBlockIter::SetPinnedItersMgr(PinnedIteratorsManager* pinned_iters_mgr) {

}

bool PLRBlockIter::IsKeyPinned() const {

}

bool PLRBlockIter::IsValuePinned() const {

}

Status PLRBlockIter::GetProperty(std::string /*prop_name*/, std::string* /*prop*/) {
    return Status::NotSupported("PLRBlockIter::GetProperty");
}

Status PLRBlockHelper::DecodePLRBlock(const BlockContents& index_block_contents) {
    // index_block_contents->GetValue().data
}

Status PLRBlockHelper::PredictBlockRange(const Slice& target, int* begin_block, int* end_block) {

}

Status PLRBlockHelper::GetBlockHandle(BlockHandle* block_handle) {

}

} // namespace ROCKSDB_NAMESPACE


