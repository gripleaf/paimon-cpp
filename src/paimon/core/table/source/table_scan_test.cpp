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

#include "paimon/table/source/table_scan.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "paimon/defs.h"
#include "paimon/fs/file_system.h"
#include "paimon/scan_context.h"
#include "paimon/status.h"
#include "paimon/testing/utils/testharness.h"

namespace paimon::test {
namespace {

void WriteSchemaFile(const std::shared_ptr<FileSystem>& fs, const std::string& table_path,
                     int64_t schema_id, const std::string& schema_json) {
    ASSERT_OK(fs->WriteFile(table_path + "/schema/schema-" + std::to_string(schema_id),
                            schema_json, /*overwrite=*/false));
}

}  // namespace

TEST(TableScanTest, TestNoSnapshot) {
    std::string path = paimon::test::GetDataDir() +
                       "/orc/append_table_with_nested_type.db/append_table_with_nested_type/";
    ScanContextBuilder builder(path);
    builder.AddOption(Options::FILE_FORMAT, "orc");
    ASSERT_OK_AND_ASSIGN(auto context, builder.Finish());
    ASSERT_OK_AND_ASSIGN(auto table_scan, TableScan::Create(std::move(context)));
    ASSERT_OK_AND_ASSIGN(auto plan, table_scan->CreatePlan());
    ASSERT_FALSE(plan->SnapshotId());
    ASSERT_TRUE(plan->Splits().empty());
}

TEST(TableScanTest, TestNonExistTable) {
    std::string path = paimon::test::GetDataDir() + "/non-exist.db/non-exist/";
    ScanContextBuilder builder(path);
    builder.AddOption(Options::FILE_FORMAT, "orc");
    ASSERT_OK_AND_ASSIGN(auto context, builder.Finish());
    ASSERT_NOK_WITH_MSG(TableScan::Create(std::move(context)), "not found latest schema");
}

TEST(TableScanTest, TestNoSchemaEvolution) {
    std::string path =
        paimon::test::GetDataDir() + "/orc/pk_table_with_alter_table.db/pk_table_with_alter_table/";
    ScanContextBuilder builder(path);
    builder.AddOption(Options::FILE_FORMAT, "orc");
    ASSERT_OK_AND_ASSIGN(auto context, builder.Finish());
    ASSERT_NOK_WITH_MSG(TableScan::Create(std::move(context)), "do not support schema evolution");
}

TEST(TableScanTest, TestPkPropertyOnlySchemaChange) {
    auto dir = UniqueTestDirectory::Create();
    const std::string path = dir->Str();
    const std::string schema0 = R"({
        "version" : 3,
        "id" : 0,
        "fields" : [ {
                "id" : 0,
                "name" : "pk",
                "type" : "INT NOT NULL"
        }, {
                "id" : 1,
                "name" : "v",
                "type" : "STRING"
        } ],
        "highestFieldId" : 1,
        "partitionKeys" : [],
        "primaryKeys" : [ "pk" ],
        "options" : {
                "bucket" : "1",
                "file.format" : "orc",
                "manifest.format" : "orc"
        },
        "timeMillis" : 1000
    })";
    const std::string schema1 = R"({
        "version" : 3,
        "id" : 1,
        "fields" : [ {
                "id" : 0,
                "name" : "pk",
                "type" : "INT NOT NULL"
        }, {
                "id" : 1,
                "name" : "v",
                "type" : "STRING"
        } ],
        "highestFieldId" : 1,
        "partitionKeys" : [],
        "primaryKeys" : [ "pk" ],
        "options" : {
                "bucket" : "1",
                "file.format" : "orc",
                "manifest.format" : "orc",
                "custom.property" : "new-value"
        },
        "timeMillis" : 2000
    })";
    WriteSchemaFile(dir->GetFileSystem(), path, /*schema_id=*/0, schema0);
    WriteSchemaFile(dir->GetFileSystem(), path, /*schema_id=*/1, schema1);

    // Without explicit opt-in, the strict default rejects any cross-schema option diff.
    {
        ScanContextBuilder builder(path);
        builder.AddOption(Options::FILE_FORMAT, "orc");
        ASSERT_OK_AND_ASSIGN(auto context, builder.Finish());
        ASSERT_NOK_WITH_MSG(TableScan::Create(std::move(context)),
                            "do not support schema evolution");
    }

    // Users opt-in to ignoring `custom.property` across schema versions via the regex list, so
    // the property-only schema change is accepted for PK scan.
    ScanContextBuilder builder(path);
    builder.AddOption(Options::FILE_FORMAT, "orc");
    builder.AddOption(Options::SCAN_PK_SCHEMA_COMPATIBILITY_IGNORED_OPTIONS, "custom\\.property");
    ASSERT_OK_AND_ASSIGN(auto context, builder.Finish());
    ASSERT_OK_AND_ASSIGN(auto table_scan, TableScan::Create(std::move(context)));
    ASSERT_OK_AND_ASSIGN(auto plan, table_scan->CreatePlan());
    ASSERT_FALSE(plan->SnapshotId());
    ASSERT_TRUE(plan->Splits().empty());
}

