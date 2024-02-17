/*
#pragma once

#include "db/dbformat.h"
#include "table/format.h"
#include "table/block_based/block_type.h"


namespace ROCKSDB_NAMESPACE {

struct BlockFetcherParams {
      RandomAccessFileReader* file;
      FilePrefetchBuffer* prefetch_buffer;
      const Footer& footer;
      const ReadOptions& read_options;
      BlockContents* contents;
      const ImmutableCFOptions& ioptions;
      bool do_uncompress;
      bool maybe_compressed;
      BlockType block_type;
      const UncompressionDict& uncompression_dict;
      const PersistentCacheOptions& cache_options;
      MemoryAllocator* memory_allocator = nullptr;
      MemoryAllocator* memory_allocator_compressed = nullptr;
      bool for_compaction = false;

      BlockFetcherParams(RandomAccessFileReader* file,
                          FilePrefetchBuffer* prefetch_buffer,
                          const Footer& footer,
                          const ReadOptions& read_options,
                          BlockContents* contents,
                          const ImmutableCFOptions& ioptions,
                          bool do_uncompress,
                          bool maybe_compressed,
                          BlockType block_type,
                          const UncompressionDict& uncompression_dict,
                          const PersistentCacheOptions& cache_options,
                          MemoryAllocator* memory_allocator = nullptr,
                          MemoryAllocator* memory_allocator_compressed = nullptr,
                          bool for_compaction = false)
          : file(file),
            prefetch_buffer(prefetch_buffer),
            footer(footer),
            read_options(read_options),
            contents(contents),
            ioptions(ioptions),
            do_uncompress(do_uncompress),
            maybe_compressed(maybe_compressed),
            block_type(block_type),
            uncompression_dict(uncompression_dict),
            cache_options(cache_options),
            memory_allocator(memory_allocator),
            memory_allocator_compressed(memory_allocator_compressed),
            for_compaction(for_compaction) {}
    };

}
*/