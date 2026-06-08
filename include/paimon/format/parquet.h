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

#include "paimon/status.h"
#include "paimon/visibility.h"

namespace paimon::parquet {

/// Resize the process-wide parquet metadata cache. `max_bytes <= 0` disables the
/// cache for subsequently created readers, and shrinks the existing cache down to
/// the new limit immediately (entries evicted in LRU order). The cache is
/// initialized eagerly when the parquet file format factory is registered, so this
/// function is safe to call at any time.
PAIMON_EXPORT Status ResizeParquetMetadataCache(int64_t max_bytes);

}  // namespace paimon::parquet
