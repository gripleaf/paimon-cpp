.. Copyright 2024-present Alibaba Inc.

.. Licensed under the Apache License, Version 2.0 (the "License");
.. you may not use this file except in compliance with the License.
.. You may obtain a copy of the License at

..   http://www.apache.org/licenses/LICENSE-2.0

.. Unless required by applicable law or agreed to in writing, software
.. distributed under the License is distributed on an "AS IS" BASIS,
.. WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
.. See the License for the specific language governing permissions and
.. limitations under the License.

Read
====
Paimon by functionality can be divided into two layers:

- Control Plane: Responsible for accessing and managing Meta (snapshot, manifest, etc.), including:
  - Catalog / Database access
  - Table retrieval
  - Collection and resolution of data files

- Data Plane: Responsible for accessing actual data files, including:
  - Readers for various file formats
  - Coordinated reading of file collections

The control plane and data plane interact primarily via DataSplit (the query plan). C++ Paimon currently supports a standard
DataSplit protocol which includes the necessary meta information to access data files. With DataSplit, a high-performance
data access path can be integrated.

At compute time, the execution engine (reader) does not need to be aware of the concrete table type or its metadata details.
It only needs to follow the instructions within the DataSplit (query plan) to perform data reading operations.

With the layered abstraction of the control plane and data plane, and the use of DataSplit as a stable protocol interface,
the two layers can evolve their functionality and optimize code relatively independently. This design also enables
cross-language task scheduling and interaction (e.g., Java and C++), substantially reducing engineering maintenance costs
across the two language ecosystems.

Parquet Page Index Filtering
----------------------------

Paimon C++ supports page-level pruning for Parquet files that contain Parquet
ColumnIndex and OffsetIndex metadata. This optimization narrows the read range
after file-level and row-group-level pruning. It is useful for selective
queries on Parquet files whose row groups contain multiple pages.

The read path is layered as follows:

1. File and row-group pruning
   The scan and read path first applies existing file metadata and Parquet
   row-group statistics pruning. For Parquet files, row-group pruning is
   delegated to Arrow Dataset metadata filtering.

2. Page index pruning
   For the remaining row groups, Paimon C++ reads the Parquet page index and
   evaluates the predicate against page-level min/max/null statistics. The
   result is represented as row ranges inside each row group. A row group can
   then be skipped, read fully, or read through a page-filtered path.

3. Page-filtered reading
   When only part of a row group may match, Paimon C++ uses the OffsetIndex to
   map selected pages to byte ranges. Non-overlapping pages are skipped at the
   Parquet page reader level, and the remaining rows are read with
   ``SkipRecords`` and ``ReadRecords`` so that projected columns stay aligned.

The page index filter is conservative for predicates. A page is skipped only
when page-level statistics prove that no row in the page can match. If the
metadata is missing or a predicate cannot be evaluated safely, the reader falls
back to the full row group or full column chunk path.

Supported Predicate Semantics
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Page index filtering supports simple leaf predicates on Parquet leaf columns,
and compound predicates built from them:

- ``Equal`` keeps pages whose min/max range may contain the literal.
- ``NotEqual`` skips only pages that are proven to contain no matching value.
- ``LessThan`` and ``LessOrEqual`` use page minimum values.
- ``GreaterThan`` and ``GreaterOrEqual`` use page maximum values.
- ``IsNull`` and ``IsNotNull`` use null page and null count metadata when
  available.
- ``In`` is evaluated as the union of equality checks.
- ``And`` intersects row ranges from child predicates.
- ``Or`` unions row ranges from child predicates.

Unsupported predicates, unsupported physical type comparisons, or missing page
index metadata do not fail the read. They disable page-level pruning for the
affected predicate or row group.

Interaction with Bitmap Selection
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

When a selection bitmap is provided by deletion vectors, global indexes, or
other upper-layer pruning, Paimon C++ first uses it to remove row groups that
do not intersect the bitmap. If page index filtering is also active, the
page-derived row ranges are intersected with the selection bitmap inside each
remaining row group. This avoids reading pages and rows that are already known
to be unselected.

Configuration
~~~~~~~~~~~~~

