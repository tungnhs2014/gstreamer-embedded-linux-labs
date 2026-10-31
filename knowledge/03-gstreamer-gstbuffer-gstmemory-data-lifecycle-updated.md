# Bài 3 — `GstBuffer`, `GstMemory` và vòng đời dữ liệu

> **Lộ trình:** [Bảng đối chiếu lý thuyết ↔ lab](README.md) · [Lab 03](../practice/lesson-03/README.md). Phần CamStream Control là case study của dự án ngoài repo lab; chỉ tiêu chí PASS của lab mới là bằng chứng kiểm chứng tại repo này.

> **Dự án:** CamStream Control
> **Đối tượng:** Người mới học Embedded Linux và GStreamer
> **Phạm vi:** Bài này giải thích cách dữ liệu media được biểu diễn, sở hữu, đọc, sửa, sao chép và tái sử dụng trong GStreamer.

---

# 1. `GstBuffer` trong luồng dữ liệu GStreamer

## 1.1 Mục tiêu và phạm vi bài học

Sau bài này, người học cần:

- hiểu vai trò của `GstBuffer` trong graph;
- phân biệt `GstBuffer`, `GstMemory`, `GstAllocator` và `GstBufferPool`;
- hiểu một buffer không phải lúc nào cũng tương ứng đúng một video frame;
- hiểu reference counting và ownership của `GstMiniObject`;
- phân biệt full reference, borrowed reference và ownership transfer;
- dùng đúng `gst_buffer_ref()` và `gst_buffer_unref()`;
- hiểu contract ownership của `gst_pad_push()`;
- kiểm tra writability trước khi sửa buffer;
- dùng đúng `gst_buffer_make_writable()`;
- map và unmap payload an toàn;
- phân biệt shallow copy và deep copy;
- hiểu vai trò của timestamp, flags và `GstMeta`;
- hiểu vì sao giữ buffer quá lâu có thể làm cạn buffer pool;
- hiểu ranh giới giữa V4L2 capture buffer và `GstBuffer`;
- không suy luận zero-copy khi chưa có bằng chứng.

Bài này chỉ tập trung vào object và vòng đời dữ liệu. Các nội dung sau được để cho bài chuyên sâu tương ứng:

| Nội dung | Bài mở rộng |
|---|---:|
| Bố trí byte của YUY2, video thô và MJPEG | Bài 4 |
| Caps và caps negotiation | Bài 5 |
| V4L2 `QBUF`, `DQBUF`, MMAP và DMABUF | Bài 6 |
| State machine, flushing và đồng bộ thời gian | Bài 7 |
| `appsink`, `appsrc` và xử lý frame trong ứng dụng | Bài tích hợp ứng dụng |

## 1.2 Khái niệm và vai trò của `GstBuffer`

Bài 1 đã giới thiệu `GstBuffer` là đối tượng mang dữ liệu media. Bài 2 đã giải thích các element kết nối qua pad để tạo thành graph. Trong luồng dữ liệu đó, `GstBuffer` là đơn vị dữ liệu được truyền từ element phía trước sang element phía sau.

```text
Element phía trước
    ↓ tạo hoặc xử lý
GstBuffer
    ↓ đi qua link giữa các GstPad
Element phía sau
```

Ví dụ:

```text
v4l2src
    ↓ GstBuffer
jpegdec
    ↓ GstBuffer đầu ra
videoconvert
    ↓ GstBuffer đầu ra
fakesink
```

> **`GstBuffer` là đơn vị truyền dữ liệu cơ bản trong GStreamer, chứa hoặc tham chiếu đến payload thông qua các `GstMemory` và mang thông tin thời gian cùng metadata liên quan.**

Một buffer gồm hai nhóm thông tin chính:

```text
GstBuffer
    ├── Payload
    │     └── 0, 1 hoặc nhiều GstMemory
    │
    └── Thông tin đi kèm
          ├── PTS / DTS / duration
          ├── offset / offset_end
          ├── flags
          └── GstMeta
```

`GstBuffer` không chỉ là một con trỏ byte:

- payload có thể nằm trong nhiều vùng memory;
- memory có thể đến từ nhiều allocator;
- payload có thể nằm trong RAM, shared memory hoặc vùng memory do phần cứng quản lý;
- buffer còn mang timestamp, flags và metadata;
- vòng đời được quản lý bằng reference counting.

Pad tạo đường kết nối; `GstBuffer` mang dữ liệu đi trên đường kết nối đó.

## 1.3 Phạm vi biểu diễn của một buffer

Không có quy tắc chung rằng:

```text
1 GstBuffer = 1 video frame
```

Ý nghĩa của một buffer phụ thuộc vào media type và contract của element.

Các trường hợp thường gặp:

- một raw-video buffer thường đại diện cho một frame hoặc field;
- một encoded-video buffer có thể chứa một access unit, một phần access unit hoặc nhiều đơn vị dữ liệu tùy element;
- một audio buffer thường chứa nhiều sample;
- parser hoặc demuxer có thể thay đổi ranh giới buffer;
- một buffer có thể chỉ mang metadata hoặc không chứa memory;
- một `GstBufferList` có thể nhóm nhiều buffer.

Ứng dụng phải xác định ý nghĩa của buffer dựa trên:

- caps;
- metadata;
- contract của element;
- hành vi của parser hoặc decoder.

Không được suy luận chỉ từ kiểu `GstBuffer`. Bài 4 sẽ giải thích payload YUY2 và MJPEG cụ thể hơn.

---

# 2. Cấu trúc dữ liệu: `GstMemory` và `GstAllocator`

## 2.1 Khái niệm `GstMemory`

> **`GstMemory` là đối tượng nhẹ có reference counting, bao bọc một vùng memory và thường được dùng để quản lý payload của `GstBuffer`.**

