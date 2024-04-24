## HKUST COMP 4981 Final Year Project (2023-24): Learned Indexes for Commercial Database

This repository contains source code of RocksDB and the implementation of our learned index, Sherry.

### Basic Usage
Regarding the basic operations of RocksDB, please refer to [this wikipedia](https://github.com/facebook/rocksdb/wiki/Basic-Operations). Several important operations are: `Put()`, `Get()`, `Delete()`, and `Flush()`.

Specifically, to enable Sherry as the underlying index block, pass the following option when creating/opening a database:

```
// Specify options to enable Sherry.
rocksdb::Options options;
BlockBasedTableOptions table_options;

table_options.index_type = BlockBasedTableOptions::kLearnedIndexWithPLR;
options.table_factory.reset(NewBlockBasedTableFactory(table_options));

// Open a database with custom options.
rocksdb::DB* db;
rocksdb::Status status = rocksdb::DB::Open(options, "/tmp/testdb", &db);
assert(status.ok());
```

### Important Branches
There are several branches related to our work:
- `fyp-6.8.fb-dev`: The development branch of our project, not used for benchmarking.
- `fyp-6.8.fb-dev-debug`: Used for benchmarking Sherry in terms of time and space performance.
- `6.8.fb`: Used for benchmarking the original implementation of RocksDB index block in terms of space performance.
- `6.8.fb-benchmark`: Used for benchmarking the original implementation of RocksDB index block in terms of time performance.

## RocksDB: A Persistent Key-Value Store for Flash and RAM Storage

[![Linux/Mac Build Status](https://travis-ci.org/facebook/rocksdb.svg?branch=master)](https://travis-ci.org/facebook/rocksdb)
[![Windows Build status](https://ci.appveyor.com/api/projects/status/fbgfu0so3afcno78/branch/master?svg=true)](https://ci.appveyor.com/project/Facebook/rocksdb/branch/master)
[![PPC64le Build Status](http://140.211.168.68:8080/buildStatus/icon?job=Rocksdb)](http://140.211.168.68:8080/job/Rocksdb)

RocksDB is developed and maintained by Facebook Database Engineering Team.
It is built on earlier work on [LevelDB](https://github.com/google/leveldb) by Sanjay Ghemawat (sanjay@google.com)
and Jeff Dean (jeff@google.com)

This code is a library that forms the core building block for a fast
key-value server, especially suited for storing data on flash drives.
It has a Log-Structured-Merge-Database (LSM) design with flexible tradeoffs
between Write-Amplification-Factor (WAF), Read-Amplification-Factor (RAF)
and Space-Amplification-Factor (SAF). It has multi-threaded compactions,
making it especially suitable for storing multiple terabytes of data in a
single database.

Start with example usage here: https://github.com/facebook/rocksdb/tree/master/examples

See the [github wiki](https://github.com/facebook/rocksdb/wiki) for more explanation.

The public interface is in `include/`.  Callers should not include or
rely on the details of any other header files in this package.  Those
internal APIs may be changed without warning.

Design discussions are conducted in https://www.facebook.com/groups/rocksdb.dev/

## License

RocksDB is dual-licensed under both the GPLv2 (found in the COPYING file in the root directory) and Apache 2.0 License (found in the LICENSE.Apache file in the root directory).  You may select, at your option, one of the above-listed licenses.
