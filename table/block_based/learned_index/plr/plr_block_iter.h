//Todo(fyp): Add licenses later lol

#pragma once

#include <string>
#include "db/dbformat.h"
#include "include/rocksdb/status.h"
#include "rocksdb/comparator.h"
#include "rocksdb/iterator.h"
#include "rocksdb/status.h"
#include "table/format.h"
#include "table/internal_iterator.h"
#include "table/block_based/learned_index/plr/external/plr/library.h"
#include "test_util/sync_point.h"

namespace ROCKSDB_NAMESPACE {

// temporary design: https://drive.google.com/file/d/1Z2s31E8Pfxjy6GUOL2a9f5ea2-1hJOFV/view?usp=sharing
class PLRBlockIter : public InternalIteratorBase<IndexValue> {

    public:
        bool Valid() const override;
        void SeekToFirst() override;
        void SeekToLast() override;
        void Seek(const Slice& target) override;
        void SeekForPrev(const Slice& target) override;
        void Next() override;
        void Prev() override;
        Slice key() const override;
        IndexValue value() const override;
        Status status() const override;
        bool IsOutOfBound() override;
        bool MayBeOutOfLowerBound() override;
        bool MayBeOutOfUpperBound() override;
        void SetPinnedItersMgr(PinnedIteratorsManager* pinned_iters_mgr) 
            override;
        bool IsKeyPinned() const override;
        bool IsValuePinned() const override;
        Status GetProperty(std::string /*prop_name*/, std::string* /*prop*/) 
            override;

    private:
        enum class SeekMode : char {
            kUnknown = 0x00,
            kLinearSeek = 0x01,
            kBinarySeek = 0x02,
        };
        SeekMode seek_mode_;

        static const uint32_t invalid_block_number_ = -1;
        uint32_t current_, begin_block_, end_block_;
        IndexValue value_;
        Status status_;

        PLRBlockHelper* helper_;

        inline bool isLastBinarySeek() const {
            return begin_block_ > end_block_;
        }
        inline bool isLastLinearSeek() const {
            return current_ == end_block_;
        }

        inline uint32_t getMidpointBlockNumber() const {
            return (begin_block_ + end_block_) / 2;
        }
};

// Data members should be stack-allocated.
// Consider passing an already-initialized Helper to ctor of iterator
// to avoid repeatedly constructing and destructing this Helper.
// This can be achieved by IndexReader owning a Helper and passing it to
// create iterator.
class PLRBlockHelper {
    public:
        // Decode two parts: PLR model parameters and Data block size array
        Status DecodePLRBlock(const BlockContents& index_block_contents);
        Status PredictBlockRange(const Slice& target, int* begin_block, 
                                    int* end_block) const;
        Status GetBlockHandle(BlockHandle* block_handle) const;
        // If there's no data block, return an integer < 0.
        inline uint32_t GetMaxDataBlockNumber() const { return max_block_number_; }

    private:
        // TODO(fyp): Verify member types
        PLRDataRep<std::string, double> model_;
        uint32_t max_block_number_;
        BlockHandleCalculatorStub handle_calculator_;
        // Need a function ptr for the 'rounding rule' when a decimal block is 
        // returned
};

class BlockHandleCalculatorStub {
    public:
        Status CalculateHandle(uint32_t /* block_number */, 
                                BlockHandle* /* handle */) {
            return Status.NotSupported();
        }
        Status Decode(const std::string /* str */) {
            return Status.NotSupported();
        }
};

} // namespace ROCKSDB_NAMESPACE