Một `GstMemory` mô tả:

```text
Vùng đã cấp phát
┌──────────────────────────────────────┐
│ prefix │ vùng dữ liệu hợp lệ │ suffix │
└──────────────────────────────────────┘
          ↑                   ↑
        offset           offset + size

maxsize
    = kích thước toàn vùng đã cấp phát

offset
    = vị trí bắt đầu của vùng hợp lệ

size
    = kích thước vùng hợp lệ
```

Các giá trị này có ý nghĩa:

- `maxsize` không đổi trong vòng đời của memory;
- `offset` và `size` xác định phần dữ liệu hiện được xem là hợp lệ;
- vùng trước `offset` hoặc sau `offset + size` có thể dành cho prefix, padding hoặc alignment.

`GstMemory` không nhất thiết là vùng memory được tạo trực tiếp bằng `malloc()`.

Nó có thể đại diện cho:

- memory của allocator mặc định;
- memory được wrap từ nơi khác;
- shared memory;
- DMA-BUF;
- GPU hoặc hardware memory;
- memory có mapping đặc thù.

## 2.2 Quan hệ giữa buffer, memory và cách truy cập

Một `GstBuffer` có thể chứa:

```text
0 GstMemory
1 GstMemory
nhiều GstMemory
```

Buffer không có memory vẫn là một `GstBuffer` hợp lệ.

Ví dụ:

```cpp
GstBuffer* empty_buffer =
    gst_buffer_new();

if (empty_buffer == nullptr) {
    return false;
}

const guint memory_count =
    gst_buffer_n_memory(empty_buffer);

// memory_count có thể bằng 0.
gst_buffer_unref(empty_buffer);
```

Buffer có một memory:

```text
GstBuffer
    └── GstMemory 0
```

Buffer có nhiều memory:

```text
GstBuffer
    ├── GstMemory 0
    ├── GstMemory 1
    └── GstMemory 2
```

Số lượng `GstMemory` không tự động cho biết:

- video có bao nhiêu plane;
- format là packed hay planar;
- buffer có zero-copy hay không.

Các khái niệm video plane, stride và pixel layout thuộc Bài 4.

**Borrowed và owned reference khi truy cập memory**

Hai API thường gặp có ownership khác nhau.

**Borrowed access**

```cpp
GstMemory* memory =
    gst_buffer_peek_memory(
        buffer,
        0);
```

`gst_buffer_peek_memory()` trả về:

```text
transfer none
    = borrowed pointer
```

Không được tự `gst_memory_unref()` pointer này.

Pointer chỉ còn hợp lệ khi:

- memory vẫn nằm trong buffer;
- buffer chưa thay thế, merge hoặc remove memory đó;
- vòng đời của buffer vẫn còn.

**Owned access**

```cpp
GstMemory* memory =
    gst_buffer_get_memory(
        buffer,
        0);

if (memory != nullptr) {
    /* sử dụng memory */
    gst_memory_unref(memory);
}
```

`gst_buffer_get_memory()` trả về:

```text
transfer full
    = caller sở hữu một reference
```

Caller phải `gst_memory_unref()` sau khi dùng.

Mô hình cần nhớ:

```text
peek
    = chỉ nhìn, không nhận ownership

get
    = nhận reference, phải trả reference
```

Không được quyết định ownership dựa vào tên API theo cảm tính. Phải đọc transfer annotation hoặc tài liệu chính thức của từng hàm.

## 2.3 Khái niệm `GstAllocator`

> **`GstAllocator` là đối tượng cung cấp cơ chế cấp phát và quản lý các `GstMemory`.**

Ví dụ cấp phát bằng allocator mặc định:

```cpp
GstMemory* memory =
    gst_allocator_alloc(
        nullptr,
        4096,
        nullptr);

if (memory == nullptr) {
    return false;
}

/* Caller đang sở hữu memory. */
gst_memory_unref(memory);
```

Khi đối số allocator là `nullptr`, GStreamer dùng allocator mặc định.

Allocator abstraction cho phép graph sử dụng nhiều loại memory mà không buộc mọi element phải giả định payload luôn là một vùng heap thông thường.

Một buffer có thể được tạo cùng memory bằng API tiện ích:

```cpp
GstBuffer* buffer =
    gst_buffer_new_allocate(
        nullptr,
        4096,
        nullptr);

if (buffer == nullptr) {
    return false;
}

/* Payload chưa được bảo đảm đã được khởi tạo về 0. */
gst_buffer_unref(buffer);
```

Nếu kích thước yêu cầu bằng 0, buffer có thể được tạo mà không có memory đi kèm.

---

# 3. Reference counting và ownership của buffer

## 3.1 `GstMiniObject` và reference counting

> **`GstBuffer` kế thừa `GstMiniObject`, không kế thừa `GstObject`.**

Điều này dẫn đến hai nhóm API khác nhau:

```text
GstObject
    → gst_object_ref()
    → gst_object_unref()

GstMiniObject
    → gst_mini_object_ref()
    → gst_mini_object_unref()
```

Đối với buffer, dùng API chuyên biệt:

```cpp
gst_buffer_ref(buffer);
gst_buffer_unref(buffer);
```

Không dùng:

```cpp
gst_object_unref(buffer); // Sai type.
```

Các loại phổ biến dựa trên `GstMiniObject` gồm:

- `GstBuffer`;
- `GstCaps`;
- `GstEvent`;
- `GstMessage`;
- `GstQuery`;
- `GstSample`.

**Reference counting**

Buffer mới tạo thường bắt đầu với một reference.

```text
refcount = 1
```

Khi cần giữ buffer lâu hơn phạm vi hiện tại:

```cpp
GstBuffer* retained =
    gst_buffer_ref(buffer);
```

Khi không còn dùng:

```cpp
gst_buffer_unref(retained);
```

Khi reference count về 0:

- `GstBuffer` được giải phóng hoặc trả lại pool;
- các `GstMemory` mà buffer giữ được unref;
- metadata được giải phóng theo phần triển khai tương ứng.

Mỗi reference mạnh phải có một lần release tương ứng.

```text
gst_buffer_ref()
    ↔
gst_buffer_unref()
```

Không phải mọi pointer đến buffer đều đại diện cho một reference riêng. Một pointer có thể chỉ là borrowed pointer trong phạm vi callback.

## 3.2 Full reference, borrowed reference và ownership transfer

**Owned reference**

Caller sở hữu reference và phải release:

```text
API trả transfer full
    ↓
caller dùng
    ↓
caller unref
```

Ví dụ:

```cpp
GstBuffer* copied =
    gst_buffer_copy(buffer);

if (copied != nullptr) {
    gst_buffer_unref(copied);
}
```

**Borrowed reference**

Caller chỉ mượn pointer:

```text
API/callback trả transfer none
    ↓
caller dùng trong lifetime cho phép
    ↓
không unref nếu chưa tự ref
```

Nếu cần giữ lâu hơn:

```cpp
GstBuffer* retained =
    gst_buffer_ref(borrowed_buffer);
```

**Ownership transfer**

Caller chuyển reference cho API khác:

```text
caller sở hữu
    ↓
gọi API transfer full
    ↓
caller không còn sở hữu reference đó
```

Sau ownership transfer, caller không được unref lại như thể reference vẫn còn thuộc mình.

## 3.3 Ownership khi truyền buffer qua API và callback

`gst_pad_push()` đưa buffer từ source pad sang peer pad:

```cpp
const GstFlowReturn result =
    gst_pad_push(
        source_pad,
        buffer);
```

Contract quan trọng:

> Sau khi gọi `gst_pad_push()`, caller mất reference đã truyền vào trong mọi trường hợp, dù hàm trả thành công hay thất bại.

Do đó code sau là sai:

```cpp
GstFlowReturn result =
    gst_pad_push(
        source_pad,
        buffer);

gst_buffer_unref(buffer); // Double-unref.
```

Pattern đúng:

```cpp
GstFlowReturn result =
    gst_pad_push(
        source_pad,
        buffer);

/*
 * Không unref buffer ở đây.
 * Reference đã được chuyển vào gst_pad_push().
 */
```

Nếu cần tiếp tục giữ buffer sau push:

```cpp
GstBuffer* retained =
    gst_buffer_ref(buffer);

const GstFlowReturn result =
    gst_pad_push(
        source_pad,
        buffer);

/* retained là reference riêng của caller. */
gst_buffer_unref(retained);
```

Reference được truyền vào `gst_pad_push()` và reference `retained` là hai reference khác nhau.

**Ownership trong callback**

Callback có thể nhận pointer theo nhiều contract khác nhau.

Ví dụ chain function của element thường nhận ownership của buffer được chain vào. Trong khi đó, một pad probe callback thường chỉ nhận quyền truy cập theo contract của probe API.

Không được áp dụng một quy tắc chung cho mọi callback.

Quy trình an toàn:

1. xác định API nào gọi callback;
2. đọc transfer annotation và tài liệu callback;
3. xác định pointer là owned hay borrowed;
4. chỉ `ref()` nếu cần giữ lâu hơn;
5. chỉ `unref()` reference mà code thực sự sở hữu.

---

# 4. Đọc, sửa và sao chép buffer

## 4.1 Writability của buffer và memory

> **Buffer writable nghĩa là cấu trúc `GstBuffer` có thể được sửa an toàn mà thay đổi không bị quan sát qua một reference khác.**

Đối với mini-object thông thường, buffer thường writable khi reference count bằng 1.

Kiểm tra:

```cpp
if (gst_buffer_is_writable(buffer) == FALSE) {
    // Không sửa trực tiếp metadata hoặc danh sách memory.
}
```

Khi buffer không writable, không được trực tiếp:

- thay đổi PTS, DTS hoặc duration;
- thêm, xóa hoặc thay thế memory;
- thêm hoặc xóa metadata;
- thay đổi flags.

**Buffer writable không đảm bảo mọi memory bên trong đã writable.**

```text
GstBuffer writable
    = cấu trúc buffer có thể sửa

GstMemory writable
    = payload memory có thể ghi trực tiếp
```

Một buffer có thể writable nhưng chứa memory đang shared hoặc read-only.

Khi map buffer với `GST_MAP_WRITE`:

- buffer phải writable;
- nếu buffer writable nhưng memory không writable, GStreamer có thể tạo writable copy của memory và thay memory cũ trong buffer.

**Làm buffer writable bằng `gst_buffer_make_writable()`**

`gst_buffer_make_writable()` bảo đảm caller nhận một writable buffer:

```cpp
buffer =
    gst_buffer_make_writable(buffer);

if (buffer == nullptr) {
    return false;
}
```

Contract quan trọng:

```text
buffer đầu vào
    = transfer full

buffer đầu ra
    = transfer full
```

Hàm tiêu thụ reference đầu vào và trả về:

- cùng pointer nếu buffer đã writable;
- pointer mới nếu cần copy;
- `nullptr` nếu cần copy nhưng không thể copy.

Vì vậy không được dùng pointer cũ riêng biệt:

```cpp
GstBuffer* old_buffer = buffer;

buffer =
    gst_buffer_make_writable(buffer);

/* old_buffer không còn là reference hợp lệ của caller. */
```

Pattern đúng là gán trực tiếp kết quả vào biến đang sở hữu.

`make_writable()` không đồng nghĩa với deep-copy toàn bộ payload.

