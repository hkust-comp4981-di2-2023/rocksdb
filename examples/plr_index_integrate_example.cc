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

std::string kDBPath = "/tmp/plr_index_integrate_example";

Slice MakeSlice(std::string& element) {
  return Slice(element);
}

BlockHandle MakeBlockHandle(const int& offset_and_size) {
  return BlockHandle(offset_and_size, offset_and_size);
}

int main() {
  // TODO(fyp): start from here (mainly for debugging) before writing test cases
  return 0;
}
