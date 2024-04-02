#include "table/block_based/learned_index/plr/plr_block_iter.h"

namespace ROCKSDB_NAMESPACE {

bool PLRBlockIter::Valid() const {
	if (current_ == invalid_block_number_) {
		op_logs += "Valid();";
	}
	else {
		op_logs += "InValid();";
	}
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
	op_logs = "SeekToFirst():[";

	status_ = Status::OK();
	seek_mode_ = SeekMode::kUnknown;
	is_key_set_ = false;

	begin_block_ = 0;
	end_block_ = helper_->GetNumberOfDataBlock();
	if (end_block_ == 0) {
		current_ = invalid_block_number_;
		return;
	}
	end_block_ -= 1;
	current_ = 0;

	SetCurrentIndexValue();
	if (!status_.ok()) {
		current_ = invalid_block_number_;
	}

	seek_mode_ = SeekMode::kLinearSeek;
	op_logs += GetStateMessage() + "];";
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
	op_logs = "SeekToLast():[";

	status_ = Status::OK();
	seek_mode_ = SeekMode::kUnknown;
	is_key_set_ = false;

	begin_block_ = 0;
	end_block_ = helper_->GetNumberOfDataBlock();
	if (end_block_ == 0) {
		current_ = invalid_block_number_;
		return;
	}
	end_block_ -= 1;
	current_ = end_block_;

	SetCurrentIndexValue();
	if (!status_.ok()) {
		current_ = invalid_block_number_;
		return;
	}

	seek_mode_ = SeekMode::kLinearSeek;
	op_logs += GetStateMessage() + "];";
}

// Take a key as input and uses helper_->PredictBlockRange() to update data
// member start_block_ and end_block_. Update current_ to be the midpoint. 
// Then change seek_mode_ to kBinarySeek.
//
// PredictBlockRange() uses model_ to predict a float-type block number with a
// gamma error bound, helper_ uses a function pointer to convert the block 
// number to an integer.
//
// Note: The input param. target is an internal key.
//
// REQUIRES: helper_ (and thus helper_->model_) is initialized.
// REQUIRES: Input param. target is an internal key.
// REQUIRES: Input param. target has size <= 8 after ExtractUserKey().
void PLRBlockIter::Seek(const Slice& target) {
	TEST_SYNC_POINT("PLRBlockIter::Seek:0");
	assert(helper_ != nullptr);
	op_logs = "Seek(user_key=";

	seek_mode_ = SeekMode::kUnknown;
	is_key_set_ = false;
	
	Slice seek_key = ExtractUserKey(target);
	for (size_t idx = 0; idx < seek_key.size(); ++idx) {
		op_logs += std::to_string((unsigned int) ((unsigned char) seek_key[idx])) + ";"
	}
	op_logs += "):[";

	assert(seek_key.size() <= 8);

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
	op_logs += GetStateMessage() + "];";
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
	op_logs += "Next():[";

	is_key_set_ = false;

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
	op_logs += GetStateMessage() + "];";
}

// At this moment, Prev() is only supported if seek_mode_ is kLinearSeek.
// REQUIRES: Valid()
// REQUIRES: seek_mode_ is kLinearSeek.
void PLRBlockIter::Prev() {
	assert(Valid());
	assert(seek_mode_ == SeekMode::kLinearSeek);
	op_logs += "Prev():[";

	is_key_set_ = false;

	if (current_ == 0) {
		current_ = invalid_block_number_;
		return;
	}
	--current_;
	
	SetCurrentIndexValue();
	op_logs += GetStateMessage() + "];";
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
	if (is_key_set_) {
		op_logs += "key()[";
		Slice k = key_.GetKey();
		for (size_t idx = 0; idx < k.size(); ++idx) {
			op_logs += std::to_string((unsigned int) ((unsigned char) k[idx])) + ";"
		}
		op_logs += "];";
		return key_.GetKey();
	}
	return Slice(key_extraction_not_supported_);
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
	op_logs += "SetCurrentIndexValue()[";
	if (!Valid()) {
		op_logs += "]";
		return;
	}
	
	value_ = IndexValue();
	
	BlockHandle handle = BlockHandle();
	status_ = helper_->GetBlockHandle(current_, handle);

	if (!status_.ok()) {
		op_logs += "status_ not ok()]";
		return;
	}

	value_.handle = handle;
	op_logs += GetStateMessage() + "];";
}

// This function updates the seek range correspondingly, such that the 
// next PLRBlockIter::Next() will update current_ to a correct value.
//
// The primary goal of this function is to update current_ until it points to
// the block that contains the first occurrence of the user key in seek_key.
// However, there may have optimizations that try to make current_ pointing to
// a block that contains an internal key with same user key as seek_key and
// a seqno closer to seek_key (on L.H.S.).
//
// Note: This function assumes input data_block first/last keys are the keys of
// the current data block (as pointed by current_).
// Note: Only accepts internal keys.
//
// REQUIRES: Valid()
// REQUIRES: binary seek mode
void PLRBlockIter::UpdateBinarySeekRange(const Slice& seek_key,
																					const Slice& data_block_first_key,
																					const Slice& data_block_last_key) {
	assert(Valid());
	assert(seek_mode_ == SeekMode::kBinarySeek);

	assert(internal_key_comparator_->Compare(data_block_first_key, 
																					 data_block_last_key) <= 0);

	op_logs += "UpdateBinarySeekRange()=>";
	
	// Case 1: Seek key > All keys in current data block.
	if (internal_key_comparator_->Compare(data_block_last_key, seek_key) < 0) {
		SetBeginBlockAsCurrent();
		return;
	}

	// Case 2: Seek key < All keys in current data block.
	if (internal_key_comparator_->Compare(seek_key, data_block_first_key) < 0) {
		SetEndBlockAsCurrent();
		return;
	}

	// Case 3: First key in data block <= Seek key <= Last key in data block.
	// Then Seek key must lie within the current data block if it exists.
	begin_block_ = end_block_+1;
	assert(IsLastBinarySeek());
	op_logs += "IsLastBinarySeek();";
}

void PLRBlockIter::SeekBeginBlock() {
	assert(seek_mode_ == SeekMode::kBinarySeek);
	assert(!IsLastBinarySeek());
	current_ = begin_block_;
	SetCurrentIndexValue();
}

void PLRBlockIter::SeekEndBlock() {
	assert(seek_mode_ == SeekMode::kBinarySeek);
	assert(!IsLastBinarySeek());
	current_ = end_block_;
	SetCurrentIndexValue();
}

Status PLRBlockHelper::DecodePLRBlock(const Slice& data) {
	// Extract the substring corr. to PLR Segments and Data block sizes
	const size_t total_length = data.size();

	const size_t block_handles_length = sizeof(uint64_t) * (num_data_blocks_ + 1);
	assert(total_length > block_handles_length);

	const size_t plr_segments_length = total_length - block_handles_length;
	assert(plr_segments_length % kParamSize == 0);
	
	const char* plr_segments_start = data.data();
	const char* block_handles_start = plr_segments_start + plr_segments_length;

	std::string encoded_plr_segments(plr_segments_start, plr_segments_length);
	std::string encoded_block_handles(block_handles_start, block_handles_length);

	// Initialize model_ and handle_calculator_
	model_.reset(
		new PLRDataRep<EncodedStrBaseType, long double>(encoded_plr_segments));
	handle_calculator_.reset(
		new BlockHandleCalculator(encoded_block_handles, num_data_blocks_));
	
	return Status::OK();
}

Status PLRBlockHelper::PredictBlockRange(const Slice& target, 
																					uint64_t& begin_block, 
																					uint64_t& end_block) {
	// Check if target is smaller than uint64_t
	assert(target.size() <= kKeySize);
	
	// Cast back to uint64_t
	std::string target_str(target.data(), target.size());
	KeyInternalRep key = stringToNumber<KeyInternalRep>(target_str);

	// Get range, check for invalid range
	auto range =  model_->GetValue(static_cast<EncodedStrBaseType>(key));
	assert(range.first <= range.second);

	assert(num_data_blocks_ > 0);
	begin_block = std::max<uint64_t>(0, 
																	std::min(num_data_blocks_ - 1, static_cast<uint64_t>(range.first)));
	end_block = std::max<uint64_t>(0, 
																	std::min(num_data_blocks_ - 1, static_cast<uint64_t>(range.second)));
	// TODO(fyp): debug only:
	// begin_block = 0;
	// end_block = num_data_blocks_ - 1;
	return Status::OK();
}

Status PLRBlockHelper::GetBlockHandle(const uint64_t data_block_number, 
																			BlockHandle& block_handle) const {
	assert(data_block_number < num_data_blocks_);

	return handle_calculator_->GetBlockHandle(data_block_number, block_handle);
}
} // namespace ROCKSDB_NAMESPACE