Nó chủ yếu bảo đảm cấu trúc buffer có thể sửa. `GstMemory` bên trong có thể tiếp tục được chia sẻ nếu memory type cho phép. Khi payload cần ghi, bước map với `GST_MAP_WRITE` có thể làm phát sinh memory copy.

## 4.2 Map, unmap và sửa payload

`gst_buffer_map()` cho phép truy cập dữ liệu của toàn buffer qua `GstMapInfo`.

Ví dụ đọc:

```cpp
bool inspect_buffer(
    GstBuffer* buffer) {
    if (buffer == nullptr) {
        return false;
    }

    GstMapInfo map = GST_MAP_INFO_INIT;

    if (gst_buffer_map(
            buffer,
            &map,
            GST_MAP_READ) == FALSE) {
        return false;
    }

    if (map.size > 0U) {
        const guint8 first_byte =
            map.data[0];

        g_print(
            "Kích thước: %" G_GSIZE_FORMAT
            ", byte đầu: %u\n",
            map.size,
            static_cast<unsigned>(first_byte));
    }

    gst_buffer_unmap(
        buffer,
        &map);

    return true;
}
```

Quy tắc:

```text
map thành công
    ↓
chỉ dùng map.data trong phạm vi mapping
    ↓
unmap đúng buffer và GstMapInfo
```

Sau `gst_buffer_unmap()`:

- không đọc `map.data`;
- không ghi `map.data`;
- không lưu pointer đó để dùng ở thread hoặc callback khác.

`gst_buffer_map()` có thể hợp nhất nhiều memory block thành một vùng nhìn thấy qua `GstMapInfo`.

```text
map.data liên tục
    ≠
payload ban đầu chỉ có một memory block
    ≠
zero-copy
```

Việc map có thể yêu cầu merge, copy hoặc mapping dành cho phần cứng tùy memory type.

**Sửa payload an toàn**

Để sửa payload:

1. caller phải sở hữu buffer reference;
2. làm buffer writable;
3. map với `GST_MAP_WRITE`;
4. sửa trong phạm vi `map.size`;
5. unmap;
6. tiếp tục dùng pointer buffer được trả về.

Ví dụ:

```cpp
bool clear_first_bytes(
    GstBuffer*& buffer,
    gsize byte_count) {
    if (buffer == nullptr) {
        return false;
    }

    buffer =
        gst_buffer_make_writable(buffer);

    if (buffer == nullptr) {
        return false;
    }

    GstMapInfo map = GST_MAP_INFO_INIT;

    if (gst_buffer_map(
            buffer,
            &map,
            GST_MAP_WRITE) == FALSE) {
        return false;
    }

    const gsize bytes_to_clear =
        byte_count < map.size
            ? byte_count
            : map.size;

    if (bytes_to_clear > 0U) {
        std::memset(
            map.data,
            0,
            bytes_to_clear);
    }

    gst_buffer_unmap(
        buffer,
        &map);

    return true;
}
```

Caller của hàm phải truyền một owned reference. Hàm có thể thay pointer vì `make_writable()` có thể tạo buffer mới.

Không map `GST_MAP_WRITE` lên borrowed buffer mà callback không cho phép sở hữu hoặc sửa.

## 4.3 Sao chép buffer

Ba API có mục đích khác nhau.

| API | Hành vi chính |
|---|---|
| `gst_buffer_copy()` | Tạo buffer mới; memory thường được chia sẻ bằng reference nếu memory type cho phép |
| `gst_buffer_copy_deep()` | Tạo buffer mới và buộc payload được sao chép sang memory mới |
| `gst_buffer_copy_into()` | Chọn flags, offset và size cần sao chép vào buffer đích |

**`gst_buffer_copy()`**

```cpp
GstBuffer* copy =
    gst_buffer_copy(source);

if (copy == nullptr) {
    return false;
}

gst_buffer_unref(copy);
```

Đây không phải lời hứa rằng payload luôn được copy byte-for-byte sang memory mới.

Thông thường:

```text
cấu trúc buffer mới
    +
memory được ref/chia sẻ khi có thể
```

Một số memory type có thể buộc phải copy.

**`gst_buffer_copy_deep()`**

```cpp
GstBuffer* deep_copy =
    gst_buffer_copy_deep(source);
```

API này yêu cầu tạo bản sao mới của payload.

Deep copy có chi phí:

- cấp phát memory;
- copy dữ liệu;
- tăng băng thông memory;
- tăng latency và chi phí CPU.

Không nên deep-copy mọi frame nếu không có yêu cầu ownership hoặc biến đổi payload rõ ràng.

**`gst_buffer_copy_into()`**

API này cho phép chọn:

- flags;
- timestamps;
- metadata;
- memory;
- vùng dữ liệu.

Ví dụ chỉ copy metadata cơ bản:

```cpp
GstBuffer* destination =
    gst_buffer_new();

if (destination == nullptr) {
    return false;
}

const gboolean copied =
    gst_buffer_copy_into(
        destination,
        source,
        GST_BUFFER_COPY_METADATA,
        0,
        static_cast<gsize>(-1));

if (copied == FALSE) {
    gst_buffer_unref(destination);
    return false;
}

gst_buffer_unref(destination);
```

Metadata tùy biến không được mặc định bảo đảm luôn được truyền qua mọi thao tác copy hoặc transform. Việc giữ metadata phụ thuộc:

- copy flags;
- phần triển khai của meta;
- hàm chuyển đổi của meta;
- vùng dữ liệu được copy;
- hành vi của element.

---

# 5. Timestamp, flags và metadata

## 5.1 Thông tin thời gian, vị trí và flags

`GstBuffer` mang các trường thời gian và vị trí.

**PTS**

```text
PTS
    = thời điểm nội dung nên được trình bày
```

Ví dụ sink có thể dùng PTS để quyết định khi nào hiển thị một frame.

