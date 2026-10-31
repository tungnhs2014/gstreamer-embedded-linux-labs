# Lab 03 — GstBuffer, GstMemory and Data Ownership

Theory: [Lesson 03](../../knowledge/03-gstreamer-gstbuffer-gstmemory-data-lifecycle-updated.md) · [Full theory-to-lab map](../../knowledge/README.md). This lab verifies only the outcomes stated in its examples and completion criteria.

## 1. Objective

This lab introduces the GStreamer data model used to carry media through a pipeline.

The main focus is:

```text
GstAllocator
    ↓
GstMemory
    ↓
GstBuffer
    ├── payload
    ├── timestamps
    ├── flags
    └── GstMeta
```

The lab also covers:

* buffer reference counting;
* borrowed vs owned references;
* buffer mapping;
* buffer writability;
* shallow vs deep copies;
* buffer metadata;
* buffer pools;
* ownership transfer through `gst_pad_push()`;
* borrowed buffers in pad probes;
* RAII-based buffer mapping in C++.

The examples are intentionally incremental and focus on ownership and data lifetime rather than camera devices or media-format interpretation.

---

## 2. Examples

### Example 01 — GstBuffer, GstMemory and GstAllocator

Source:

```text
src/01_buffer_memory.cpp
```

Topics:

* `GstAllocator`
* `GstMemory`
* `GstBuffer`
* multiple memory blocks per buffer
* `gst_buffer_append_memory()`
* `gst_buffer_peek_memory()`
* `gst_buffer_get_memory()`
* ownership transfer

Mental model:

```text
GstAllocator
     ↓ allocates
 GstMemory
     ↓ attached to
 GstBuffer
```

Important ownership rules:

```text
gst_buffer_peek_memory()
    → borrowed reference
    → do not unref

gst_buffer_get_memory()
    → owned reference
    → caller must unref

gst_buffer_append_memory()
    → ownership transferred to GstBuffer
```

---

### Example 02 — Mapping and Writability

Source:

```text
src/02_mapping_writability.cpp
```

Topics:

* `gst_buffer_map()`
* `gst_buffer_unmap()`
* `GST_MAP_READ`
* `GST_MAP_WRITE`
* `gst_buffer_is_writable()`
* `gst_buffer_make_writable()`
* copy-on-write behavior

Basic mapping lifecycle:

```text
map
 ↓
access map.data
 ↓
unmap
```

Safe modification workflow:

```text
owned GstBuffer
      ↓
gst_buffer_make_writable()
      ↓
gst_buffer_map(..., GST_MAP_WRITE)
      ↓
modify payload
      ↓
gst_buffer_unmap()
```

After `gst_buffer_unmap()`, the mapped data pointer must no longer be used.

---

### Example 03 — Reference Counting and Buffer Copies

Source:

```text
src/03_buffer_copy.cpp
```

Topics:

* `gst_buffer_ref()`
* `gst_buffer_unref()`
* `gst_buffer_copy()`
* `gst_buffer_copy_deep()`
* shared memory
* independent payload copies

Mental model:

```text
gst_buffer_ref()
    → same GstBuffer
    → additional reference

gst_buffer_copy()
    → new GstBuffer structure
    → memory may remain shared

gst_buffer_copy_deep()
    → new GstBuffer
    → payload copied into independent memory
```

Deep copies should not be introduced without a clear ownership or lifetime requirement because they may increase memory bandwidth, allocation cost and latency.

---

### Example 04 — Timestamp, Flags and GstMeta

Source:

```text
src/04_buffer_metadata.cpp
```

Topics:

* `PTS`
* `DTS`
* duration
* offset
* buffer flags
* `GstMeta`
* metadata copying
* payload vs metadata

Mental model:

```text
GstBuffer
├── GstMemory
├── PTS
├── DTS
├── Duration
├── Offset
├── Flags
└── GstMeta
```

A buffer is therefore more than a pointer to media bytes.

Important distinction:

```text
payload
    = media data stored through GstMemory

metadata
    = timing, flags and GstMeta associated with the buffer
```

---

### Example 05 — GstBufferPool

Source:

```text
src/05_buffer_pool.cpp
```

Topics:

* `GstBufferPool`
* pool configuration
* buffer acquisition
* buffer reuse
* maximum outstanding buffers
* buffer retention
* backpressure

Lifecycle:

```text
pool
 ↓
acquire buffer
 ↓
use buffer
 ↓
last gst_buffer_unref()
 ↓
buffer returns to pool
 ↓
reuse
```

The example demonstrates an important runtime behavior:

```text
retain too many buffers
        ↓
pool has no free buffer
        ↓
acquire may block or fail in non-blocking mode
```

This becomes especially important in camera and real-time media pipelines.

---

### Example 06 — gst_pad_push() Ownership Transfer

Source:

```text
src/06_pad_push_ownership.cpp
```

Topics:

* source pads
* `gst_pad_push()`
* transfer-full ownership
* controlled `GST_FLOW_NOT_LINKED`
* retaining a separate reference

Critical ownership rule:

```text
caller owns buffer
      ↓
gst_pad_push(pad, buffer)
      ↓
ownership transferred
```

The caller must not unref the same reference after `gst_pad_push()`.

The standalone source pad sends stream-start and segment events before its
buffer. Since it has no peer, `GST_FLOW_NOT_LINKED` is the expected result.

If the application needs to retain access:

```text
gst_buffer_ref(buffer)
      ↓
gst_pad_push(pad, buffer)
      ↓
use retained reference
      ↓
gst_buffer_unref(retained)
```

---

### Example 07 — Pad Probe and RAII Mapping

Source:

```text
src/07_pad_probe_raii.cpp
```

Topics:

* `GST_PAD_PROBE_TYPE_BUFFER`
* `GstPadProbeInfo`
* borrowed buffer references
* read-only inspection
* C++ RAII
* automatic `map/unmap` pairing

Pad probe ownership:

```text
GstPadProbeInfo
       ↓
borrowed GstBuffer
       ↓
inspect only
```

The callback must not unref the borrowed buffer.

If the application needs to retain it beyond the callback:

```text
borrowed buffer
      ↓
gst_buffer_ref()
      ↓
owned retained reference
```

The RAII wrapper demonstrates:

```text
constructor
    ↓
gst_buffer_map()

scope exit
    ↓
destructor
    ↓
gst_buffer_unmap()
```

RAII manages the mapping lifetime, not the lifetime of the `GstBuffer`.

---

## 3. Build

Activate the project environment:

```bash
cd ~/TungNHS/Gstreamer-Lesson

source .venv/bin/activate
source env.sh
```

Enter the lab:

```bash
cd practice/lesson-03
```

First build:

```bash
meson setup build
meson compile -C build
```

Subsequent builds:

```bash
meson compile -C build
```

---

## 4. Run

```bash
./build/01_buffer_memory
./build/02_mapping_writability
./build/03_buffer_copy
./build/04_buffer_metadata
./build/05_buffer_pool
./build/06_pad_push_ownership
./build/07_pad_probe_raii
```

---

## 5. Core Ownership Rules

### GstBuffer

`GstBuffer` is a `GstMiniObject`.

Use:

```cpp
gst_buffer_ref(buffer);
gst_buffer_unref(buffer);
```

Do not treat it as a `GstObject`.

---

### GstMemory

A `GstBuffer` may contain zero, one or multiple `GstMemory` blocks.

```text
GstBuffer
├── GstMemory 0
├── GstMemory 1
└── ...
```

The number of `GstMemory` objects must not be assumed to equal the number of video planes.

---

### Borrowed Reference

Example:

```cpp
gst_buffer_peek_memory(...)
```

or a buffer obtained from a pad probe.

Rule:

```text
borrowed
    → inspect/use within valid lifetime
    → do not unref
```

---

### Owned Reference

Example:

```cpp
gst_buffer_get_memory(...)
gst_buffer_ref(...)
```

Rule:

```text
owned reference
    → caller must eventually unref
```

---

### Ownership Transfer

Example:

```cpp
gst_buffer_append_memory(buffer, memory);
gst_pad_push(pad, buffer);
```

After ownership transfer, the caller must not release the transferred reference again.

---

## 6. Writability

A shared buffer should not be modified directly.

Mental model:

```text
single reference
    ↓
buffer usually writable

multiple references
    ↓
buffer not writable
```

Modification pattern:

```text
own buffer
    ↓
make writable
    ↓
map WRITE
    ↓
modify
    ↓
unmap
```

`gst_buffer_make_writable()` may return the original buffer or a new writable buffer.

---

## 7. Buffer Copies

```text
gst_buffer_ref()
    = same object

gst_buffer_copy()
    = new buffer structure
      memory may be shared

gst_buffer_copy_deep()
    = new buffer
      independent payload copy
```

Use deep copies only when independent payload lifetime or modification is actually required.

---

## 8. Buffer Pool Mental Model

```text
GstBufferPool
      ↓
prepares/reuses buffers
      ↓
application or element acquires
      ↓
buffer remains outstanding
      ↓
last reference disappears
      ↓
buffer returns to pool
```

Holding references longer than necessary can prevent reuse and create:

* pool exhaustion;
* blocking;
* backpressure;
* additional latency.

---

## 9. Relation to V4L2

At this stage, keep the abstraction boundary clear:

```text
V4L2 capture buffer
    = Linux video capture contract

GstBuffer
    = GStreamer media-data object
```

A GStreamer implementation may copy capture data or wrap/retain underlying memory.

Do not assume:

```text
V4L2 buffer == GstBuffer
```

and do not claim zero-copy without runtime evidence.

Detailed V4L2 ownership and capture lifecycle belong to Lab 06.

---

## 10. Scope Boundary

This lab focuses on:

```text
GstBuffer
GstMemory
GstAllocator
reference counting
mapping
writability
copy semantics
metadata
buffer pools
dataflow ownership
```

The following topics are intentionally handled later:

```text
YUY2 / MJPEG / raw format layout   → Lab 04
Caps negotiation                   → Lab 05
V4L2 camera capture                → Lab 06
Pipeline state machine             → Lab 07
GstBus / GstMessage                → Lab 08
```

---

## 11. Completion Criteria

Lab 03 is complete when:

```text
[PASS] Example 01 builds and runs
[PASS] Example 02 builds and runs
[PASS] Example 03 builds and runs
[PASS] Example 04 builds and runs
[PASS] Example 05 builds and runs
[PASS] Example 06 builds and runs
[PASS] Example 07 builds and runs
[PASS] Build produces no project-source warnings
```

The programs also check their core observations before printing PASS: memory
block counts; writable-buffer byte isolation; copy contents and isolation;
metadata values without payload; pool exhaustion and reuse; the retained
buffer after a failed push; and one probe callback reading byte `0x7E`.

After completing the lab, you should be able to explain:

```text
GstAllocator → GstMemory → GstBuffer

borrowed vs owned references

map → access → unmap

buffer writability

ref vs shallow copy vs deep copy

payload vs metadata

buffer pool acquire/release/reuse

gst_pad_push() ownership transfer

pad-probe borrowed buffer lifetime
```

## Lab Status

```text
LAB 03: PASS
```

Change the status to `PASS` only after all examples have been successfully built and executed.

All seven examples were built and run with the custom GStreamer 1.28.6
installation on 2026-10-24. The two standalone-pad examples completed without
stream-start or segment warnings after sending the required events.
