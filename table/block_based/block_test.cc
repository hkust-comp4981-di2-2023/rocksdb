//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).
//

#include <stdio.h>
#include <algorithm>
#include <set>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "db/dbformat.h"
#include "db/memtable.h"
#include "db/write_batch_internal.h"
#include "rocksdb/db.h"
#include "rocksdb/env.h"
#include "rocksdb/iterator.h"
#include "rocksdb/slice_transform.h"
#include "rocksdb/table.h"
#include "table/block_based/block.h"
#include "table/block_based/block_builder.h"
#include "table/format.h"
#include "test_util/testharness.h"
#include "test_util/testutil.h"
#include "util/random.h"

#include "table/block_based/index_builder.h"
#include "table/block_based/learned_index/plr/plr_block_iter.h"

namespace ROCKSDB_NAMESPACE {

static std::string RandomString(Random *rnd, int len) {
  std::string r;
  test::RandomString(rnd, len, &r);
  return r;
}
std::string GenerateKey(int primary_key, int secondary_key, int padding_size,
                        Random *rnd) {
  char buf[50];
  char *p = &buf[0];
  snprintf(buf, sizeof(buf), "%6d%4d", primary_key, secondary_key);
  std::string k(p);
  if (padding_size) {
    k += RandomString(rnd, padding_size);
  }

  return k;
}

// Generate random key value pairs.
// The generated key will be sorted. You can tune the parameters to generated
// different kinds of test key/value pairs for different scenario.
void GenerateRandomKVs(std::vector<std::string> *keys,
                       std::vector<std::string> *values, const int from,
                       const int len, const int step = 1,
                       const int padding_size = 0,
                       const int keys_share_prefix = 1) {
  Random rnd(302);

  // generate different prefix
  for (int i = from; i < from + len; i += step) {
    // generating keys that shares the prefix
    for (int j = 0; j < keys_share_prefix; ++j) {
      keys->emplace_back(GenerateKey(i, j, padding_size, &rnd));

      // 100 bytes values
      values->emplace_back(RandomString(&rnd, 100));
    }
  }
}

class BlockTest : public testing::Test {};

// block test
TEST_F(BlockTest, SimpleTest) {
  Random rnd(301);
  Options options = Options();

  std::vector<std::string> keys;
  std::vector<std::string> values;
  BlockBuilder builder(16);
  int num_records = 100000;

  GenerateRandomKVs(&keys, &values, 0, num_records);
  // add a bunch of records to a block
  for (int i = 0; i < num_records; i++) {
    builder.Add(keys[i], values[i]);
  }

  // read serialized contents of the block
  Slice rawblock = builder.Finish();

  // create block reader
  BlockContents contents;
  contents.data = rawblock;
  Block reader(std::move(contents), kDisableGlobalSequenceNumber);

  // read contents of block sequentially
  int count = 0;
  InternalIterator *iter =
      reader.NewDataIterator(options.comparator, options.comparator);
  for (iter->SeekToFirst(); iter->Valid(); count++, iter->Next()) {
    // read kv from block
    Slice k = iter->key();
    Slice v = iter->value();

    // compare with lookaside array
    ASSERT_EQ(k.ToString().compare(keys[count]), 0);
    ASSERT_EQ(v.ToString().compare(values[count]), 0);
  }
  delete iter;

  // read block contents randomly
  iter = reader.NewDataIterator(options.comparator, options.comparator);
  for (int i = 0; i < num_records; i++) {
    // find a random key in the lookaside array
    int index = rnd.Uniform(num_records);
    Slice k(keys[index]);

    // search in block for this key
    iter->Seek(k);
    ASSERT_TRUE(iter->Valid());
    Slice v = iter->value();
    ASSERT_EQ(v.ToString().compare(values[index]), 0);
  }
  delete iter;
}

// return the block contents
BlockContents GetBlockContents(std::unique_ptr<BlockBuilder> *builder,
                               const std::vector<std::string> &keys,
                               const std::vector<std::string> &values,
                               const int /*prefix_group_size*/ = 1) {
  builder->reset(new BlockBuilder(1 /* restart interval */));

  // Add only half of the keys
  for (size_t i = 0; i < keys.size(); ++i) {
    (*builder)->Add(keys[i], values[i]);
  }
  Slice rawblock = (*builder)->Finish();

  BlockContents contents;
  contents.data = rawblock;

  return contents;
}

void CheckBlockContents(BlockContents contents, const int max_key,
                        const std::vector<std::string> &keys,
                        const std::vector<std::string> &values) {
  const size_t prefix_size = 6;
  // create block reader
  BlockContents contents_ref(contents.data);
  Block reader1(std::move(contents), kDisableGlobalSequenceNumber);
  Block reader2(std::move(contents_ref), kDisableGlobalSequenceNumber);

  std::unique_ptr<const SliceTransform> prefix_extractor(
      NewFixedPrefixTransform(prefix_size));

  std::unique_ptr<InternalIterator> regular_iter(
      reader2.NewDataIterator(BytewiseComparator(), BytewiseComparator()));

  // Seek existent keys
  for (size_t i = 0; i < keys.size(); i++) {
    regular_iter->Seek(keys[i]);
    ASSERT_OK(regular_iter->status());
    ASSERT_TRUE(regular_iter->Valid());

    Slice v = regular_iter->value();
    ASSERT_EQ(v.ToString().compare(values[i]), 0);
  }

  // Seek non-existent keys.
  // For hash index, if no key with a given prefix is not found, iterator will
  // simply be set as invalid; whereas the binary search based iterator will
  // return the one that is closest.
  for (int i = 1; i < max_key - 1; i += 2) {
    auto key = GenerateKey(i, 0, 0, nullptr);
    regular_iter->Seek(key);
    ASSERT_TRUE(regular_iter->Valid());
  }
}

// In this test case, no two key share same prefix.
TEST_F(BlockTest, SimpleIndexHash) {
  const int kMaxKey = 100000;
  std::vector<std::string> keys;
  std::vector<std::string> values;
  GenerateRandomKVs(&keys, &values, 0 /* first key id */,
                    kMaxKey /* last key id */, 2 /* step */,
                    8 /* padding size (8 bytes randomly generated suffix) */);

  std::unique_ptr<BlockBuilder> builder;
  auto contents = GetBlockContents(&builder, keys, values);

  CheckBlockContents(std::move(contents), kMaxKey, keys, values);
}

TEST_F(BlockTest, IndexHashWithSharedPrefix) {
  const int kMaxKey = 100000;
  // for each prefix, there will be 5 keys starts with it.
  const int kPrefixGroup = 5;
  std::vector<std::string> keys;
  std::vector<std::string> values;
  // Generate keys with same prefix.
  GenerateRandomKVs(&keys, &values, 0,  // first key id
                    kMaxKey,            // last key id
                    2,                  // step
                    10,                 // padding size,
                    kPrefixGroup);

  std::unique_ptr<BlockBuilder> builder;
  auto contents = GetBlockContents(&builder, keys, values, kPrefixGroup);

  CheckBlockContents(std::move(contents), kMaxKey, keys, values);
}

// A slow and accurate version of BlockReadAmpBitmap that simply store
// all the marked ranges in a set.
class BlockReadAmpBitmapSlowAndAccurate {
 public:
  void Mark(size_t start_offset, size_t end_offset) {
    assert(end_offset >= start_offset);
    marked_ranges_.emplace(end_offset, start_offset);
  }