**DTS**

```text
DTS
    = thời điểm nội dung cần được giải mã hoặc xử lý theo thứ tự decode
```

Với raw video, DTS có thể không cần thiết. Với luồng đã mã hóa có reordering, PTS và DTS có thể khác nhau.

**Duration**

```text
duration
    = khoảng thời gian media mà buffer đại diện
```

Các trường thời gian có thể mang:

```text
GST_CLOCK_TIME_NONE
```

khi giá trị không biết hoặc không có ý nghĩa.

**Offset và offset_end**

Offset có ý nghĩa theo media type:

- video: thường có thể biểu diễn số thứ tự frame;
- audio: sample offset;
- file hoặc compressed data: có thể là độ lệch byte;
- loại media khác có thể dùng ý nghĩa khác.

Không được giả định offset luôn là độ lệch byte.

Clock, segment, running time và synchronization được trình bày ở Bài 7.

**Buffer flags**

Buffer flags bổ sung thông tin về payload.

Một số flag thường gặp:

| Flag | Ý nghĩa tổng quát |
|---|---|
| `GST_BUFFER_FLAG_DISCONT` | Có discontinuity trước buffer này |
| `GST_BUFFER_FLAG_CORRUPTED` | Dữ liệu được đánh dấu có lỗi |
| `GST_BUFFER_FLAG_DELTA_UNIT` | Dữ liệu phụ thuộc dữ liệu trước đó để decode |
| `GST_BUFFER_FLAG_HEADER` | Dữ liệu header hoặc dữ liệu khởi tạo |

Flags phải được diễn giải theo loại media và contract của element.

Ví dụ:

```text
DELTA_UNIT
    thường liên quan compressed stream

không dùng để suy luận layout raw video
```

Flag không thay thế Bus error. Một buffer được đánh dấu `CORRUPTED` có thể vẫn tiếp tục đi qua graph tùy policy của element, trong khi lỗi pipeline nghiêm trọng được báo bằng flow return hoặc Bus message.

## 5.2 `GstMeta` và `GstVideoMeta`

> **`GstMeta` là cơ chế gắn metadata mở rộng vào `GstBuffer`.**

Metadata có thể mô tả:

- bố trí video;
- vùng crop;
- region of interest;
- reference đến parent buffer;
- thông tin phần cứng;
- dữ liệu tùy biến.

Phân biệt ba đối tượng:

```text
GstCaps
    = định dạng của stream

GstBuffer
    = một đơn vị dữ liệu trong stream

GstMeta
    = thông tin bổ sung gắn với buffer cụ thể
```

Caps không nằm trong từng buffer như payload. Caps được thương lượng và truyền bằng cơ chế riêng trên pad.

Một phần triển khai của meta có thể cung cấp hàm chuyển đổi để quyết định meta được sao chép hoặc biến đổi như thế nào khi buffer được copy/transform.

Không được giả định:

```text
element tạo buffer đầu ra mới
    → mọi meta đầu vào tự động còn nguyên
```

**`GstVideoMeta`**

`GstVideoMeta` là metadata dành cho video buffer.

Nó có thể mô tả:

- video format;
- width và height;
- số plane;
- offset từng plane;
- stride từng plane;
- alignment và padding liên quan.

`GstVideoMeta` không thay thế caps.

```text
Caps
    = định dạng stream đã thương lượng

GstVideoMeta
    = layout cụ thể của một video buffer
```

Bài 4 sẽ giải thích video format, plane và stride đầy đủ hơn.

---

# 6. Tái sử dụng buffer và allocation

## 6.1 Khái niệm và trạng thái của `GstBufferPool`

> **`GstBufferPool` là đối tượng quản lý một tập buffer có các thuộc tính tương đồng để cấp phát và tái sử dụng hiệu quả.**

Nếu mỗi frame đều thực hiện:

```text
allocate
→ use
→ free
```

pipeline có thể chịu chi phí:

- allocation liên tục;
- memory fragmentation;
- cache pressure;
- synchronization;
- thiết lập tài nguyên phần cứng.

Buffer pool cho phép:

```text
acquire buffer
    ↓
sử dụng
    ↓
reference cuối cùng được release
    ↓
buffer quay lại pool
    ↓
tái sử dụng
```

Khi buffer được lấy từ pool, GStreamer thường tự trả buffer về pool khi reference cuối cùng biến mất.

**Active, inactive và flushing**

Buffer pool có hai trạng thái chính.

**Inactive**

- có thể cấu hình;
- chưa cấp buffer cho dataflow;
- thay đổi caps, size, allocator, alignment hoặc options.

**Active**

- có thể acquire buffer;
- buffer được tái sử dụng theo cấu hình đang hoạt động;
- thường không thay đổi cấu hình tùy ý.

Khi pool bị deactivate hoặc flushing:

- acquire mới có thể trả `GST_FLOW_FLUSHING`;
- các buffer đang outstanding vẫn có thể còn reference;
- chúng được xử lý khi reference cuối cùng được release.

State và flushing trong toàn pipeline thuộc Bài 7.

## 6.2 Giữ buffer, backpressure và allocation

Giả sử pool có số buffer hữu hạn:

```text
Pool
    ├── Buffer A
    ├── Buffer B
    └── Buffer C
```

Nếu downstream giữ thêm reference:

```text
A bị giữ
B bị giữ
C bị giữ
    ↓
upstream không có buffer trống
```

Hệ quả có thể là:

- `acquire_buffer()` chờ;
- dataflow bị backpressure;
- frame bị drop theo policy của element;
- pipeline tăng latency;
- source hoặc driver thiếu capture buffer.

Giữ buffer lâu hơn không luôn là lỗi. Nhưng việc giữ buffer phải có giới hạn và chính sách rõ ràng.

