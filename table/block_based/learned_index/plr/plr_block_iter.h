//Todo(fyp): Add licenses later lol

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
#include "test_util/sync_point.h"

namespace ROCKSDB_NAMESPACE {

// temporary design: https://drive.google.com/file/d/1Z2s31E8Pfxjy6GUOL2a9f5ea2-1hJOFV/view?usp=sharing
class PLRBlockIter : public InternalIteratorBase<IndexValue> {
 public:
	PLRBlockIter(BlockContents* block_contents, bool key_includes_seq): 
		InternalIteratorBase<IndexValue>(),
		seek_mode_(SeekMode::kUnknown),
		data_(block_contents->data.data()),
		current_(invalid_block_number_),
		begin_block_(invalid_block_number_),
		end_block_(invalid_block_number_),
		key_includes_seq_(key_includes_seq),
		helper_(std::make_unique(new PLRBlockHelper()))
	{
		helper_->DecodePLRBlock(data_);
	}

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

	// Reuse trivial implementation like IndexBlockIter?
	// bool IsOutOfBound() override;
	// bool MayBeOutOfLowerBound() override;
	// bool MayBeOutOfUpperBound() override;
	// Status GetProperty(std::string /*prop_name*/, std::string* /*prop*/) 
	//    override;

#ifndef NDEBUG
	// only need to verify status in debug build
	~PLRBlockIter() override {
		// Assert that the BlockIter is never deleted while Pinning is Enabled.
		assert(!pinned_iters_mgr_ ||
					(pinned_iters_mgr_ && !pinned_iters_mgr_->PinningEnabled()));
	}

	void SetPinnedItersMgr(PinnedIteratorsManager* pinned_iters_mgr) override {
		_pinned_iters_mgr = pinned_iters_mgr;
	}

	PinnedIteratorsManager* pinned_iters_mgr = nullptr;
#endif

	// PLRBlockIter never owns a memory for key, so return false always
	bool IsKeyPinned() const override { return false; }

	// Since block_content_ is not expected to be returned to IndexReader anymore
	// (according to IndexReader::NewIterator()), block is not pinned and 
	// this function should return false.
	bool IsValuePinned() const override { return false; }

 private:
	enum class SeekMode : char {
		kUnknown = 0x00,
		kLinearSeek = 0x01,
		kBinarySeek = 0x02,
	};
	SeekMode seek_mode_;

	// Actual block content.
	// It is used only once when initializing helper_. The memory buffer will be
	// released (reference count - 1) when DoCleanup() is triggered in base-class 
	// destructor, if block_content_->TransferTo(iter) was called.
	const char* data_;
	uint32_t current_, begin_block_, end_block_;
	static const uint32_t invalid_block_number_ = -1;

	// If true, this means keys written in index block contains seq_no, which are
	// internal keys. As a result, when we Seek() an internal key, we don't need
	// to do any extraction.
	// If false, this means keys written in index block are user keys. In this
	// case, when we Seek() an internal key, we always need to extract the user
	// key portion from it.
	//
	// Note: In Seek(target), target is always an internal key.
	bool key_includes_seq_;
	static const Slice key_extraction_not_supported_ = 
											Slice("PLR_key()_not_supported");
	IndexValue value_;
	Status status_;

	std::unique_ptr<PLRBlockHelper> helper_;

	inline bool IsLastBinarySeek() const {
		return begin_block_ > end_block_;
	}

	inline bool IsLastLinearSeek() const {
		return current_ == end_block_;
	}

	inline uint32_t GetMidpointBlockNumber() const {
		return (begin_block_ + end_block_) / 2;
	}

	void GetCurrentIndexValue();
};

// Data members should be stack-allocated.
class PLRBlockHelper {
 public:
	PLRBlockHelper() = default;

	// Decode two parts: PLR model parameters and Data block size array
	Status DecodePLRBlock(const char* data);
	
	Status PredictBlockRange(const Slice& target, int& begin_block, 
														int& end_block) const;
	
	Status GetBlockHandle(int current, BlockHandle& block_handle) const;
	
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