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

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "paimon/common/utils/generic_lru_cache.h"
#include "paimon/result.h"

namespace parquet {
class FileMetaData;
}  // namespace parquet

namespace paimon::parquet {

/// Cache for parsed Parquet FileMetaData (the footer), keyed by the file's URI.
///
/// Paimon data files are immutable: once written, a given path never changes its
/// content. Therefore a parsed FileMetaData is valid for the lifetime of the file and
/// can be safely reused across reader creations that share this cache instance without
/// any invalidation/staleness check. Reusing it lets the underlying Parquet reader skip
/// re-reading and re-parsing the footer, which is especially impactful when many
/// readers are opened for the same file (e.g. prefetch parallelism).
///
/// The cached FileMetaData is an immutable, read-only object and is shared via
/// shared_ptr, so concurrent reuse from multiple reader threads is safe.
class ParquetMetadataCache {
 public:
    using MetadataLoader = std::function<Result<std::shared_ptr<::parquet::FileMetaData>>()>;

    explicit ParquetMetadataCache(int64_t max_weight_bytes);

    /// Look up cached metadata for the given file uri. On a cache miss, invokes
    /// `loader` (outside the cache lock) to produce the metadata and inserts the result
    /// into the cache before returning it. If `loader` fails, the error is propagated
    /// and nothing is cached. Modeled after CacheManager::GetPage so callers do not
    /// have to orchestrate Get/Put themselves.
    Result<std::shared_ptr<::parquet::FileMetaData>> Get(const std::string& uri,
                                                         const MetadataLoader& loader);

    /// @return The maximum total weight (in bytes) currently configured.
    int64_t GetMaxWeight() const;

    /// Update the maximum total weight (in bytes) at runtime. The new limit is
    /// published immediately; if it is smaller than the current total weight,
    /// entries are evicted in LRU order until the cache fits within the new limit.
    /// Expired entries are also reaped opportunistically.
    void SetMaxWeight(int64_t max_weight_bytes);

 private:
    // This cache only uses GenericLruCache::Get with a supplier. Avoid adding
    // Put-style replacement paths here: GenericLruCache compares shared_ptr
    // values by dereferencing them, while FileMetaData is cached by pointer
    // identity.
    using MetadataLruCache = GenericLruCache<std::string, std::shared_ptr<::parquet::FileMetaData>>;

    MetadataLruCache cache_;
};

}  // namespace paimon::parquet