Ví dụ cần cân nhắc:

- hàng đợi của ứng dụng;
- nhánh xử lý chậm;
- xử lý suy luận bất đồng bộ;
- ghi file hoặc network chậm;
- ứng dụng lưu frame lịch sử.

**Allocation query ở mức tổng quan**

Sau khi format đã được thương lượng, các pad còn phải thống nhất cách cấp phát buffer.

Luồng tổng quát:

```text
Upstream src pad tạo ALLOCATION query
    ↓
Downstream đề xuất
    ├── buffer pool
    ├── allocator
    ├── allocation parameters
    └── metadata requirements
    ↓
Upstream chọn cách allocation phù hợp
```

Downstream có thể đề xuất nhưng upstream không bắt buộc phải dùng mọi đề xuất nếu có chiến lược hợp lệ khác.

Phân biệt:

```text
Caps negotiation
    = dữ liệu có định dạng gì

Allocation negotiation
    = buffer và memory được cấp phát thế nào
```

Bài 5 tập trung vào caps negotiation. Bài 6 liên hệ allocation với `v4l2src` và V4L2 I/O mode.

---

# 7. Quan hệ với V4L2 và CamStream Control

## 7.1 V4L2 buffer và cầu nối `v4l2src`

> **V4L2 capture buffer và `GstBuffer` là hai đối tượng thuộc hai API và có lifecycle khác nhau.**

V4L2 buffer được quản lý bằng V4L2 queue và ioctl.

`GstBuffer` được quản lý bằng GStreamer reference counting, pad dataflow và buffer pool.

```text
V4L2 buffer
    = contract capture video giữa kernel và userspace

GstBuffer
    = GStreamer đối tượng dữ liệu media
```

Không được coi hai đối tượng là cùng một type chỉ vì chúng có thể cùng tham chiếu đến dữ liệu của một frame.

**`v4l2src` là cầu nối lifecycle**

Mô hình tổng quát:

```text
Driver hoàn thành capture buffer
    ↓
v4l2src nhận dữ liệu
    ↓
v4l2src tạo GstBuffer hoặc gắn memory gắn với tài nguyên capture
    ↓
GstBuffer đi xuống graph
    ↓
tài nguyên được copy, giữ hoặc trả lại theo chiến lược thực tế
```

Có nhiều khả năng implementation:

```text
Đường có sao chép dữ liệu
    V4L2 payload
        ↓ copy
    memory do GStreamer sở hữu

Đường dùng trực tiếp tài nguyên capture
    GstBuffer giữ reference đến tài nguyên capture
```

Nếu `GstBuffer` giữ tài nguyên capture-backed:

```text
downstream còn giữ GstBuffer
    ↓
tài nguyên có thể chưa được trả lại hàng đợi capture
```

Nếu payload đã được copy sang memory khác:

```text
V4L2 capture buffer
    có thể được requeue sớm hơn
```

Do đó không được phát biểu chung rằng:

```text
GstBuffer refcount luôn trực tiếp điều khiển V4L2 QBUF
```

Mối quan hệ đó phụ thuộc đường đi của memory và phần triển khai của `v4l2src`.

QBUF, DQBUF, MMAP, USERPTR, DMABUF và I/O mode thuộc Bài 6.

## 7.2 Quyền truy cập frame trong CamStream hiện tại

Hai graph hiện tại:

```text
YUY2:
v4l2src
→ capsfilter
→ fakesink
```

```text
MJPEG:
v4l2src
→ capsfilter
→ jpegdec
→ videoconvert
→ fakesink
```

Tầng ứng dụng của CamStream hiện:

- xây và điều khiển pipeline;
- cấu hình source, capsfilter và sink;
- xử lý state cùng Bus;
- không dùng `appsink`;
- không nhận `GstSample`;
- không map payload frame;
- không giữ `GstBuffer` trong hàng đợi của ứng dụng.

`fakesink` tiêu thụ dữ liệu trong graph. Điều đó chứng minh dataflow chức năng trong phạm vi test, nhưng không cho application quyền truy cập trực tiếp vào frame.

Khi dự án cần xử lý frame trong application, một hướng phổ biến là:

```text
pipeline
    ↓
appsink
    ↓
GstSample
    ↓
GstBuffer
```

Ownership của `GstSample`, thread thực thi callback và chính sách hàng đợi sẽ thuộc bài tích hợp ứng dụng.

## 7.3 Ranh giới của kết luận zero-copy

Chưa có bằng chứng để tuyên bố zero-copy.

Pipeline chạy thành công không chứng minh:

```text
camera
→ driver
→ v4l2src
→ decoder/converter
→ sink
```

không phát sinh copy.

Copy có thể xảy ra tại:

- V4L2-to-GStreamer bridge;
- memory mapping;
- decoder output;
- `videoconvert`;
- allocator không tương thích;
- buffer merge;
- writable copy;
- yêu cầu của element phía sau.

Các graph hiện tại không cấu hình hoặc kiểm tra:

- DMABUF export/import;
- caps features của hardware memory;
- allocator/pool negotiation cụ thể;
- tracer hoặc cơ chế đo và theo dõi thao tác copy;
- đường zero-copy phụ thuộc phần cứng.

Kết luận đúng:

```text
CamStream đã chứng minh luồng dữ liệu chức năng
    ≠
CamStream đã chứng minh zero-copy
```

---

# 8. Lỗi thường gặp, ví dụ C++ và tổng kết

## 8.1 Lỗi ownership và mapping thường gặp

**Unref borrowed pointer**

```cpp
GstMemory* memory =
    gst_buffer_peek_memory(
        buffer,
        0);

gst_memory_unref(memory); // Sai.
```

**Quên unref full reference**

```cpp
GstMemory* memory =
    gst_buffer_get_memory(
        buffer,
        0);

/* quên gst_memory_unref(memory); */
```

