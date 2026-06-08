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
#include <exception>
#include <map>
#include <memory>
#include <string>
#include <utility>

#include "paimon/common/utils/arrow/arrow_input_stream_adapter.h"
#include "paimon/common/utils/arrow/mem_utils.h"
#include "paimon/format/parquet/parquet_file_batch_reader.h"
#include "paimon/format/parquet/parquet_metadata_cache.h"
#include "paimon/format/reader_builder.h"
#include "paimon/memory/memory_pool.h"
#include "paimon/reader/file_batch_reader.h"
#include "paimon/result.h"
#include "paimon/status.h"
#include "parquet/file_reader.h"

namespace paimon::parquet {

class ParquetReaderBuilder : public ReaderBuilder {
 public:
    ParquetReaderBuilder(const std::map<std::string, std::string>& options, int32_t batch_size,
                         std::shared_ptr<ParquetMetadataCache> metadata_cache)
        : batch_size_(batch_size),
          pool_(GetDefaultPool()),
          options_(options),
          metadata_cache_(std::move(metadata_cache)) {}

    ReaderBuilder* WithMemoryPool(const std::shared_ptr<MemoryPool>& pool) override {
        pool_ = pool;
        return this;
    }

    Result<std::unique_ptr<FileBatchReader>> Build(
        const std::shared_ptr<InputStream>& path) const override {
        PAIMON_ASSIGN_OR_RAISE(int64_t file_length, path->Length());
        std::shared_ptr<arrow::MemoryPool> arrow_pool = GetArrowPool(pool_);
        auto input_stream =
            std::make_shared<ArrowInputStreamAdapter>(path, arrow_pool, file_length);

        PAIMON_ASSIGN_OR_RAISE(std::shared_ptr<::parquet::FileMetaData> file_metadata,
                               LoadCachedMetadata(path, input_stream, arrow_pool));

        return ParquetFileBatchReader::Create(std::move(input_stream), arrow_pool, options_,
                                              batch_size_, std::move(file_metadata));
    }

    Result<std::unique_ptr<FileBatchReader>> Build(const std::string& path) const override {
        return Status::Invalid("do not support build reader with path in parquet format");
    }

 private:
    /// Resolve the cached Parquet FileMetaData for `path` if a metadata cache is
    /// configured. Returns nullptr (not an error) when the cache is disabled or
    /// the input stream does not expose a stable URI; downstream code treats a
    /// null metadata as "load it lazily on first use".
    Result<std::shared_ptr<::parquet::FileMetaData>> LoadCachedMetadata(
        const std::shared_ptr<InputStream>& path,
        const std::shared_ptr<ArrowInputStreamAdapter>& input_stream,
        const std::shared_ptr<arrow::MemoryPool>& arrow_pool) const {
        if (metadata_cache_ == nullptr) {
            return std::shared_ptr<::parquet::FileMetaData>();
        }
        PAIMON_ASSIGN_OR_RAISE(std::string file_uri, path->GetUri());
        if (file_uri.empty()) {
            return std::shared_ptr<::parquet::FileMetaData>();
        }
        PAIMON_ASSIGN_OR_RAISE(
            ::parquet::ReaderProperties reader_properties,
            ParquetFileBatchReader::CreateReaderProperties(arrow_pool, options_));
        auto loader = [input_stream, reader_properties]()
            -> Result<std::shared_ptr<::parquet::FileMetaData>> {
            try {
                return ::parquet::ParquetFileReader::Open(input_stream, reader_properties)
                    ->metadata();
            } catch (const std::exception& e) {
                return Status::Invalid("ParquetReaderBuilder::Build: ", e.what());
            } catch (...) {
                return Status::UnknownError("ParquetReaderBuilder::Build: unknown error");
            }
        };
        return metadata_cache_->Get(file_uri, loader);
    }

    int32_t batch_size_ = -1;
    std::shared_ptr<MemoryPool> pool_;
    std::map<std::string, std::string> options_;
    std::shared_ptr<ParquetMetadataCache> metadata_cache_;
};

}  // namespace paimon::parquet
