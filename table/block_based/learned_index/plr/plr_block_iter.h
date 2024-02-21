//Todo(fyp): Add licenses later lol

#pragma once

#include <algorithm>
#include <string>

#include "db/dbformat.h"
#include "db/pinned_iterators_manager.h"
#include "include/rocksdb/status.h"
#include "rocksdb/comparator.h"
#include "rocksdb/iterator.h"
#include "rocksdb/status.h"
#include "table/format.h"
#include "table/internal_iterator.h"
#include "table/block_based/learned_index/plr/external/plr/library.h"
#include "table/block_based/learned_index/plr/block_handle_cal.h"
#include "test_util/sync_point.h"
#include "stdint.h"


namespace ROCKSDB_NAMESPACE {

// PLRBlockHelper should handle the lifecylce of all its data members properly.
class PLRBlockHelper {
 public:
	PLRBlockHelper(const uint64_t num_data_blocks, const Slice& data):
		model_(nullptr),
		handle_calculator_(nullptr),
		num_data_blocks_(num_data_blocks) {
		DecodePLRBlock(data); 
	}

	// Only called at constructor.
	//
	// Decode two parts: PLR model parameters and Data block size array.
	// Initialize model_ and handle_calculator_ properly.
	Status DecodePLRBlock(const Slice& data);
	
	Status PredictBlockRange(const Slice& target, uint64_t& begin_block, 
														uint64_t& end_block);
	
	// Get SINGLE blockhandle.
	// TODO(fyp): do we need to if() check input block number?
	//
	// REQUIRES: input block number within [0, num_data_blocks_)
	Status GetBlockHandle(const uint64_t data_block_number, 
												BlockHandle& block_handle) const;
	
	// If there's no data block, return 0
	inline uint64_t GetNumberOfDataBlock() const { return num_data_blocks_; }

 private:
	typedef uint64_t KeyInternalRep;
	typedef uint64_t EncodedStrBaseType;
	static const size_t kKeySize = sizeof(KeyInternalRep);
	static const size_t kParamSize = sizeof(EncodedStrBaseType);

	// TODO(fyp): Verify member types
	std::unique_ptr<PLRDataRep<EncodedStrBaseType, double>> model_;
	std::unique_ptr<BlockHandleCalculator> handle_calculator_;
	const uint64_t num_data_blocks_;

	// Need a function ptr for the 'rounding rule' when a decimal block is 
	enum class RoundingRule: char {
		kCeil = 0x00,
		kFloor = 0x01
	//	kFutureExtend = 0x02
	};
};

class PLRBlockIter : public InternalIteratorBase<IndexValue> {
 public:
	PLRBlockIter(const BlockContents* contents, const bool key_includes_seq, 
				const uint64_t num_data_blocks, const Comparator* user_comparator):
		InternalIteratorBase<IndexValue>(),
		seek_mode_(SeekMode::kUnknown),
		data_(contents->data),
		current_(invalid_block_number_),
		begin_block_(invalid_block_number_),
		end_block_(invalid_block_number_),
		key_includes_seq_(key_includes_seq),
		value_(),
		user_comparator_(user_comparator),
		status_(),
		helper_(std::unique_ptr<PLRBlockHelper>(
			new PLRBlockHelper(num_data_blocks, data_)))
		{}

	bool Valid() const override;

	void SeekToFirst() override;
	void SeekToLast() override;
	void Seek(const Slice& target) override;
	void SeekForPrev(const Slice&) override {
    assert(false);
    current_ = invalid_block_number_;
    status_ = Status::InvalidArgument(
        "RocksDB internal error: should never call SeekForPrev() on index "
        "blocks");
  }

	void Next() override;
	void Prev() override;

	Slice key() const override;
	IndexValue value() const override;
	Status status() const override;

	// Reuse trivial implementation like IndexBlockIter.
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
		pinned_iters_mgr_ = pinned_iters_mgr;
	}

	PinnedIteratorsManager* pinned_iters_mgr_ = nullptr;
#endif

	// PLRBlockIter never owns a memory for key, so return false always
	bool IsKeyPinned() const override { return false; }

	// Since block_content_ is not expected to be returned to IndexReader anymore
	// (according to IndexReader::NewIterator()), block is not pinned and 
	// this function should return false.
	bool IsValuePinned() const override { return false; }

	void UpdateBinarySeekRange(const Slice& seek_key,
															const Slice& data_block_first_key,
															const Slice& data_block_last_key);

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
	Slice data_;

	// TODO(fyp): Write binary search logic s.t. we know go left or go right after 
	// searching the current block
	// Changed to uint64_t to match the type of num_data_blocks_ 
	uint64_t current_, begin_block_, end_block_;
	static const uint64_t invalid_block_number_ = UINT64_MAX;

	// If true, this means keys written in index block contains seq_no, which are
	// internal keys. As a result, when we Seek() an internal key, we don't need
	// to do any extraction.
	// If false, this means keys written in index block are user keys. In this
	// case, when we Seek() an internal key, we always need to extract the user
	// key portion from it.
	//
	// Note: In Seek(target), target is always an internal key.
	bool key_includes_seq_;
	static constexpr const char* key_extraction_not_supported_ = 
																											"PLR_key()_not_supported";
	IndexValue value_;
	const Comparator* user_comparator_;
	Status status_;

	std::unique_ptr<PLRBlockHelper> helper_;

	inline bool IsLastBinarySeek() const {
		return begin_block_ > end_block_;
	}

	inline bool IsLastLinearSeek() const {
		return current_ == end_block_;
	}

	inline uint64_t GetMidpointBlockNumber() const {
		return (begin_block_ + end_block_) / 2;
	}

	inline void SetBeginBlockAsCurrent() {
		begin_block_ = current_ + 1;
	}

	inline void SetEndBlockAsCurrent() {
		end_block_ = current_ - 1;
	}

	void SetCurrentIndexValue();
};
} // namespace ROCKSDB_NAMESPACE