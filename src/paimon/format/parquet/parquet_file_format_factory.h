/*
 * Copyright 2024-present Alibaba Inc.
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
#include <map>
#include <memory>
#include <string>

#include "paimon/format/file_format.h"
#include "paimon/format/file_format_factory.h"
#include "paimon/result.h"

namespace paimon::parquet {

class ParquetMetadataCache;

class ParquetFileFormatFactory : public FileFormatFactory {
 public:
    static const char IDENTIFIER[];

    ParquetFileFormatFactory();
    ~ParquetFileFormatFactory() override;

    const char* Identifier() const override {
        return IDENTIFIER;
    }

    Result<std::unique_ptr<FileFormat>> Create(
        const std::map<std::string, std::string>& options) const override;

    /// Resize the process-wide parquet metadata cache held by this factory.
    /// `max_bytes <= 0` disables further caching: subsequent Create() calls inject
    /// nullptr instead of the cache instance, and the cache is shrunk down to the
    /// new limit immediately (entries evicted in LRU order). Note: the cache
    /// instance itself is preserved across resizes so that the cache can be
    /// re-enabled later without losing the singleton identity.
    Status ResizeMetadataCache(int64_t max_bytes);

 private:
    /// Always non-null. Holds all currently cached entries; effective capacity is
    /// controlled by the cache's internal max weight.
    std::shared_ptr<ParquetMetadataCache> metadata_cache_;
};

}  // namespace paimon::parquet
