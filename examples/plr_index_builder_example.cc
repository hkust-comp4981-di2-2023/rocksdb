#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>

#include "rocksdb/db.h"
#include "rocksdb/slice.h"
#include "rocksdb/options.h"
#include "table/block_based/index_builder.h"
#include "table/format.h"

using namespace ROCKSDB_NAMESPACE;

std::string kDBPath = "/tmp/plr_index_builder_example";

Slice MakeSlice(std::string& element) {
  return Slice(element);
}

BlockHandle MakeBlockHandle(const int& offset_and_size) {
  return BlockHandle(offset_and_size, offset_and_size);
}

int main() {
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


  PLRIndexBuilder* builder = new PLRIndexBuilder(0.3);


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

  printf("Encoded string size: %d\n", ib.index_block_contents.size());
  for (size_t i = 0; i < ib.index_block_contents.size(); ++i) {
    printf("%hhu;", ib.index_block_contents.data()[i]);
  }
  printf("\n");

  delete builder;

  return 0;
}
