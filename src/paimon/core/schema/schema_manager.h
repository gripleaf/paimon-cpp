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
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "paimon/core/schema/table_schema.h"
#include "paimon/fs/file_system.h"
#include "paimon/result.h"

namespace arrow {
class Schema;
}  // namespace arrow

namespace paimon {
class FileSystem;

/// Schema Manager to manage schema versions.
class SchemaManager {
 public:
    SchemaManager(const std::shared_ptr<FileSystem>& file_system, const std::string& table_root);
    /// Specify the default branch for data writing.
    SchemaManager(const std::shared_ptr<FileSystem>& file_system, const std::string& table_root,
                  const std::string& branch);

    /// Read schema for schema id. Find schema in cache first.
    Result<std::shared_ptr<TableSchema>> ReadSchema(int64_t schema_id) const;
    Result<std::optional<std::shared_ptr<TableSchema>>> Latest() const;
    Result<std::unique_ptr<TableSchema>> CreateTable(
        const std::shared_ptr<arrow::Schema>& schema,
        const std::vector<std::string>& partition_keys,
        const std::vector<std::string>& primary_keys,
        const std::map<std::string, std::string>& options);

    std::string SchemaDirectory() const;
    Result<bool> SchemaExists(int64_t id) const;
    Result<std::vector<int64_t>> ListAllIds() const;
    // This is not schema evolution support. It only verifies that every other schema version is
    // equivalent to the given schema for primary-key scan, so property-only changes can reuse the
    // normal read path. The schema with the same id as `table_schema` is skipped (self-compare),
    // and option keys whose names match any of the regex patterns in `ignored_option_patterns`
    // are allowed to differ across schema versions.
    Result<bool> IsSchemasCompatibleForPKScan(
        const TableSchema& table_schema,
        const std::vector<std::string>& ignored_option_patterns = {}) const;

 private:
    std::string BranchPath() const;
    std::string ToSchemaPath(int64_t schema_id) const;

 private:
    static constexpr char SCHEMA_PREFIX[] = "schema-";

 private:
    std::shared_ptr<FileSystem> file_system_;
    std::string table_root_;
    const std::string branch_;
    // `schema_cache_` may be queried from parallel scan paths (FileStoreScan executes manifest
    // reads concurrently via `ReadFileEntries`), so reads / writes must be serialized to avoid
    // racing on the underlying std::map.
    mutable std::mutex schema_cache_mutex_;
    mutable std::map<int64_t, std::shared_ptr<TableSchema>> schema_cache_;
};

}  // namespace paimon
