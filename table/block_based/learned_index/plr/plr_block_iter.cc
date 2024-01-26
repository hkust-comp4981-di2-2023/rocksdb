#pragma once

#include <string>
#include "db/dbformat.h"
#include "rocksdb/comparator.h"
#include "rocksdb/iterator.h"
#include "rocksdb/status.h"
#include "table/format.h"
#include "table/internal_iterator.h"
#include "table/block_based/learned_index/plr/plr_block_iter.h"

namespace ROCKSDB_NAMESPACE {

bool PLRBlockIter::Valid() const {
    return false;
}

void PLRBlockIter::SeekToFirst() {

}

void PLRBlockIter::SeekToLast() {

}

void PLRBlockIter::Seek(const Slice& target) {

}

void PLRBlockIter::Next() {

}

void PLRBlockIter::Prev() {

}

Slice PLRBlockIter::key() const {

}

IndexValue PLRBlockIter::value() const {

}

Status PLRBlockIter::status() const {

}

bool PLRBlockIter::IsOutOfBound() {

}

bool PLRBlockIter::MayBeOutOfLowerBound() {

}

bool PLRBlockIter::MayBeOutOfUpperBound() {

}

void PLRBlockIter::SetPinnedItersMgr(PinnedIteratorsManager* pinned_iters_mgr) {

}

bool PLRBlockIter::IsKeyPinned() const {

}

bool PLRBlockIter::IsValuePinned() const {

}

Status PLRBlockIter::GetProperty(std::string /*prop_name*/, std::string* /*prop*/) {
    return Status::NotSupported("PLRBlockIter::GetProperty");
}

} // namespace ROCKSDB_NAMESPACE


