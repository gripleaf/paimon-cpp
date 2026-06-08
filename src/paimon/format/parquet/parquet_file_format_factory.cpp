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

#include "paimon/format/parquet/parquet_file_format_factory.h"

#include <utility>

#include "paimon/factories/factory.h"
#include "paimon/format/parquet/parquet_file_format.h"
#include "paimon/format/parquet/parquet_metadata_cache.h"

namespace paimon::parquet {

const char ParquetFileFormatFactory::IDENTIFIER[] = "parquet";

ParquetFileFormatFactory::ParquetFileFormatFactory()
    : metadata_cache_(std::make_shared<ParquetMetadataCache>(0)) {}

ParquetFileFormatFactory::~ParquetFileFormatFactory() = default;

Result<std::unique_ptr<FileFormat>> ParquetFileFormatFactory::Create(
    const std::map<std::string, std::string>& options) const {
    // Inject the cache only if it is currently enabled (max weight > 0). When
    // disabled by ResizeMetadataCache(0), pass nullptr so downstream readers behave
    // identically to "no cache configured".
    std::shared_ptr<ParquetMetadataCache> cache =
        metadata_cache_->GetMaxWeight() > 0 ? metadata_cache_ : nullptr;
    return std::make_unique<ParquetFileFormat>(options, std::move(cache));
}

Status ParquetFileFormatFactory::ResizeMetadataCache(int64_t max_bytes) {
    metadata_cache_->SetMaxWeight(max_bytes);
    return Status::OK();
}

REGISTER_PAIMON_FACTORY(ParquetFileFormatFactory);

}  // namespace paimon::parquet