The optimization is controlled by the following Parquet options:

- ``parquet.write.enable-page-index``
  Enables writing Parquet ColumnIndex and OffsetIndex metadata for newly
  written files. The default value is ``true``.

- ``parquet.read.enable-page-index-filter``
  Enables page-level pruning during Parquet reads. The default value is
  ``true``.

- ``parquet.read.enable-pre-buffer``
  Controls Parquet pre-buffer behavior. When page filtering is active, Paimon
  C++ can pre-buffer selected page byte ranges instead of whole column chunks.

Metrics
~~~~~~~

The reader exposes page-index related counters through reader metrics:

- ``parquet.read.page-index.row-groups.total``
- ``parquet.read.page-index.row-groups.skipped``
- ``parquet.read.page-index.row-groups.partial``
- ``parquet.read.page-index.fallback.count``

These metrics help determine whether page index filtering is reducing the
number of row groups and pages read, or falling back because page index metadata
is unavailable.

Limitations
~~~~~~~~~~~

- Page index filtering applies to Parquet leaf columns. Nested column predicate
  pruning requires an unambiguous mapping from the Paimon field to a Parquet
  leaf column.
- It is an I/O pruning optimization, not a replacement for row-level predicate
  evaluation. Page-level min/max statistics can only identify pages that may
  match.
- Existing files without Parquet page index metadata are read through the
  normal row-group or column-chunk path.

Schema Evolution
-----------------------
Scope and Compatibility
~~~~~~~~~~~~~~~~~~~~~~~~

C++ Paimon supports all evolution kinds available in Java Paimon for non-nested types:

- Add column
- Drop column
- Reorder columns
- Rename column
- Change column type

.. note::

  - Only non-nested type evolution is supported. Nested columns (struct, array, map) are not supported.
  - Partition keys: Only column reordering is supported; other operations are not supported (consistent with Java Paimon).
  - Primary key:

    - Adding or dropping columns is not supported.
    - Other operations are supported (consistent with Java Paimon).

Per-File Schema via Field IDs
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

In DataSplit, each file may have a completely different data schema. Paimon uses field IDs to uniquely identify fields.

Overflow Behavior Disclaimer
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Overflow behavior is undefined for C++ and Java Paimon. Results in overflow scenarios may:

- Be incorrect values,
- Return an error status,
- Or be null.

C++ Paimon does not guarantee identical results to Java Paimon in overflow scenarios. Users should not rely on identical
return values between implementations.

Type Change Support Matrix
~~~~~~~~~~~~~~~~~~~~~~~~~~
The table below indicates support for changing a column type from ``source`` to ``target``. Refer to the numbered notes below the table
for caveats.

