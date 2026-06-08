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

#include "paimon/format/parquet/parquet_metadata_cache.h"

#include "parquet/metadata.h"

namespace paimon::parquet {

ParquetMetadataCache::ParquetMetadataCache(int64_t max_weight_bytes)
    : cache_(MetadataLruCache::Options{
          .max_weight = max_weight_bytes,
          .expire_after_access_ms = -1,
          // Weight each entry by the size of its thrift-encoded footer so the cache
          // bounds real memory usage rather than entry count.
          .weigh_func = [](const std::string& key,
                           const std::shared_ptr<::parquet::FileMetaData>& value) -> int64_t {
              auto weight = static_cast<int64_t>(key.size());
              if (value) {
                  weight += static_cast<int64_t>(value->size());
              }
              return weight;
          }}) {}

Result<std::shared_ptr<::parquet::FileMetaData>> ParquetMetadataCache::Get(
    const std::string& uri, const MetadataLoader& loader) {
    auto supplier =
        [&loader](const std::string&) -> Result<std::shared_ptr<::parquet::FileMetaData>> {
        PAIMON_ASSIGN_OR_RAISE(std::shared_ptr<::parquet::FileMetaData> metadata, loader());
        if (metadata == nullptr) {
            return Status::Invalid("Parquet metadata loader returned nullptr");
        }
        return metadata;
    };
    return cache_.Get(uri, supplier);
}

int64_t ParquetMetadataCache::GetMaxWeight() const {
    return cache_.GetMaxWeight();
}

void ParquetMetadataCache::SetMaxWeight(int64_t max_weight_bytes) {
    cache_.SetMaxWeight(max_weight_bytes);
}

}  // namespace paimon::parquet
