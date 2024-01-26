//Todo(fyp): Add licenses later lol

#pragma once

#include <string>
#include "db/dbformat.h"
#include "rocksdb/comparator.h"
#include "rocksdb/iterator.h"
#include "rocksdb/status.h"
#include "table/format.h"
#include "table/internal_iterator.h"

namespace ROCKSDB_NAMESPACE {

class PLRBlockIter : public InternalIteratorBase<IndexValue> {
    public:
        bool Valid() const override {return true;}
        void SeekToFirst() override;
        void SeekToLast() override;
        void Seek(const Slice& target) override;
        void Next() override;
        void Prev() override;
        Slice key() const override;
        IndexValue value() const override;
        Status status() const override;
        bool IsOutOfBound() override;
        bool MayBeOutOfLowerBound() override;
        bool MayBeOutOfUpperBound() override;
        void SetPinnedItersMgr(PinnedIteratorsManager* pinned_iters_mgr) override;
        bool IsKeyPinned() const override;
        bool IsValuePinned() const override;
        Status GetProperty(std::string /*prop_name*/, std::string* /*prop*/) {
            return Status::NotSupported("PLRBlockIter::GetProperty");
        }
}

} // namespace ROCKSDB_NAMESPACE