.. list-table::
   :header-rows: 1
   :widths: 12 10 10 10 10 10 10 8 12 10 8 18 10

   * - src \\ target
     - tinyint
     - smallint
     - int
     - bigint
     - float
     - double
     - bool
     - string
     - binary
     - date
     - timestamp (without tz)
     - decimal
   * - tinyint
     - ✅
     - ✅
     - ✅
     - ✅
     - ✅
     - ✅
     - ✅
     - ✅
     - ❌
     - ❌
     - ❌
     - ✅
   * - smallint
     - ✅ 1️⃣
     - ✅
     - ✅
     - ✅
     - ✅
     - ✅
     - ✅
     - ✅
     - ❌
     - ❌
     - ❌
     - ✅
   * - int
     - ✅ 1️⃣
     - ✅ 1️⃣
     - ✅
     - ✅
     - ✅
     - ✅
     - ✅
     - ✅
     - ❌
     - ❌
     - ✅ 1️⃣
     - ✅
   * - bigint
     - ✅ 1️⃣
     - ✅ 1️⃣
     - ✅ 1️⃣
     - ✅
     - ✅
     - ✅
     - ✅
     - ✅
     - ❌
     - ❌
     - ✅ 6️⃣
     - ✅
   * - float
     - ✅ 2️⃣
     - ✅ 2️⃣
     - ✅ 2️⃣
     - ✅ 2️⃣
     - ✅
     - ✅
     - ✅
     - ✅ 3️⃣ 4️⃣
     - ❌
     - ❌
     - ❌
     - ✅
   * - double
     - ✅ 2️⃣
     - ✅ 2️⃣
     - ✅ 2️⃣
     - ✅ 2️⃣
     - ✅ 2️⃣
     - ✅
     - ✅
     - ✅ 3️⃣ 4️⃣
     - ❌
     - ❌
     - ❌
     - ✅
   * - bool
     - ✅
     - ✅
     - ✅
     - ✅
     - ✅
     - ✅
     - ✅
     - ✅
     - ❌
     - ❌
     - ❌
     - ✅
   * - string
     - ✅
     - ✅
     - ✅
     - ✅
     - ✅ 3️⃣
     - ✅ 3️⃣
     - ✅
     - ✅
     - ✅
     - ✅
     - ✅ 5️⃣
     - ✅ 7️⃣
   * - binary
     - ❌
     - ❌
     - ❌
     - ❌
     - ❌
     - ❌
     - ❌
     - ✅
     - ✅
     - ❌
     - ❌
     - ❌
   * - date
     - ❌
     - ❌
     - ❌
     - ❌
     - ❌
     - ❌
     - ❌
     - ✅
     - ❌
     - ✅
     - ✅ 5️⃣
     - ❌
   * - timestamp (without tz)
     - ❌
     - ❌
     - ✅ 1️⃣
     - ✅
     - ❌
     - ❌
     - ❌
     - ✅
     - ❌
     - ✅
     - ✅
     - ❌
   * - decimal
     - ✅ 1️⃣
     - ✅ 1️⃣
     - ✅ 1️⃣
     - ✅ 1️⃣
     - ✅
     - ✅
     - ❌
     - ✅
     - ❌
     - ❌
     - ❌
     - ✅

.. admonition:: Overflow Behavior Notes
  :class: note

  1️⃣ Integer downcast overflow behavior matches Java in specific cases.
    Example: smallint -> tinyint, 32767 becomes -1; int -> smallint, -2147483648 becomes 0.

  2️⃣ Floating-point overflow behavior is partially consistent with Java and partially different.
    Example: float -> tinyint
      - Java: MAX_FLOAT -> -1, INFINITY -> -1
      - C++:  MAX_FLOAT -> 0, INFINITY -> 0

  3️⃣ Keyword differences for special float/double values:
    - Java: Infinity, -Infinity, NaN
    - C++:  inf, -inf, nan

  4️⃣ Printing difference:
    - C++ Paimon prints 1.0 as ``1``
    - Java Paimon prints 1.0 as ``1.0``

  5️⃣ Timestamp precision and range differences:
    - Java Paimon: 0000-01-01 00:00:00.000000000 to 9999-12-31 23:59:59.999999999
    - C++ Paimon:  1677-09-21 00:12:43.145224192 to 2262-04-11 23:47:16.854775807
    - C++ only supports nanosecond precision; range is smaller.

  6️⃣ bigint -> timestamp range differences:
    - Java Paimon (ms):   ``[MIN_INT64/1000, MAX_INT64/1000]`` seconds
    - C++ Paimon (ns):    ``[MIN_INT64/1e9,  MAX_INT64/1e9]`` seconds

  7️⃣ string -> decimal with precision > 38:
    - C++ returns ``null`` if parsing would overflow 128-bit arithmetic.
    - Java may rescale and return a value based on the rescaled precision.
    - Example input: ``1111111111111111111111111111111111111.15``, Java returns: ``1111111111111111111111111111111111111.2``, C++ returns: ``null``

Implementation Guidance
~~~~~~~~~~~~~~~~~~~~~~~

- Use DataSplit as the sole interface between control and data planes. Treat it as the canonical query plan contract.
- Resolve field types and IDs per file; prefer inline data file metadata, fallback to table schema files when necessary.
- Expect per-file schema variability; design readers to align by field IDs rather than positional indices.
- Do not assume identical overflow semantics across C++ and Java; tests should validate acceptable ranges and nullability.
- For timestamp handling, consider precision/range constraints in C++ when interoperating with Java-produced data splits.
