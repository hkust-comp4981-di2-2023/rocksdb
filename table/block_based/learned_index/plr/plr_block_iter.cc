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
    return (it_ < block_handles_->end());
}

void PLRBlockIter::SeekToFirst() {

}

void PLRBlockIter::SeekToLast() {

}

// Take key, predicted float, return block handles in the range [pred - gamma, pred + gamma]
// REQUIRES:
// 1. Model already constructed
void PLRBlockIter::Seek(const Slice& target) {
    Status s = helper_.PredictBlockId(target);  
    if (!s.ok()) {
        status_ = s;
        return;
    }
    helper_.GetBlockHandles(block_handles_);
    // Should be?
    assert(block_handles_->size() > 0);
    it_ = block_handles_->begin();
}

void PLRBlockIter::Next() {
    ++it_;
    assert(Valid());
}

/* 
begin -> 1 -> 2 -> end
for(it->Seek(..); iter->Valid(); iter->Next())
*/

void PLRBlockIter::Prev() {

}

Slice PLRBlockIter::key() const {

}

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

Status PLRBlockHelper::DecodePLRBlock(const BlockContents& index_block_contents, void* segments, void* data_block_sizes, void* gamma_) {
    // index_block_contents->GetValue().data
}

Status PLRBlockHelper::BuildModel(void* segments, std::unique_ptr<void*> model_) {
    // Create model
    // Transfer ownership of segments to model_
}

Status PLRBlockHelper::PredictBlockId(const Slice& target) {

}

void PLRBlockHelper::GetBlockHandles(std::vector<BlockHandle>* block_handles) {

}

} // namespace ROCKSDB_NAMESPACE


