#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>
#include <iostream>

#include "rocksdb/db.h"
#include "rocksdb/slice.h"
#include "rocksdb/options.h"
#include "table/block_based/index_builder.h"
#include "table/block_based/learned_index/plr/plr_block_iter.h"
#include "table/format.h"

using namespace ROCKSDB_NAMESPACE;

std::string kDBPath = "/tmp/plr_index_integrate_example";

Slice MakeSlice(std::string& element) {
  return Slice(element);
}

BlockHandle MakeBlockHandle(const int& offset_and_size) {
  return BlockHandle(offset_and_size, offset_and_size);
}

int main() {
  // Index block content building using PLRIndexBuilder
  printf("===== Index block content building using PLRIndexBuilder =====\n");
  
  std::vector<std::string> tmp = {"yamada", "anna", "totemo", "kawaii", 
    "ichikawa", "kyotaro", "mo", "kakkoi",
    "kanojo", "dekite", "hoshii"};
  std::sort(tmp.begin(), tmp.end());
  std::vector<Slice> keys;
  std::transform(tmp.begin(), tmp.end(), std::back_inserter(keys), MakeSlice);

  std::vector<uint64_t> offset_and_size = {1, 12, 56, 78,
    91, 126, 954, 1045,
    5506, 20687, 50489};
  std::vector<BlockHandle> bh;
  std::transform(offset_and_size.begin(), offset_and_size.end(), 
                  std::back_inserter(bh), MakeBlockHandle);
  bh[0].set_offset(1111);

  size_t num_data_block = keys.size();
  assert(bh.size() == keys.size());

  double gamma = 0.3;
  PLRIndexBuilder* builder = new PLRIndexBuilder(gamma);


  builder->OnKeyAdded(keys[0]);
  std::vector<Slice>::const_iterator it = keys.begin();
  it++;
  std::vector<BlockHandle>::const_iterator bhit = bh.begin();
  for (; it != keys.end(); it++, bhit++) {
    builder->AddIndexEntry(nullptr, &(*it), *bhit);
  }
  builder->AddIndexEntry(nullptr, nullptr, *bhit);

  std::string result;
  IndexBuilder::IndexBlocks ib;
  ib.index_block_contents = MakeSlice(result);

  builder->Finish(&ib, *bhit);

  printf("Encoded string size: %lu\n", ib.index_block_contents.size());
  for (size_t i = 0; i < ib.index_block_contents.size(); ++i) {
    printf("%u;", ib.index_block_contents.data()[i]);
  }
  printf("\n");

  delete builder;

  // Index block content reading using PLRBlockHelper
  printf("===== Index block content reading using PLRBlockHelper =====\n");

  PLRBlockHelper reader(num_data_block, ib.index_block_contents);

  std::vector<std::string> sorted_data_block_keys = {"anna", "dekite", "hoshii",
    "ichikawa", "kakkoi", "kanojo", "kawaii", 
    "kyotaro", "mo", "totemo", "yamada"};
  std::map<std::string, BlockHandle> data_blocks;
  for (size_t i = 0; i < num_data_block; ++i) {
    data_blocks[sorted_data_block_keys[i]] = bh[i];
  }
  printf("Trained with the following data blocks (Gamma = %.2f):\n", gamma);
  for (const auto& pair : data_blocks) {
    std::cout << pair.first << " -> " << pair.second.ToString() << std::endl;
  }

  uint64_t begin_num, end_num;
  BlockHandle begin_bh, end_bh;
  std::vector<std::string> targets = {
    "tenki", "ga", "ii", "kara", "sanpo", "shimasho", 
    "i", "go", "to", "school", "by", "bus",
    "never", "gonna", "give", "you", "up",
    "never", "gonna", "let", "you", "down"};

  printf("\n");
  printf("Testing with the following target keys (Gamma = %.2f):\n", gamma);
  for (std::vector<std::string>::const_iterator it = targets.begin(); 
        it != targets.end(); it++) {
    reader.PredictBlockRange(*it, begin_num, end_num);
    reader.GetBlockHandle(begin_num, begin_bh);
    reader.GetBlockHandle(end_num, end_bh);
    std::cout << "Target [" << *it << "] -> Output [Begin (block#: ";
    std::cout << begin_num << "; handle: " << begin_bh.ToString();
    std::cout << ")End (block#: " << end_num << "; handle: ";
    std::cout << end_bh.ToString() << ")]" << std::endl;
    // printf("Target [%s] -> Output [Begin (block#: %lu; handle: %s)"
    //         "End (block#: %lu; handle: %s)]\n",
    //         *it, begin_num, begin_bh.ToString(), end_num, end_bh.ToString());
  }

  return 0;
}