**Double-unref sau push**

```cpp
gst_pad_push(
    source_pad,
    buffer);

gst_buffer_unref(buffer); // Sai.
```

**Dùng pointer cũ sau `make_writable()`**

```cpp
GstBuffer* old_buffer = buffer;

buffer =
    gst_buffer_make_writable(buffer);

/* old_buffer không còn là owned reference. */
```

**Map write khi buffer chưa writable**

```cpp
gst_buffer_map(
    borrowed_buffer,
    &map,
    GST_MAP_WRITE); // Sai contract nếu không sở hữu/writable.
```

**Quên unmap trên nhánh lỗi**

```cpp
gst_buffer_map(
    buffer,
    &map,
    GST_MAP_READ);

if (invalid_data) {
    return false; // Quên unmap.
}
```

**Giữ `map.data` sau unmap**

```cpp
const guint8* saved = map.data;

gst_buffer_unmap(
    buffer,
    &map);

/* saved không còn hợp lệ để sử dụng. */
```

**Deep-copy mọi frame**

Deep copy có thể đúng khi cần ownership độc lập, nhưng dùng mặc định cho mọi frame có thể gây chi phí lớn.

**Giữ buffer không giới hạn**

Queue ứng dụng không giới hạn có thể giữ reference làm cạn pool và tăng latency.

## 8.2 Ví dụ C++ quản lý lifetime bằng RAII

RAII giúp bảo đảm unmap trên mọi đường return.

```cpp
#include <gst/gst.h>

#include <cstddef>

class BufferMap final {
  public:
    BufferMap(
        GstBuffer* buffer,
        GstMapFlags flags)
        : buffer(buffer),
          access_flags(flags) {
        if (buffer != nullptr) {
            mapped =
                gst_buffer_map(
                    buffer,
                    &map,
                    flags) != FALSE;
        }
    }

    ~BufferMap() noexcept {
        if (mapped) {
            gst_buffer_unmap(
                buffer,
                &map);
        }
    }

    BufferMap(const BufferMap&) = delete;
    BufferMap& operator=(const BufferMap&) = delete;
    BufferMap(BufferMap&&) = delete;
    BufferMap& operator=(BufferMap&&) = delete;

    [[nodiscard]]
    bool valid() const noexcept {
        return mapped;
    }

    [[nodiscard]]
    const guint8* data() const noexcept {
        return mapped
            ? map.data
            : nullptr;
    }

    [[nodiscard]]
    guint8* writable_data() noexcept {
        const bool writable =
            (access_flags & GST_MAP_WRITE) != 0;

        return mapped && writable
            ? map.data
            : nullptr;
    }

    [[nodiscard]]
    gsize size() const noexcept {
        return mapped
            ? map.size
            : 0U;
    }

  private:
    GstBuffer* buffer = nullptr;
    GstMapInfo map = GST_MAP_INFO_INIT;
    GstMapFlags access_flags = GST_MAP_READ;
    bool mapped = false;
};
```

Ví dụ đọc:

```cpp
bool inspect_payload(
    GstBuffer* buffer) {
    BufferMap mapped(
        buffer,
        GST_MAP_READ);

    if (!mapped.valid()) {
        return false;
    }

    if (mapped.size() == 0U) {
        return true;
    }

    g_print(
        "Byte đầu: %u\n",
        static_cast<unsigned>(
            mapped.data()[0]));

    return true;
}
```

Class này chỉ quản lý lifetime của mapping. Nó không sở hữu `GstBuffer` và không tự làm buffer writable.

Code gọi vẫn phải bảo đảm:

- buffer tồn tại lâu hơn `BufferMap`;
- map flags phù hợp;
- buffer đã writable trước khi yêu cầu `GST_MAP_WRITE`.

**RAII cho buffer reference**

Một wrapper nhỏ có thể quản lý full reference:

```cpp
#include <gst/gst.h>

#include <utility>

class BufferRef final {
  public:
    BufferRef() noexcept = default;

    explicit BufferRef(
        GstBuffer* owned_buffer) noexcept
        : buffer(owned_buffer) {}

    ~BufferRef() noexcept {
        reset();
    }

    BufferRef(const BufferRef&) = delete;
    BufferRef& operator=(const BufferRef&) = delete;

    BufferRef(BufferRef&& other) noexcept
        : buffer(
              std::exchange(
                  other.buffer,
                  nullptr)) {}

    BufferRef& operator=(
        BufferRef&& other) noexcept {
        if (this != &other) {
            reset();

            buffer =
                std::exchange(
                    other.buffer,
                    nullptr);
        }

        return *this;
    }

    [[nodiscard]]
    GstBuffer* get() const noexcept {
        return buffer;
    }

    [[nodiscard]]
    GstBuffer* release() noexcept {
        return std::exchange(
            buffer,
            nullptr);
    }

    void reset(
        GstBuffer* owned_buffer = nullptr) noexcept {
        if (owned_buffer == buffer) {
            return;
        }

        if (buffer != nullptr) {
            gst_buffer_unref(buffer);
        }

        buffer = owned_buffer;
    }

  private:
    GstBuffer* buffer = nullptr;
};
```

Cách tạo từ borrowed pointer:

```cpp
BufferRef retained(
    gst_buffer_ref(
        borrowed_buffer));
```

Cách nhận full reference:

```cpp
BufferRef copied(
    gst_buffer_copy(
        source_buffer));
```

Cách chuyển ownership vào `gst_pad_push()`:

```cpp
GstBuffer* buffer_to_push =
    owned_buffer.release();

if (buffer_to_push == nullptr) {
    return GST_FLOW_ERROR;
}

return gst_pad_push(
    source_pad,
    buffer_to_push);
```

