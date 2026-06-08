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

#include <memory>
#include <string>

#include "arrow/api.h"
#include "arrow/array/array_nested.h"
#include "arrow/c/abi.h"
#include "arrow/c/bridge.h"
#include "arrow/ipc/json_simple.h"
#include "gtest/gtest.h"
#include "paimon/common/utils/arrow/arrow_input_stream_adapter.h"
#include "paimon/common/utils/arrow/mem_utils.h"
#include "paimon/common/utils/path_util.h"
#include "paimon/format/parquet/parquet_format_defs.h"
#include "paimon/format/parquet/parquet_format_writer.h"
#include "paimon/fs/local/local_file_system.h"
#include "paimon/memory/memory_pool.h"
#include "paimon/testing/utils/testharness.h"
#include "parquet/file_reader.h"

namespace paimon::parquet::test {

class ParquetMetadataCacheTest : public ::testing::Test {
 public:
    void SetUp() override {
        dir_ = paimon::test::UniqueTestDirectory::Create();
        ASSERT_TRUE(dir_);
        fs_ = std::make_shared<LocalFileSystem>();
        pool_ = GetArrowPool(GetDefaultPool());
        file_path_ = PathUtil::JoinPath(dir_->Str(), "test.parquet");
    }

    void WriteSingleIntColumnFile(const std::string& file_path) {
        auto field = arrow::field("f0", arrow::int32());
        auto schema = arrow::schema({field});
        auto array = std::dynamic_pointer_cast<arrow::StructArray>(
            arrow::ipc::internal::json::ArrayFromJSON(arrow::struct_({field}), R"([[1], [2]])")
                .ValueOrDie());

        ASSERT_OK_AND_ASSIGN(std::shared_ptr<OutputStream> out,
                             fs_->Create(file_path, /*overwrite=*/true));
        ::parquet::WriterProperties::Builder builder;
        builder.write_batch_size(array->length());
        builder.max_row_group_length(array->length());
        builder.disable_dictionary();
        auto writer_properties = builder.build();
        ASSERT_OK_AND_ASSIGN(auto format_writer, ParquetFormatWriter::Create(
                                                     out, schema, writer_properties,
                                                     DEFAULT_PARQUET_WRITER_MAX_MEMORY_USE, pool_));
        auto arrow_array = std::make_unique<ArrowArray>();
        ASSERT_TRUE(arrow::ExportArray(*array, arrow_array.get()).ok());
        ASSERT_OK(format_writer->AddBatch(arrow_array.get()));
        ASSERT_OK(format_writer->Finish());
        ASSERT_OK(out->Close());
    }

 protected:
    std::shared_ptr<paimon::test::UniqueTestDirectory> dir_;
    std::shared_ptr<FileSystem> fs_;
    std::shared_ptr<arrow::MemoryPool> pool_;
    std::string file_path_;
};

TEST_F(ParquetMetadataCacheTest, ReusesEntryForSameKeyAndMissesForDifferentKey) {
    WriteSingleIntColumnFile(file_path_);
    auto length = fs_->GetFileStatus(file_path_).value()->GetLen();

    ParquetMetadataCache cache(/*max_weight_bytes=*/128 * 1024 * 1024);
    int load_count = 0;
    auto loader = [&]() -> Result<std::shared_ptr<::parquet::FileMetaData>> {
        ++load_count;
        PAIMON_ASSIGN_OR_RAISE(auto input_stream, fs_->Open(file_path_));
        auto in_stream =
            std::make_shared<ArrowInputStreamAdapter>(std::move(input_stream), pool_, length);
        return ::parquet::ReadMetaData(in_stream);
    };

    ASSERT_OK_AND_ASSIGN(auto metadata1, cache.Get(file_path_, loader));
    ASSERT_OK_AND_ASSIGN(auto metadata2, cache.Get(file_path_, loader));
    ASSERT_EQ(metadata1.get(), metadata2.get());
    ASSERT_EQ(1, load_count);

    // A different cache key must trigger a fresh loader call. The string itself
    // does not need to refer to a real file - the loader closure decides what to
    // open. Using `file_path_ + "#different-key"` keeps that intent explicit.
    ASSERT_OK_AND_ASSIGN(auto metadata3, cache.Get(file_path_ + "#different-key", loader));
    ASSERT_NE(metadata1.get(), metadata3.get());
    ASSERT_EQ(2, load_count);
}

TEST_F(ParquetMetadataCacheTest, NullLoaderResultIsNotCached) {
    ParquetMetadataCache cache(/*max_weight_bytes=*/128 * 1024 * 1024);
    int load_count = 0;
    auto loader = [&]() -> Result<std::shared_ptr<::parquet::FileMetaData>> {
        ++load_count;
        return std::shared_ptr<::parquet::FileMetaData>();
    };

    ASSERT_NOK_WITH_MSG(cache.Get(file_path_, loader), "Parquet metadata loader returned nullptr");
    ASSERT_NOK_WITH_MSG(cache.Get(file_path_, loader), "Parquet metadata loader returned nullptr");
    ASSERT_EQ(2, load_count);
}

}  // namespace paimon::parquet::test
