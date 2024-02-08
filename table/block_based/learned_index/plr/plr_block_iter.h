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
#include "stdint.h"
#include "table/block_based/learned_index/plr/plr_block_fetcher_params.h"

namespace ROCKSDB_NAMESPACE {

// temporary design: https://drive.google.com/file/d/1Z2s31E8Pfxjy6GUOL2a9f5ea2-1hJOFV/view?usp=sharing
class PLRBlockIter : public InternalIteratorBase<IndexValue> {
 public:
	PLRBlockIter(BlockContents* contents, bool key_includes_seq, const uint64_t num_data_blocks): 
		InternalIteratorBase<IndexValue>(),
		seek_mode_(SeekMode::kUnknown),
		data_(contents->data.data()),
		current_(invalid_block_number_),
		begin_block_(invalid_block_number_),
		end_block_(invalid_block_number_),
		key_includes_seq_(key_includes_seq),
		helper_(std::unique_ptr<PLRBlockHelper>(new PLRBlockHelper(num_data_blocks, data_,
																	begin_block_, end_block_)))
		{}

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
		//TODO(fyp): Red underlined for _pinned_iters_mgr
		//_pinned_iters_mgr = pinned_iters_mgr;
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

	inline uint64_t GetMidpointBlockNumber() const {
		return (begin_block_ + end_block_) / 2;
	}

	Status BinarySearchBlockHandle(const Slice& target, uint64_t& begin_block, 
									uint64_t& end_block);

	void SetCurrentIndexValue();
};

// Data members should be stack-allocated.
class PLRBlockHelper {
 public:
	PLRBlockHelper(const uint64_t num_data_blocks, const char* data_, uint64_t& begin_block, 
					uint64_t& end_block): 
		// TODO(fyp): Need to store in Helper? Or simply pass into Stub?
		num_data_blocks_(num_data_blocks),
		// Dummy model with gamma = -1, overwritten by DecodePLRBlock
		model_(-1.0),
		block_sizes_(new uint64_t[num_data_blocks_]),
		handle_calculator_(BlockHandleCalculatorStub(num_data_blocks, block_sizes_,
													begin_block, end_block)) {
			DecodePLRBlock(data_, block_sizes_);
		}

	// Decode two parts: PLR model parameters and Data block size array
	// Construct stub
	// Update max_block_number_, get from property block
	Status DecodePLRBlock(const char* data, std::shared_ptr<uint64_t[]> block_sizes);
	
	// TODO(fyp): const necessary? cannot get range pair unless make GetValue() const
	// or remove const here
	Status PredictBlockRange(const Slice& target, uint64_t& begin_block, uint64_t& end_block);
	
	// Get SINGLE blockhandle 
	// Get blockhandle from stub with current_
	Status GetBlockHandle(uint64_t current, BlockHandle& block_handle) const;
	
	// If there's no data block, return 0
	inline uint64_t GetMaxDataBlockNumber() const { return num_data_blocks_; }

 private:
	// TODO(fyp): Verify member types
	PLRDataRep<uint64_t, double> model_;
	uint64_t num_data_blocks_;
	BlockHandleCalculatorStub handle_calculator_;
	std::shared_ptr<uint64_t[]> block_sizes_;

	// Helper function to decode PLR block
	// Only extract block_sizes, PLRDataRep takes original encoded str
	Status GetBlockSizes(const char* data,
									 	std::shared_ptr<uint64_t[]> block_sizes);

	// Need a function ptr for the 'rounding rule' when a decimal block is 
	enum class RoundingRule: char {
		kCeil = 0x00,
		kFloor = 0x01
	//	kFutureExtend = 0x02
	};
};

class BlockHandleCalculatorStub {
 public:
	BlockHandleCalculatorStub(uint64_t num_data_blocks,
								std::shared_ptr<uint64_t[]> block_sizes,
								uint64_t& begin_block, uint64_t& end_block): 
								num_data_blocks_(num_data_blocks) {
		Status s = GetAllDataBlockHandles(block_sizes, begin_block, end_block);
		assert(s == Status::OK());
	}

	Status GetAllDataBlockHandles(std::shared_ptr<uint64_t[]> block_sizes, 
									uint64_t& begin_block, uint64_t& end_block) {}

	Status GetBlockHandle(uint64_t current, BlockHandle& block_handle) const {}

	Status Decode(const std::string /* str */) {}

	private:
	 std::vector<BlockHandle> handles_;
	 const uint64_t num_data_blocks_;
};

} // namespace ROCKSDB_NAMESPACE