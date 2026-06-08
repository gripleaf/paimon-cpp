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

#include "paimon/factories/factory_creator.h"
#include "paimon/format/parquet.h"
#include "paimon/format/parquet/parquet_file_format_factory.h"
#include "paimon/status.h"

namespace paimon::parquet {

Status ResizeParquetMetadataCache(int64_t max_bytes) {
    auto* factory_creator = ::paimon::FactoryCreator::GetInstance();
    if (factory_creator == nullptr) {
        return Status::Invalid("FactoryCreator is not initialized");
    }
    auto* factory = dynamic_cast<ParquetFileFormatFactory*>(
        factory_creator->Create(ParquetFileFormatFactory::IDENTIFIER));
    if (factory == nullptr) {
        return Status::Invalid("ParquetFileFormatFactory is not registered");
    }
    return factory->ResizeMetadataCache(max_bytes);
}

}  // namespace paimon::parquet
