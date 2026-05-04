# ObjectStore Design

`ObjectStore` stores timestamped opaque byte payloads alongside the columnar
`DataEngine`. It is for data that should be selected by time but should not be
expanded into scalar columns at ingest time.

## Responsibilities

`ObjectStore` owns:

- object topic registration scoped by `DatasetId`
- monotonically non-decreasing timestamped entries per topic
- eager payload storage through `pushOwned()`
- lazy payload storage through `pushLazy()`
- at-or-before timestamp lookup
- entry-index lookup and timestamp views
- per-topic retention budgets
- explicit topic eviction, removal, and clear operations

It does not decode payloads, interpret metadata, choose renderers, or own UI
policy. Topic metadata is opaque JSON retained verbatim for callers that need to
interpret object bytes.

## Data Model

```cpp
struct ObjectTopicDescriptor {
  DatasetId dataset_id;
  std::string topic_name;
  std::string metadata_json;
};

struct ObjectEntry {
  Timestamp timestamp;
  std::variant<
      std::shared_ptr<const std::vector<uint8_t>>,
      std::function<std::vector<uint8_t>()>> payload;
};

struct RetentionBudget {
  int64_t time_window_ns;
  size_t max_memory_bytes;
};
```

Topic names must be unique within one dataset. The same topic name may appear in
different datasets.

Entries in a topic must be pushed in monotonically non-decreasing timestamp
order. Equal timestamps are allowed. Out-of-order writes fail.

## Write Paths

`pushOwned(id, timestamp, payload)` moves the caller-provided vector into a
shared buffer owned by the store. Owned entries contribute to `memoryUsage()`.

`pushLazy(id, timestamp, fetch)` stores a callable instead of bytes. The callable
is invoked on each read and returns a fresh byte vector. Lazy entries do not
contribute to `memoryUsage()` because the store does not retain the bytes between
reads.

Both write paths apply the topic retention budget after the new entry is
inserted.

## Read Paths

`latestAt(id, timestamp)` returns the newest entry whose timestamp is less than
or equal to the query timestamp. It returns `std::nullopt` if the topic is
unknown, empty, or has no entry at or before that time.

`at(id, index)` resolves an entry by sequence index.

`indexAt(id, timestamp)` returns the index that `latestAt()` would resolve.

`entryTimestamps(id)` returns an `EntryTimestampsView` that holds the series read
lock while the timestamp span is inspected.

Resolved entries contain:

```cpp
struct ResolvedObjectEntry {
  Timestamp timestamp;
  std::shared_ptr<const std::vector<uint8_t>> data;
};
```

The shared pointer keeps resolved bytes alive independently of later store
mutation.

## Retention

Retention is configured per topic:

- `time_window_ns > 0`: drop entries older than `newest_push_ts -
  time_window_ns`.
- `max_memory_bytes > 0`: drop oldest entries until owned-payload memory is at
  or below the cap.

Either axis can be zero to disable that axis. Both zero disables automatic
retention.

Automatic retention runs only during `pushOwned()` and `pushLazy()`. Explicit
eviction is available through `evictBefore(id, threshold)` and
`evictAllBefore(threshold)`.

Memory accounting includes only owned payloads. Lazy entries are counted as zero
bytes because the store retains a fetch callable, not the fetched payload.

## Threading

The store has one global shared mutex for topic lookup and one shared mutex per
topic series. Reads can proceed concurrently with reads on the same or different
topics. Writes take the target topic's exclusive lock. Topic registration,
removal, and `clear()` take the global exclusive lock.

## Plugin ABI Bridge

Plugin access to `ObjectStore` is provided by three optional v4 services:

| Service | Host implementation | Purpose |
|---|---|---|
| `pj.source_object_write.v1` | `DatastoreSourceObjectWriteHost` | DataSource plugins register object topics and push owned or lazy entries. |
| `pj.parser_object_write.v1` | `DatastoreParserObjectWriteHost` | MessageParser plugins push entries to a host-bound object topic. |
| `pj.toolbox_object_read.v1` | `DatastoreToolboxObjectReadHost` | Toolbox plugins look up topics and read entries as owning byte handles. |

The raw ABI lives in `pj_base/include/pj_base/plugin_data_api.h`; the C++ SDK
views live in `pj_base/include/pj_base/sdk/plugin_data_api.hpp`.

The toolbox read ABI allocates one owning handle per successful read. The handle
keeps bytes alive until the plugin releases it, even if the store evicts or
removes the underlying topic.

## Tests

Core behavior is covered by:

- `pj_datastore/tests/object_store_test.cpp`
- `pj_datastore/tests/plugin_data_host_object_test.cpp`
- `pj_datastore/tests/plugin_data_host_object_read_test.cpp`
- `pj_datastore/tests/plugin_parser_object_write_test.cpp`
