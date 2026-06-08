/*
 * Copyright 2026-present Alibaba Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "paimon/cache/cache.h"

#include <utility>

namespace paimon {

namespace {

class CacheWithKind : public Cache {
 public:
    CacheWithKind(CacheKind kind, std::shared_ptr<Cache> cache)
        : kind_(kind), cache_(std::move(cache)) {}

    Result<std::shared_ptr<CacheValue>> Get(
        const std::shared_ptr<CacheKey>& key,
        std::function<Result<std::shared_ptr<CacheValue>>(const std::shared_ptr<CacheKey>&)>
            supplier) override {
        key->SetKind(kind_);
        return cache_->Get(key, std::move(supplier));
    }

    Status Put(const std::shared_ptr<CacheKey>& key,
               const std::shared_ptr<CacheValue>& value) override {
        key->SetKind(kind_);
        return cache_->Put(key, value);
    }

    void Invalidate(const std::shared_ptr<CacheKey>& key) override {
        key->SetKind(kind_);
        cache_->Invalidate(key);
    }

    void InvalidateAll() override {
        cache_->InvalidateAll();
    }

    size_t Size() const override {
        return cache_->Size();
    }

 private:
    CacheKind kind_;
    std::shared_ptr<Cache> cache_;
};

}  // namespace

std::shared_ptr<Cache> Cache::WarpKind(CacheKind kind, const std::shared_ptr<Cache>& cache) {
    if (!cache) {
        return nullptr;
    }
    return std::make_shared<CacheWithKind>(kind, cache);
}

CacheValue::CacheValue(const MemorySegment& segment, CacheCallback callback)
    : segment_(segment), callback_(std::move(callback)) {}

CacheValue::~CacheValue() = default;

const MemorySegment& CacheValue::GetSegment() const {
    return segment_;
}

void CacheValue::OnEvict(const std::shared_ptr<CacheKey>& key) const {
    if (callback_) {
        callback_(key);
    }
}

bool CacheValue::operator==(const CacheValue& other) const {
    if (this == &other) {
        return true;
    }
    return segment_ == other.segment_;
}

}  // namespace paimon