  void ResetCheckSequence() { iter_valid_ = false; }

  // Return true if any byte in this range was Marked
  // This does linear search from the previous position. When calling
  // multiple times, `offset` needs to be incremental to get correct results.
  // Call ResetCheckSequence() to reset it.
  bool IsPinMarked(size_t offset) {
    if (iter_valid_) {
      // Has existing iterator, try linear search from
      // the iterator.
      for (int i = 0; i < 64; i++) {
        if (offset < iter_->second) {
          return false;
        }
        if (offset <= iter_->first) {
          return true;
        }

        iter_++;
        if (iter_ == marked_ranges_.end()) {
          iter_valid_ = false;
          return false;
        }
      }
    }
    // Initial call or have linear searched too many times.
    // Do binary search.
    iter_ = marked_ranges_.lower_bound(
        std::make_pair(offset, static_cast<size_t>(0)));
    if (iter_ == marked_ranges_.end()) {
      iter_valid_ = false;
      return false;
    }
    iter_valid_ = true;
    return offset <= iter_->first && offset >= iter_->second;
  }

 private:
  std::set<std::pair<size_t, size_t>> marked_ranges_;
  std::set<std::pair<size_t, size_t>>::iterator iter_;
  bool iter_valid_ = false;
};

TEST_F(BlockTest, BlockReadAmpBitmap) {
  uint32_t pin_offset = 0;
  SyncPoint::GetInstance()->SetCallBack(
      "BlockReadAmpBitmap:rnd", [&pin_offset](void *arg) {
        pin_offset = *(static_cast<uint32_t *>(arg));
      });
  SyncPoint::GetInstance()->EnableProcessing();
  std::vector<size_t> block_sizes = {
      1,                // 1 byte
      32,               // 32 bytes
      61,               // 61 bytes
      64,               // 64 bytes
      512,              // 0.5 KB
      1024,             // 1 KB
      1024 * 4,         // 4 KB
      1024 * 10,        // 10 KB
      1024 * 50,        // 50 KB
      1024 * 1024 * 4,  // 5 MB
      777,
      124653,
  };
  const size_t kBytesPerBit = 64;

  Random rnd(301);
  for (size_t block_size : block_sizes) {
    std::shared_ptr<Statistics> stats = ROCKSDB_NAMESPACE::CreateDBStatistics();
    BlockReadAmpBitmap read_amp_bitmap(block_size, kBytesPerBit, stats.get());
    BlockReadAmpBitmapSlowAndAccurate read_amp_slow_and_accurate;

    size_t needed_bits = (block_size / kBytesPerBit);
    if (block_size % kBytesPerBit != 0) {
      needed_bits++;
    }

    ASSERT_EQ(stats->getTickerCount(READ_AMP_TOTAL_READ_BYTES), block_size);

    // Generate some random entries
    std::vector<size_t> random_entry_offsets;
    for (int i = 0; i < 1000; i++) {
      random_entry_offsets.push_back(rnd.Next() % block_size);
    }
    std::sort(random_entry_offsets.begin(), random_entry_offsets.end());
    auto it =
        std::unique(random_entry_offsets.begin(), random_entry_offsets.end());
    random_entry_offsets.resize(
        std::distance(random_entry_offsets.begin(), it));

    std::vector<std::pair<size_t, size_t>> random_entries;
    for (size_t i = 0; i < random_entry_offsets.size(); i++) {
      size_t entry_start = random_entry_offsets[i];
      size_t entry_end;
      if (i + 1 < random_entry_offsets.size()) {
        entry_end = random_entry_offsets[i + 1] - 1;
      } else {
        entry_end = block_size - 1;
      }
      random_entries.emplace_back(entry_start, entry_end);
    }

    for (size_t i = 0; i < random_entries.size(); i++) {
      read_amp_slow_and_accurate.ResetCheckSequence();
      auto &current_entry = random_entries[rnd.Next() % random_entries.size()];

      read_amp_bitmap.Mark(static_cast<uint32_t>(current_entry.first),
                           static_cast<uint32_t>(current_entry.second));
      read_amp_slow_and_accurate.Mark(current_entry.first,
                                      current_entry.second);

      size_t total_bits = 0;
      for (size_t bit_idx = 0; bit_idx < needed_bits; bit_idx++) {
        total_bits += read_amp_slow_and_accurate.IsPinMarked(
            bit_idx * kBytesPerBit + pin_offset);
      }
      size_t expected_estimate_useful = total_bits * kBytesPerBit;
      size_t got_estimate_useful =
          stats->getTickerCount(READ_AMP_ESTIMATE_USEFUL_BYTES);
      ASSERT_EQ(expected_estimate_useful, got_estimate_useful);
    }
  }
  SyncPoint::GetInstance()->DisableProcessing();
  SyncPoint::GetInstance()->ClearAllCallBacks();
}

TEST_F(BlockTest, BlockWithReadAmpBitmap) {
  Random rnd(301);
  Options options = Options();

  std::vector<std::string> keys;
  std::vector<std::string> values;
  BlockBuilder builder(16);
  int num_records = 10000;

  GenerateRandomKVs(&keys, &values, 0, num_records, 1);
  // add a bunch of records to a block
  for (int i = 0; i < num_records; i++) {
    builder.Add(keys[i], values[i]);
  }

  Slice rawblock = builder.Finish();
  const size_t kBytesPerBit = 8;

  // Read the block sequentially using Next()
  {
    std::shared_ptr<Statistics> stats = ROCKSDB_NAMESPACE::CreateDBStatistics();

    // create block reader
    BlockContents contents;
    contents.data = rawblock;
    Block reader(std::move(contents), kDisableGlobalSequenceNumber,
                 kBytesPerBit, stats.get());

    // read contents of block sequentially
    size_t read_bytes = 0;
    DataBlockIter *iter = reader.NewDataIterator(
        options.comparator, options.comparator, nullptr, stats.get());
    for (iter->SeekToFirst(); iter->Valid(); iter->Next()) {
      iter->value();
      read_bytes += iter->TEST_CurrentEntrySize();

      double semi_acc_read_amp =
          static_cast<double>(read_bytes) / rawblock.size();
      double read_amp = static_cast<double>(stats->getTickerCount(
                            READ_AMP_ESTIMATE_USEFUL_BYTES)) /
                        stats->getTickerCount(READ_AMP_TOTAL_READ_BYTES);

      // Error in read amplification will be less than 1% if we are reading
      // sequentially
      double error_pct = fabs(semi_acc_read_amp - read_amp) * 100;
      EXPECT_LT(error_pct, 1);
    }

    delete iter;
  }

  // Read the block sequentially using Seek()
  {
    std::shared_ptr<Statistics> stats = ROCKSDB_NAMESPACE::CreateDBStatistics();

    // create block reader
    BlockContents contents;
    contents.data = rawblock;
    Block reader(std::move(contents), kDisableGlobalSequenceNumber,
                 kBytesPerBit, stats.get());

    size_t read_bytes = 0;
    DataBlockIter *iter = reader.NewDataIterator(
        options.comparator, options.comparator, nullptr, stats.get());
    for (int i = 0; i < num_records; i++) {
      Slice k(keys[i]);

      // search in block for this key
      iter->Seek(k);
      iter->value();
      read_bytes += iter->TEST_CurrentEntrySize();

      double semi_acc_read_amp =
          static_cast<double>(read_bytes) / rawblock.size();
      double read_amp = static_cast<double>(stats->getTickerCount(
                            READ_AMP_ESTIMATE_USEFUL_BYTES)) /
                        stats->getTickerCount(READ_AMP_TOTAL_READ_BYTES);

      // Error in read amplification will be less than 1% if we are reading
      // sequentially
      double error_pct = fabs(semi_acc_read_amp - read_amp) * 100;
      EXPECT_LT(error_pct, 1);
    }
    delete iter;
  }

  // Read the block randomly
  {
    std::shared_ptr<Statistics> stats = ROCKSDB_NAMESPACE::CreateDBStatistics();

    // create block reader
    BlockContents contents;
    contents.data = rawblock;
    Block reader(std::move(contents), kDisableGlobalSequenceNumber,
                 kBytesPerBit, stats.get());

    size_t read_bytes = 0;
    DataBlockIter *iter = reader.NewDataIterator(
        options.comparator, options.comparator, nullptr, stats.get());
    std::unordered_set<int> read_keys;
    for (int i = 0; i < num_records; i++) {
      int index = rnd.Uniform(num_records);
      Slice k(keys[index]);

      iter->Seek(k);
      iter->value();
      if (read_keys.find(index) == read_keys.end()) {
        read_keys.insert(index);
        read_bytes += iter->TEST_CurrentEntrySize();
      }

      double semi_acc_read_amp =
          static_cast<double>(read_bytes) / rawblock.size();
      double read_amp = static_cast<double>(stats->getTickerCount(
                            READ_AMP_ESTIMATE_USEFUL_BYTES)) /
                        stats->getTickerCount(READ_AMP_TOTAL_READ_BYTES);

      double error_pct = fabs(semi_acc_read_amp - read_amp) * 100;
      // Error in read amplification will be less than 2% if we are reading
      // randomly
      EXPECT_LT(error_pct, 2);
    }
    delete iter;
  }
}

TEST_F(BlockTest, ReadAmpBitmapPow2) {
  std::shared_ptr<Statistics> stats = ROCKSDB_NAMESPACE::CreateDBStatistics();
  ASSERT_EQ(BlockReadAmpBitmap(100, 1, stats.get()).GetBytesPerBit(), 1u);
  ASSERT_EQ(BlockReadAmpBitmap(100, 2, stats.get()).GetBytesPerBit(), 2u);
  ASSERT_EQ(BlockReadAmpBitmap(100, 4, stats.get()).GetBytesPerBit(), 4u);
  ASSERT_EQ(BlockReadAmpBitmap(100, 8, stats.get()).GetBytesPerBit(), 8u);
  ASSERT_EQ(BlockReadAmpBitmap(100, 16, stats.get()).GetBytesPerBit(), 16u);
  ASSERT_EQ(BlockReadAmpBitmap(100, 32, stats.get()).GetBytesPerBit(), 32u);

  ASSERT_EQ(BlockReadAmpBitmap(100, 3, stats.get()).GetBytesPerBit(), 2u);
  ASSERT_EQ(BlockReadAmpBitmap(100, 7, stats.get()).GetBytesPerBit(), 4u);
  ASSERT_EQ(BlockReadAmpBitmap(100, 11, stats.get()).GetBytesPerBit(), 8u);
  ASSERT_EQ(BlockReadAmpBitmap(100, 17, stats.get()).GetBytesPerBit(), 16u);
  ASSERT_EQ(BlockReadAmpBitmap(100, 33, stats.get()).GetBytesPerBit(), 32u);
  ASSERT_EQ(BlockReadAmpBitmap(100, 35, stats.get()).GetBytesPerBit(), 32u);
}

class IndexBlockTest
    : public testing::Test,
      public testing::WithParamInterface<std::tuple<bool, bool>> {
 public:
  IndexBlockTest() = default;

  bool useValueDeltaEncoding() const { return std::get<0>(GetParam()); }
  bool includeFirstKey() const { return std::get<1>(GetParam()); }
};

// Similar to GenerateRandomKVs but for index block contents.
void GenerateRandomIndexEntries(std::vector<std::string> *separators,
                                std::vector<BlockHandle> *block_handles,
                                std::vector<std::string> *first_keys,
                                const int len) {
  Random rnd(42);

  // For each of `len` blocks, we need to generate a first and last key.
  // Let's generate n*2 random keys, sort them, group into consecutive pairs.
  std::set<std::string> keys;
  while ((int)keys.size() < len * 2) {
    // Keys need to be at least 8 bytes long to look like internal keys.
    keys.insert(test::RandomKey(&rnd, 12));
  }

  uint64_t offset = 0;
  for (auto it = keys.begin(); it != keys.end();) {
    first_keys->emplace_back(*it++);
    separators->emplace_back(*it++);
    uint64_t size = rnd.Uniform(1024 * 16);
    BlockHandle handle(offset, size);
    offset += size + kBlockTrailerSize;
    block_handles->emplace_back(handle);
  }
}

TEST_P(IndexBlockTest, IndexValueEncodingTest) {
  Random rnd(301);
  Options options = Options();

  std::vector<std::string> separators;
  std::vector<BlockHandle> block_handles;
  std::vector<std::string> first_keys;
  const bool kUseDeltaEncoding = true;
  BlockBuilder builder(16, kUseDeltaEncoding, useValueDeltaEncoding());
  int num_records = 100;

  GenerateRandomIndexEntries(&separators, &block_handles, &first_keys,
                             num_records);
  BlockHandle last_encoded_handle;
  for (int i = 0; i < num_records; i++) {
    IndexValue entry(block_handles[i], first_keys[i]);
    std::string encoded_entry;
    std::string delta_encoded_entry;
    entry.EncodeTo(&encoded_entry, includeFirstKey(), nullptr);
    if (useValueDeltaEncoding() && i > 0) {
      entry.EncodeTo(&delta_encoded_entry, includeFirstKey(),
                     &last_encoded_handle);
    }
    last_encoded_handle = entry.handle;
    const Slice delta_encoded_entry_slice(delta_encoded_entry);
    builder.Add(separators[i], encoded_entry, &delta_encoded_entry_slice);
  }

  // read serialized contents of the block
  Slice rawblock = builder.Finish();

  // create block reader
  BlockContents contents;
  contents.data = rawblock;
  Block reader(std::move(contents), kDisableGlobalSequenceNumber);

  const bool kTotalOrderSeek = true;
  const bool kIncludesSeq = true;
  const bool kValueIsFull = !useValueDeltaEncoding();
  IndexBlockIter *kNullIter = nullptr;
  Statistics *kNullStats = nullptr;
  // read contents of block sequentially
  InternalIteratorBase<IndexValue> *iter = reader.NewIndexIterator(
      options.comparator, options.comparator, kNullIter, kNullStats,
      kTotalOrderSeek, includeFirstKey(), kIncludesSeq, kValueIsFull);
  iter->SeekToFirst();
  for (int index = 0; index < num_records; ++index) {
    ASSERT_TRUE(iter->Valid());

    Slice k = iter->key();
    IndexValue v = iter->value();

    EXPECT_EQ(separators[index], k.ToString());
    EXPECT_EQ(block_handles[index].offset(), v.handle.offset());
    EXPECT_EQ(block_handles[index].size(), v.handle.size());
    EXPECT_EQ(includeFirstKey() ? first_keys[index] : "",
              v.first_internal_key.ToString());

    iter->Next();
  }
  delete iter;

  // read block contents randomly
  iter = reader.NewIndexIterator(options.comparator, options.comparator,
                                 kNullIter, kNullStats, kTotalOrderSeek,
                                 includeFirstKey(), kIncludesSeq, kValueIsFull);
  for (int i = 0; i < num_records * 2; i++) {
    // find a random key in the lookaside array
    int index = rnd.Uniform(num_records);
    Slice k(separators[index]);

    // search in block for this key
    iter->Seek(k);
    ASSERT_TRUE(iter->Valid());
    IndexValue v = iter->value();
    EXPECT_EQ(separators[index], iter->key().ToString());
    EXPECT_EQ(block_handles[index].offset(), v.handle.offset());
    EXPECT_EQ(block_handles[index].size(), v.handle.size());
    EXPECT_EQ(includeFirstKey() ? first_keys[index] : "",
              v.first_internal_key.ToString());
  }
  delete iter;
}

INSTANTIATE_TEST_CASE_P(P, IndexBlockTest,
                        ::testing::Values(std::make_tuple(false, false),
                                          std::make_tuple(false, true),
                                          std::make_tuple(true, false),
                                          std::make_tuple(true, true)));

class PLRIndexBlockTest
    : public testing::Test,
      public testing::WithParamInterface<std::tuple<int, double>> {
 public:
  PLRIndexBlockTest() = default;

  int test_mode() const { return std::get<0>(GetParam()); }
  double gamma() const { return std::get<1>(GetParam()); }
};


std::string MakeKeyLookLikeInternalKey(const std::string& key) {
  // Internal key order: asc user key -> dsc seq no.
  static uint64_t seq_no = UINT64_MAX >> 8;
  char buf[8];
  EncodeFixed64(buf, ((seq_no--) << 8) + 1);
  std::string seq_no_str(buf, 8);

  return key + seq_no_str;
}

std::string uint2str(uint64_t x) {
    std::string s = "";
    for (int i = 7; i >= 0; --i) {
        uint64_t mask = 0xFFULL << (8 * i);
        unsigned char t = (mask & x) >> (8 * i);
        // cout << mask << endl << (char) t << endl << (unsigned int) t << endl;
        s += (char) t;
    }
    return s;
}

// Similar to GenerateRandomIndexEntries but for index block contents.
void GenerateRandomPLRIndexEntries(std::vector<BlockHandle> *block_handles,
                                std::vector<std::string> *first_keys,
                                std::vector<std::string> *in_block_keys,
                                std::vector<std::string> *last_keys,
                                std::vector<std::string> *out_of_block_keys,
                                std::map<int, int> &reverse_index,
                                const int len) {
  Random rnd(42);
  assert(len > 0);

  // For each of `len` blocks, we need to generate a first and last key.
  // Let's generate n*4 random keys, sort them, group into consecutive pairs.
  std::set<std::string> keys;
  while ((int)keys.size() < (len-1) * 4) {
    // PLR index only support key of length 8
    keys.insert(test::RandomKey(&rnd, 8));
  }

  keys.insert(uint2str(18446744073709551612ULL));
  keys.insert(uint2str(18446744073709551613ULL));
  keys.insert(uint2str(18446744073709551614ULL));
  keys.insert(uint2str(18446744073709551615ULL));

  uint64_t offset = 0;
  int idx = 0;
  for (auto it = keys.begin(); it != keys.end();) {

    first_keys->emplace_back(MakeKeyLookLikeInternalKey(*it++));
    in_block_keys->emplace_back(MakeKeyLookLikeInternalKey(*it++));
    last_keys->emplace_back(MakeKeyLookLikeInternalKey(*it++));
    out_of_block_keys->emplace_back(MakeKeyLookLikeInternalKey(*it++));


    uint64_t size = rnd.Uniform(1024 * 16);
    BlockHandle handle(offset, size);
    offset += size + kBlockTrailerSize;
    block_handles->emplace_back(handle);
    reverse_index[handle.offset()] = idx++;
  }
}

// Similar to GenerateRandomIndexEntries but for index block contents.
void GenerateRandomPLRIndexEntriesMode2(std::vector<BlockHandle> *block_handles,
                                std::vector<std::string> *first_keys,
                                std::vector<std::string> *in_block_keys,
                                std::vector<std::string> *last_keys,
                                std::vector<std::string> *out_of_block_keys,
                                std::map<int, int> &reverse_index,
                                const int len) {
  Random rnd(42);
  assert(len > 0);

  // For each of `len` blocks, we need to generate a first and last key.
  // Let's generate n*4 random keys, sort them, group into consecutive pairs.
  std::set<std::string> keys;
  const int num_keys_factor = 4;

  const int no_duplicate_denominator = 3;
  int no_duplicate_len = len / no_duplicate_denominator;
  int duplicate_len = len - no_duplicate_len;
  
  while ((int)keys.size() < no_duplicate_len * num_keys_factor) {
    // PLR index only support key of length 8
    keys.insert(test::RandomKey(&rnd, 8));
  }

  std::vector<int> distribution(no_duplicate_len, 1);
  for (int i = 0; i < duplicate_len; ++i) {
    distribution[rnd.Uniform(no_duplicate_len)]++;
  }

  std::vector<std::string> final_keys;

  int idx = 0;
  for (auto it = keys.begin(); it != keys.end(); it++, idx++) {
    for (int i = 0; i < distribution[idx/num_keys_factor]; ++i) {
      final_keys.emplace_back(*it);
    }
  }
  
  uint64_t offset = 0;
  idx = 0;
  for (auto it = final_keys.begin(); it != final_keys.end();) {

    first_keys->emplace_back(MakeKeyLookLikeInternalKey(*it++));
    in_block_keys->emplace_back(MakeKeyLookLikeInternalKey(*it++));
    last_keys->emplace_back(MakeKeyLookLikeInternalKey(*it++));
    out_of_block_keys->emplace_back(MakeKeyLookLikeInternalKey(*it++));


    uint64_t size = rnd.Uniform(1024 * 16);
    BlockHandle handle(offset, size);
    offset += size + kBlockTrailerSize;
    block_handles->emplace_back(handle);
    reverse_index[handle.offset()] = idx++;
  }
}

TEST_P(PLRIndexBlockTest, PLRIndexValueEncodingTest) {
  Random rnd(302);
  Options options = Options();

  std::vector<BlockHandle> block_handles;
  std::vector<std::string> first_keys;
  std::vector<std::string> in_block_keys;
  std::vector<std::string> last_keys;
  std::vector<std::string> out_of_block_keys;
  std::map<int, int> reverse_index; // use handle.offset() to look up idx number
  
  PLRIndexBuilder builder(gamma());
  int num_records = 100;

  switch(test_mode()) {
    case 0: {
      GenerateRandomPLRIndexEntries(&block_handles, &first_keys, &in_block_keys,
                                    &last_keys, &out_of_block_keys, 
                                    reverse_index, num_records);
      break;
    }

    case 1: {
      GenerateRandomPLRIndexEntriesMode2(&block_handles, &first_keys, 
                                         &in_block_keys, &last_keys, 
                                         &out_of_block_keys, reverse_index, 
                                         num_records);
      break;
    }

    default:
      assert(false);
  }

  InternalKeyComparator icomp(options.comparator);

  // Verify PLR index entries order correctness
  for (int i = 0; i < num_records; ++i) {
    ASSERT_TRUE(icomp.Compare(first_keys[i], in_block_keys[i]) < 0);
    ASSERT_TRUE(icomp.Compare(in_block_keys[i], last_keys[i]) < 0);
    ASSERT_TRUE(icomp.Compare(last_keys[i], out_of_block_keys[i]) < 0);
    if (i + 1 < num_records) {
      ASSERT_TRUE(icomp.Compare(out_of_block_keys[i], first_keys[i+1]) < 0);
    }
  }

  /*
  for (int i = 0; i < num_records; ++i) {
    std::cout << "Index " << i << " - First key: " << first_keys[i] 
              << "\t In-block key: " << in_block_keys[i] << "\t Last key: " 
              << last_keys[i] << "\t Out-of-block key: " << out_of_block_keys[i]
              << std::endl;
  }
  std::cout << "Reverse index:" << std::endl;
  for (auto& entry: reverse_index) {
    std::cout << entry.first << " -> " << entry.second << std::endl;
  }
  */
  
  // print training data
  // printf("Training with keys:\n");
  // for (int i = 0; i < num_records; ++i) {
  //   printf("%d: %lu\n", i, stringToNumber<uint64_t>(first_keys[i]));
  // }
  // printf("Ended training.\n\n");

  for (int i = 0; i < num_records; ++i) {
    Slice first_key = Slice(first_keys[i]);
    Slice middle_key = Slice(in_block_keys[i]);
    Slice last_key = Slice(last_keys[i]);

    builder.OnKeyAdded(first_key);
    builder.OnKeyAdded(middle_key);
    builder.OnKeyAdded(last_key);
    builder.AddIndexEntry(nullptr, nullptr, block_handles[i]);
  }

  // read serialized contents of the block
  IndexBuilder::IndexBlocks rawblock;
  builder.Finish(&rawblock, block_handles[num_records-1]);
  BlockContents block_contents(rawblock.index_block_contents);

  PLRBlockIter* iter = new PLRBlockIter(&block_contents, num_records, &icomp);


  // Test 1: Read block contents sequentially.
  // Note: We won't test key(), because key() is not supported.
  // printf("Test 1\n");
  iter->SeekToFirst();
  for (int index = 0; index < num_records; ++index) {
    ASSERT_TRUE(iter->Valid());

    // Slice k = iter->key();
    IndexValue v = iter->value();

    // EXPECT_EQ(separators[index], k.ToString());
    EXPECT_EQ(block_handles[index].offset(), v.handle.offset());
    EXPECT_EQ(block_handles[index].size(), v.handle.size());

    iter->Next();
  }
  delete iter;
  iter = nullptr;

  // Test 2: Read block contents randomly, using first_key.
  // Expected behavior: After several Next(), ultimately the iterator should
  // point to the correct index entry.
  // printf("Test 2\n");
  iter = new PLRBlockIter(&block_contents, num_records, &icomp);
  for (int i = 0; i < num_records * 2; i++) {
    // find a random key in the lookaside array
    int expected_index = rnd.Uniform(num_records);
    Slice query_key(first_keys[expected_index]);

    // search in block for this key
    iter->Seek(query_key);
    IndexValue v;
    while (iter->Valid()) {
      // printf("%s\n", iter->GetStateMessage().c_str());
      v = iter->value();
      int seek_result_index = reverse_index[v.handle.offset()];
      // printf("Seek Key: %s\t DataBlk First Key: %s\t DataBlk Last Key: %s\n\n",
        // query_key.ToString().c_str(), first_keys[seek_result_index].c_str(),
        // last_keys[seek_result_index].c_str());
      
      // check if the extracted block_handle matches the one in block_handles
      if (seek_result_index == expected_index) {
        break;
      } else {
        Slice seek_result_first_key(first_keys[seek_result_index]);
        Slice seek_result_last_key(last_keys[seek_result_index]);
        iter->UpdateBinarySeekRange(query_key, seek_result_first_key, 
                                    seek_result_last_key);
        if (iter->IsLastBinarySeek()) {
          iter->SwitchToLinearSeekMode();
          // Special handling for cases where multiple internal keys with the
          // same user key but different seq_no exist.
          while (iter->Valid() && icomp.Compare(query_key, seek_result_last_key) > 0) {
            iter->Next();
            if (iter->Valid()) {
              v = iter->value();
              seek_result_index = reverse_index[v.handle.offset()];
              seek_result_last_key = Slice(last_keys[seek_result_index]);
            }
          }
          break;
        }
        iter->Next();
      }
    }
    
    // EXPECT_EQ(separators[expected_index], iter->key().ToString());
    ASSERT_TRUE(iter->Valid());
    EXPECT_EQ(block_handles[expected_index].offset(), v.handle.offset());
    EXPECT_EQ(block_handles[expected_index].size(), v.handle.size());
  }
  delete iter;
  iter = nullptr;

  // Test 3: Read block contents randomly, using query_key.
  // Expected behavior: After several Next(), ultimately the iterator should
  // point to the correct index entry.
  // printf("Test 3\n");
  iter = new PLRBlockIter(&block_contents, num_records, &icomp);
  for (int i = 0; i < num_records * 2; i++) {
    // find a random key in the lookaside array
    int expected_index = rnd.Uniform(num_records);
    Slice query_key(in_block_keys[expected_index]);

    // search in block for this key
    iter->Seek(query_key);
    IndexValue v;
    while (iter->Valid()) {
      v = iter->value();
      int seek_result_index = reverse_index[v.handle.offset()];
      
      // check if the extracted block_handle matches the one in block_handles
      if (seek_result_index == expected_index) {
        break;
      } else {
        Slice seek_result_first_key(first_keys[seek_result_index]);
        Slice seek_result_last_key(last_keys[seek_result_index]);
        iter->UpdateBinarySeekRange(query_key, seek_result_first_key, 
                                    seek_result_last_key);
        if (iter->IsLastBinarySeek()) {
          iter->SwitchToLinearSeekMode();
          // Special handling for cases where multiple internal keys with the
          // same user key but different seq_no exist.
          while (iter->Valid() && icomp.Compare(query_key, seek_result_last_key) > 0) {
            iter->Next();
            if (iter->Valid()) {
              v = iter->value();
              seek_result_index = reverse_index[v.handle.offset()];
              seek_result_last_key = Slice(last_keys[seek_result_index]);
            }
          }
          break;
        }
        iter->Next();
      }
    }
    
    // EXPECT_EQ(separators[expected_index], iter->key().ToString());
    ASSERT_TRUE(iter->Valid());
    EXPECT_EQ(block_handles[expected_index].offset(), v.handle.offset());
    EXPECT_EQ(block_handles[expected_index].size(), v.handle.size());
  }
  delete iter;
  iter = nullptr;

  // Test 4: Read block contents randomly, using out_of_block_key.
  // Expected behavior: After Seek() and several Next(), ultimately the iterator
  // should point to the closest data block with first_key > out_of_block_key,
  // if such block exists (if not exists, becomes !Valid()).
  // printf("Test 4\n");
  /*
  iter = new PLRBlockIter(&block_contents, num_records, &icomp);
  for (int i = 0; i < num_records * 2; i++) {
    // find a random key in the lookaside array
    int expected_index = rnd.Uniform(num_records);
    Slice query_key(out_of_block_keys[expected_index]);

    // search in block for this key
    iter->Seek(query_key);
    IndexValue v;
    while (iter->Valid()) {
      v = iter->value();
      int seek_result_index = reverse_index[v.handle.offset()];
      
      Slice seek_result_first_key(first_keys[seek_result_index]);
      Slice seek_result_last_key(last_keys[seek_result_index]);

      // expectation: our query key should be outside 
      // [seek_result_first_key, seek_result_last_key]
      iter->UpdateBinarySeekRange(query_key, seek_result_first_key, 
                                  seek_result_last_key);
      if (iter->IsLastBinarySeek()) {
        iter->SwitchToLinearSeekMode();
        // Special handling for cases where multiple internal keys with the
        // same user key but different seq_no exist.
        while (iter->Valid() && icomp.Compare(query_key, seek_result_last_key) > 0) {
          iter->Next();
          if (iter->Valid()) {
            v = iter->value();
            seek_result_index = reverse_index[v.handle.offset()];
            seek_result_last_key = Slice(last_keys[seek_result_index]);
          }
        }
        break;
      }
      iter->Next();
    }
    
    // EXPECT_EQ(separators[expected_index], iter->key().ToString());
    if (iter->Valid()) {
      EXPECT_EQ(block_handles[expected_index+1].offset(), v.handle.offset());
      EXPECT_EQ(block_handles[expected_index+1].size(), v.handle.size());
    }
  }
  delete iter;
  iter = nullptr;
  */

}

INSTANTIATE_TEST_CASE_P(PLR, PLRIndexBlockTest,
                        ::testing::Values(std::make_tuple(0, 0.03), 
                                          std::make_tuple(0, 0.06), 
                                          std::make_tuple(0, 0.3), 
                                          std::make_tuple(0, 0.6), 
                                          std::make_tuple(0, 1.0), 
                                          std::make_tuple(0, 1.2), 
                                          std::make_tuple(0, 1.5), 
                                          std::make_tuple(0, 2.0), 
                                          std::make_tuple(0, 3.0), 
                                          std::make_tuple(0, 4.0),
                                          std::make_tuple(1, 0.03), 
                                          std::make_tuple(1, 0.06), 
                                          std::make_tuple(1, 0.3), 
                                          std::make_tuple(1, 0.6), 
                                          std::make_tuple(1, 1.0), 
                                          std::make_tuple(1, 1.2), 
                                          std::make_tuple(1, 1.5), 
                                          std::make_tuple(1, 2.0), 
                                          std::make_tuple(1, 3.0), 
                                          std::make_tuple(1, 4.0)));

}  // namespace ROCKSDB_NAMESPACE

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
