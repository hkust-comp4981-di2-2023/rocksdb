// Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).

#include <cstdio>
#include <string>
#include <vector>

#include "rocksdb/db.h"
#include "rocksdb/slice.h"
#include "rocksdb/options.h"
#include "table/block_based/index_builder.h"
#include "table/format.h"

using namespace ROCKSDB_NAMESPACE;

std::string kDBPath = "/tmp/plr_func_test";

Slice MakeSlice(std::string& element) {
  return Slice(element);
}

BlockHandle MakeBlockHandle(int& offset_and_size) {
  return BlockHandle(offset_and_size, offset_and_size);
}

int main() {
  std::vector<std::string> tmp = {"yamada", "anna", "totemo", "kawaii", 
  "ichikawa", "kyotaro", "mo", "kokkoi",
  "kanojo", "dekite", "hoshii"};
  std::vector<Slice> keys;
  std::transform(tmp.begin(), tmp.end(), std::back_inserter(keys), MakeSlice);

  std::vector<uint64_t> offset_and_size = {1, 12, 56, 78,
  91, 126, 954, 1045,
  5506, 20687, 50489};
  std::vector<BlockHandle> bh;
  std::transform(offset_and_size.begin(), offset_and_size.end(), 
                  std::back_inserter(bh), MakeBlockHandle);


  PLRIndexBuilder* builder = new PLRIndexBuilder(2);


  builder->OnKeyAdded(keys[0]);
  for (std::vector<std::string>::const_iterator it = keys.begin() + 1,
        std::vector<BlockHandle>::const_iterator bhit = bh.begin();
        it != keys.end(); it++, bh++) {
    builder->AddIndexEntry(nullptr, *it, *bh);
  }
  builder->AddIndexEntry(nullptr, nullptr, bh.back());

  std::string result;
  IndexBuilder::IndexBlocks ib;
  ib.index_block_contents = MakeSlice(result);

  builder->Finish(ib, bh.back());

  printf("Encoded string: %s\n", ib.index_block_contents.data());

  delete builder;

  return 0;
}