Sau `release()`, wrapper không còn ownership. `gst_pad_push()` nhận reference đó trong mọi kết quả trả về.

Không dùng `std::shared_ptr<GstBuffer>` mặc định nếu chưa định nghĩa deleter đúng và ownership semantics rõ ràng. `GstBuffer` đã có reference counting riêng; wrapper nên phản ánh trực tiếp contract đó.

## 8.3 Kiến thức cần nhớ và nguồn tham khảo

Mô hình tổng thể:

```text
GstElement
    ↓ truyền qua GstPad
GstBuffer
    ├── GstMemory
    ├── timestamp / flags
    └── GstMeta
          ↓
reference cuối cùng được release
          ↓
free hoặc quay lại GstBufferPool
```

Các ý trọng tâm:

1. `GstBuffer` là đơn vị truyền dữ liệu cơ bản, không chỉ là một mảng byte.
2. Ý nghĩa của một buffer phụ thuộc media type và contract của element.
3. Buffer có thể chứa 0, 1 hoặc nhiều `GstMemory`; số memory không tự động bằng số video plane.
4. `GstMemory` mô tả vùng memory bằng `maxsize`, `offset` và `size`.
5. `GstAllocator` cung cấp cơ chế tạo và quản lý `GstMemory`.
6. `GstBuffer` là `GstMiniObject`, không phải `GstObject`.
7. Full reference phải unref; borrowed reference chỉ được unref sau khi code đã tự lấy thêm reference.
8. `gst_pad_push()` nhận ownership của reference buffer được truyền vào trong mọi kết quả trả về.
9. Writability của buffer và writability của memory là hai contract khác nhau.
10. `gst_buffer_make_writable()` tiêu thụ reference đầu vào và không mặc định deep-copy payload.
11. Map và unmap phải đi theo cặp; `map.data` không còn hợp lệ sau unmap.
12. `gst_buffer_copy()` thường chia sẻ memory khi có thể; `gst_buffer_copy_deep()` sao chép payload.
13. Timestamp, flags và metadata phải được diễn giải theo media type và contract của element.
14. Metadata không được mặc định bảo đảm luôn truyền qua mọi phép copy hoặc transform.
15. Buffer pool tái sử dụng buffer; reference còn được giữ có thể trì hoãn việc trả buffer về pool.
16. Giữ quá nhiều buffer có thể gây backpressure, tăng latency hoặc thiếu buffer.
17. V4L2 buffer và `GstBuffer` thuộc hai API và có vòng đời khác nhau.
18. Vòng đời của `GstBuffer` chỉ có thể trì hoãn việc trả tài nguyên V4L2 khi buffer giữ memory gắn với tài nguyên capture.
19. CamStream hiện không map frame hoặc giữ `GstBuffer` ở tầng ứng dụng.
20. Pipeline chạy đúng chức năng không phải bằng chứng zero-copy.

**Nguồn tham khảo**

**Tài liệu GStreamer chính thức**

- [G1 — `GstBuffer`](https://gstreamer.freedesktop.org/documentation/gstreamer/gstbuffer.html)
- [G2 — Buffer design](https://gstreamer.freedesktop.org/documentation/additional/design/buffer.html)
- [G3 — Buffers and Events](https://gstreamer.freedesktop.org/documentation/application-development/basics/data.html)
- [G4 — `GstMemory`](https://gstreamer.freedesktop.org/documentation/gstreamer/gstmemory.html)
- [G5 — `GstAllocator`](https://gstreamer.freedesktop.org/documentation/gstreamer/gstallocator.html)
- [G6 — `GstMiniObject`](https://gstreamer.freedesktop.org/documentation/gstreamer/gstminiobject.html)
- [G7 — Mini-object design](https://gstreamer.freedesktop.org/documentation/additional/design/miniobject.html)
- [G8 — `GstPad`](https://gstreamer.freedesktop.org/documentation/gstreamer/gstpad.html)
- [G9 — `GstMeta`](https://gstreamer.freedesktop.org/documentation/gstreamer/gstmeta.html)
- [G10 — `GstVideoMeta`](https://gstreamer.freedesktop.org/documentation/video/gstvideometa.html)
- [G11 — `GstBufferPool`](https://gstreamer.freedesktop.org/documentation/gstreamer/gstbufferpool.html)
- [G12 — Buffer-pool design](https://gstreamer.freedesktop.org/documentation/additional/design/bufferpool.html)
- [G13 — Memory allocation](https://gstreamer.freedesktop.org/documentation/plugin-development/advanced/allocation.html)
- [G14 — DMA buffers](https://gstreamer.freedesktop.org/documentation/additional/design/dmabuf.html)
- [G15 — Ownership conventions](https://gstreamer.freedesktop.org/documentation/additional/design/standards.html)

**CamStream Control**

- [P1 — `GstreamerPipeline` implementation](https://github.com/tungnhs2014/camstream-control/blob/74a6a2045198ecc7c91fe32d48dbe42b52e87faa/libs/camstream-gstreamer/src/gstreamer_pipeline.cpp)
- [P2 — `GstreamerPipeline` public interface](https://github.com/tungnhs2014/camstream-control/blob/74a6a2045198ecc7c91fe32d48dbe42b52e87faa/libs/camstream-gstreamer/include/camstream/gstreamer_pipeline.hpp)
- [P3 — Stage 7 GStreamer integration validation](https://github.com/tungnhs2014/camstream-control/blob/74a6a2045198ecc7c91fe32d48dbe42b52e87faa/docs/validation/stage-07-gstreamer-integration.md)
- [P4 — Stage 8.2 service integration validation](https://github.com/tungnhs2014/camstream-control/blob/74a6a2045198ecc7c91fe32d48dbe42b52e87faa/docs/validation/stage-08.2-gstreamer-service-integration.md)