TEST(TableScanTest, TestPkSchemaChangeChecksAllSchemas) {
    auto dir = UniqueTestDirectory::Create();
    const std::string path = dir->Str();
    const std::string schema0 = R"({
        "version" : 3,
        "id" : 0,
        "fields" : [ {
                "id" : 0,
                "name" : "pk",
                "type" : "INT NOT NULL"
        }, {
                "id" : 1,
                "name" : "v",
                "type" : "STRING"
        } ],
        "highestFieldId" : 1,
        "partitionKeys" : [],
        "primaryKeys" : [ "pk" ],
        "options" : {
                "bucket" : "1",
                "file.format" : "orc",
                "manifest.format" : "orc"
        },
        "timeMillis" : 1000
    })";
    const std::string schema1_field_changed = R"({
        "version" : 3,
        "id" : 1,
        "fields" : [ {
                "id" : 0,
                "name" : "pk",
                "type" : "INT NOT NULL"
        }, {
                "id" : 1,
                "name" : "v",
                "type" : "STRING"
        }, {
                "id" : 2,
                "name" : "v2",
                "type" : "STRING"
        } ],
        "highestFieldId" : 2,
        "partitionKeys" : [],
        "primaryKeys" : [ "pk" ],
        "options" : {
                "bucket" : "1",
                "file.format" : "orc",
                "manifest.format" : "orc"
        },
        "timeMillis" : 2000
    })";
    const std::string schema2_property_changed = R"({
        "version" : 3,
        "id" : 2,
        "fields" : [ {
                "id" : 0,
                "name" : "pk",
                "type" : "INT NOT NULL"
        }, {
                "id" : 1,
                "name" : "v",
                "type" : "STRING"
        } ],
        "highestFieldId" : 1,
        "partitionKeys" : [],
        "primaryKeys" : [ "pk" ],
        "options" : {
                "bucket" : "1",
                "file.format" : "orc",
                "manifest.format" : "orc",
                "custom.property" : "new-value"
        },
        "timeMillis" : 3000
    })";
    WriteSchemaFile(dir->GetFileSystem(), path, /*schema_id=*/0, schema0);
    WriteSchemaFile(dir->GetFileSystem(), path, /*schema_id=*/1, schema1_field_changed);
    WriteSchemaFile(dir->GetFileSystem(), path, /*schema_id=*/2, schema2_property_changed);

    ScanContextBuilder builder(path);
    builder.AddOption(Options::FILE_FORMAT, "orc");
    ASSERT_OK_AND_ASSIGN(auto context, builder.Finish());
    ASSERT_NOK_WITH_MSG(TableScan::Create(std::move(context)), "do not support schema evolution");
}

}  // namespace paimon::test
