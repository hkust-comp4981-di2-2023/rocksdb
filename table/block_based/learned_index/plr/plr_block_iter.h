//Todo(fyp): Add licenses later lol

#pragma once

#include <string>
#include "db/dbformat.h"
#include "rocksdb/comparator.h"
#include "rocksdb/iterator.h"
#include "rocksdb/status.h"
#include "table/format.h"
#include "table/internal_iterator.h"
#include "include/rocksdb/status.h"

namespace ROCKSDB_NAMESPACE {

class PLRBlockIter : public InternalIteratorBase<IndexValue> {

    public:
        bool Valid() const override;
        void SeekToFirst() override;
        void SeekToLast() override;
        void Seek(const Slice& target) override;
        void Next() override;
        void Prev() override;
        Slice key() const override;
        IndexValue value() const override;
        Status status() const override;
        bool IsOutOfBound() override;
        bool MayBeOutOfLowerBound() override;
        bool MayBeOutOfUpperBound() override;
        void SetPinnedItersMgr(PinnedIteratorsManager* pinned_iters_mgr) override;
        bool IsKeyPinned() const override;
        bool IsValuePinned() const override;
        Status GetProperty(std::string /*prop_name*/, std::string* /*prop*/) override;

    private:
        PLRBlockHelper helper_;
        std::vector<BlockHandle>* block_handles_;
        std::vector<BlockHandle>::iterator it_;
        Status status_;
};

// This class should handle all memory-related logic
class PLRBlockHelper {
    public:
        Status DecodePLRBlock(const BlockContents& index_block_contents, void* segments, void* data_block_sizes, void* gamma_);
        // Remember to deallocate segments memory 
        Status BuildModel(void* segments, std::unique_ptr<void*> model_);
        Status PredictBlockId(const Slice& target);
        // TODO(fyp): Think think need mm need deletion
        void GetBlockHandles(std::vector<BlockHandle>* block_handles);

    private:
        // TODO(fyp)
        std::unique_ptr<void*> model_;

}

} // namespace ROCKSDB_NAMESPACE