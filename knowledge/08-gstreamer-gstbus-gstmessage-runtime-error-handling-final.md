# Bài 8 — `GstBus`, `GstMessage` và xử lý runtime

> **Lộ trình:** [Bảng đối chiếu lý thuyết ↔ lab](README.md) · [Lab 08](../practice/lesson-08/README.md). Phần CamStream Control là case study của dự án ngoài repo lab; chỉ tiêu chí PASS của lab mới là bằng chứng kiểm chứng tại repo này.

> **Dự án:** CamStream Control
> **Đối tượng:** Người mới học Embedded Linux và GStreamer
> **Phạm vi:** Bài này giải thích cách các element báo EOS, lỗi, cảnh báo và thay đổi trạng thái lên application qua `GstBus`; đồng thời trình bày ownership, các mô hình consume Bus, timeout và clean shutdown trong CamStream Control.

---

# 1. Vai trò của Bus trong GStreamer application

## 1.1 Mục tiêu và phạm vi bài học

Bài 7 đã giải thích cách application yêu cầu pipeline chuyển state và xác nhận pipeline đạt `PLAYING` hoặc `NULL`.

Nhưng một pipeline có thể bắt đầu thành công rồi gặp sự kiện sau đó:

```text
camera bị rút
USB reset
driver dừng giao frame
decoder gặp dữ liệu lỗi
source hữu hạn hoàn tất
pipeline đổi state
clock không còn sử dụng được
```

Lời gọi `start()` đã kết thúc nên không thể trả kết quả lần thứ hai.

GStreamer giải quyết bằng đường thông báo runtime:

```text
Element hoặc Pipeline
    ↓ post GstMessage
GstBus
    ↓
Application
```

Sau bài này, người học cần:

- hiểu `GstBus` và `GstMessage`;
- phân biệt Buffer, Event, Query và Message;
- hiểu message được post từ streaming thread nhưng có thể được xử lý ở application thread;
- hiểu `GstMessage` là `GstMiniObject`;
- hiểu `GstBus` là `GstObject`;
- hiểu message type, source, timestamp và sequence number;
- phân biệt EOS event với EOS message;
- hiểu Bin/Pipeline xử lý message của child như thế nào;
- lấy và release Bus đúng ownership;
- hiểu polling, Bus watch, poll-fd integration và sync handler;
- không dùng nhiều Bus consumer trên cùng pipeline mà không có policy;
- hiểu filtered pop sẽ loại bỏ message không khớp mask;
- xử lý đúng `EOS`, `ERROR`, `WARNING`, `STATE_CHANGED`;
- hiểu vai trò của `ASYNC_DONE`, `STREAM_START`, `BUFFERING`, `CLOCK_LOST` và `LATENCY`;
- parse và giải phóng `GError` cùng debug string;
- phân biệt terminal outcome với non-terminal notification;
- hiểu Bus flushing khi pipeline về `NULL`;
- hiểu finite `wait()` và continuous service polling của CamStream;
- hiểu absolute deadline và bounded stall allowance;
- hiểu clean shutdown sau EOS, ERROR hoặc timeout;
- debug generic runtime error theo source và từng tầng.

Bài này không đi sâu vào:

| Nội dung | Bài mở rộng |
|---|---:|
| Clock selection và running time | Bài 7 |
| Latency calculation chi tiết | Bài nâng cao |
| QoS và frame-drop policy | Bài nâng cao |
| `tee`, `queue` và threading topology | Bài nâng cao |
| `appsink`, `appsrc` và application frame processing | Bài tích hợp ứng dụng |
| Automatic device-recovery state machine | Bài service/recovery |

## 1.2 Vì sao không chỉ dùng return value?

Xét workflow:

```cpp
build();
start();
```

`start()` có thể xác nhận pipeline đạt `PLAYING`.

Ba giây sau:

```text
v4l2src streaming thread
    ↓
USB hoặc driver error
    ↓
dataflow dừng
```

Không còn stack frame của `start()` để trả lỗi.

Element post:

```text
GST_MESSAGE_ERROR
```

Application consume message từ Bus và quyết định:

- log;
- đánh dấu run thất bại;
- yêu cầu stop;
- đưa pipeline về `NULL`;
- retry hoặc exit theo policy.

Bus vì vậy là application-control boundary, không phải media payload path.

## 1.3 Bốn đường giao tiếp cần phân biệt

```text
GstBuffer
    = media data

GstEvent
    = điều khiển timeline/dataflow trong graph

GstQuery
    = hỏi và trả lời thông tin qua pads

GstMessage
    = thông báo runtime từ graph lên application
```

| Object | Mục đích | Hướng đi thường gặp |
|---|---|---|
| `GstBuffer` | Mang payload | Downstream |
| `GstEvent` | CAPS, SEGMENT, EOS, FLUSH... | Upstream hoặc downstream |
| `GstQuery` | Caps, allocation, latency, position... | Theo query type |
| `GstMessage` | EOS, ERROR, WARNING, state... | Element → Bus → application |

Không được thay thế lẫn nhau:

```text
EOS event
    ≠
EOS message

CAPS event
    ≠
STATE_CHANGED message

LATENCY query
    ≠
LATENCY message
```

---

# 2. `GstMessage`: cấu trúc và ownership

## 2.1 Khái niệm `GstMessage`

> **`GstMessage` là lightweight reference-counted object mà các object trong pipeline dùng để báo một runtime condition cho application qua `GstBus`.**

`GstMessage` là subclass của:

```text
GstMiniObject
```

Quản lý lifetime bằng:

```cpp
gst_message_ref(message);
gst_message_unref(message);
```

Không dùng:

```cpp
gst_object_unref(message); // Sai type.
```

Message có thể mang một `GstStructure` chứa dữ liệu riêng của message type.

## 2.2 Các trường chung

Mọi message có các thông tin cơ bản:

```text
type
source
timestamp
sequence number
message-specific structure
```

**Type**

```cpp
GST_MESSAGE_TYPE(message)
GST_MESSAGE_TYPE_NAME(message)
```

Type quyết định parser phù hợp:

```text
ERROR
    → gst_message_parse_error()

WARNING
    → gst_message_parse_warning()

STATE_CHANGED
    → gst_message_parse_state_changed()
```

**Source**

```cpp
GST_MESSAGE_SRC(message)
```

Source là object đã post message.

Ví dụ:

```text
camera-source
jpeg-decoder
video-converter
diagnostic-sink
camstream-gstreamer-pipeline
```

Source pointer thuộc message. Không unref pointer trả bởi `GST_MESSAGE_SRC()`.

**Timestamp**

Message có timestamp do GStreamer ghi nhận.

Không nhầm message timestamp với:

- buffer PTS;
- buffer DTS;
- running time của frame;
- wall-clock time của log.

**Sequence number**

Sequence number giúp liên hệ event và message thuộc cùng một operation.

Ví dụ:

```text
application gửi EOS event
    ↓ cùng seqnum
sink tạo EOS message
```

Không dùng sequence number như globally unique database identifier.

## 2.3 Post message và ownership transfer

Element thường dùng:

```cpp
gst_element_post_message(
    element,
    message);
```

Bus API trực tiếp:

```cpp
gst_bus_post(
    bus,
    message);
```

`gst_bus_post()` nhận ownership của message.

Sau call:

```text
code gọi không được tiếp tục unref
reference đã transfer
```

Nếu Bus đang flushing, message có thể bị drop và release theo Bus contract.

## 2.4 Pop message và full reference

Các API như:

```cpp
gst_bus_pop()
gst_bus_timed_pop()
gst_bus_pop_filtered()
gst_bus_timed_pop_filtered()
```

trả:

```text
full reference
```

Code gọi phải:

```cpp
gst_message_unref(message);
```

Ví dụ:

```cpp
GstMessage* message =
    gst_bus_pop(bus);

if (message != nullptr) {
    process_message(message);
    gst_message_unref(message);
}
```

## 2.5 RAII cho `GstMessage`

Ví dụ C++17:

```cpp
#include <gst/gst.h>

#include <memory>

using MessagePtr =
    std::unique_ptr<
        GstMessage,
        decltype(&gst_message_unref)>;

MessagePtr pop_message(
    GstBus* bus) {
    return MessagePtr{
        gst_bus_pop(bus),
        gst_message_unref,
    };
}
```

Sử dụng:

```cpp
MessagePtr message =
    pop_message(bus);

if (message != nullptr) {
    process_message(
        message.get());
}
```

RAII giúp mọi return path chỉ release message đúng một lần.

---

# 3. `GstBus`: hàng đợi giữa streaming threads và application

## 3.1 Khái niệm `GstBus`

> **`GstBus` là object chịu trách nhiệm chuyển `GstMessage` theo thứ tự FIFO từ các thread của pipeline tới application.**

Pipeline có top-level Bus.

Application thường lấy Bus bằng:

```cpp
GstBus* bus =
    gst_element_get_bus(
        pipeline);
```

`gst_element_get_bus()` trả full reference.

Cleanup:

```cpp
gst_object_unref(bus);
```

`GstBus` là `GstObject`, khác với `GstMessage`.

```text
GstBus
    → gst_object_unref()

GstMessage
    → gst_message_unref()
```

## 3.2 Vì sao Bus cần thread marshalling?

GStreamer có thể chạy nhiều thread:

```text
source streaming thread
decoder task
converter task
sink thread
state-change context
application thread
```

Nếu mọi runtime error callback chạy trực tiếp trong thread đã phát hiện lỗi:

- application phải tự xử lý nhiều thread context;
- GUI hoặc control state dễ race;
- handler có thể block streaming thread;
- cleanup có thể deadlock.

Bus hỗ trợ đưa message vào queue để application consume theo một context được chọn.

Mental model:

```text
Streaming thread
    ↓ post nhanh
GstBus queue
    ↓
Application event loop hoặc polling loop
```

## 3.3 Bin và Pipeline forward message

Child element post message lên parent Bus.

`GstBin` thường forward message của child lên Bus bên ngoài.

Một số message được Bin xử lý đặc biệt.

Quan trọng nhất là EOS:

```text
một sink nhận EOS
    ↓
sink post EOS condition
    ↓
Pipeline/Bin chờ các sink liên quan hoàn tất
    ↓
application nhận pipeline EOS
```

Vì vậy top-level EOS nghĩa:

```text
pipeline đã đạt end-of-stream ở các sink cần thiết
```

Không chỉ nghĩa:

```text
source đã tạo EOS event
```

## 3.4 Bus queue không tự tiêu thụ message

Nếu application không:

- pop message;
- gắn Bus watch;
- drain Bus;

message có thể tích lũy trong Bus.

Điều này gây:

- memory tăng;
- reference tới source object còn được giữ;
- runtime notification không được xử lý.

Application phải chọn đúng một mô hình Bus consumption rõ ràng.

## 3.5 Bus flushing

`GstPipeline` mặc định auto-flush Bus khi đi:

```text
READY → NULL
```

Khi flushing:

```text
message đang chờ có thể bị drop
message mới có thể bị drop
```

Mục tiêu:

- phá circular reference do queued message giữ source object;
- cho pipeline được destroy sạch.

Khi pipeline đi:

```text
NULL → READY
```

Bus được đặt về non-flushing.

Hệ quả thiết kế:

```text
message cần log
    nên được consume trước cleanup nếu policy yêu cầu

stop về NULL
    có thể flush message còn lại
```

Không dựa vào việc message vẫn còn sau `stop()`.

---

# 4. Các mô hình consume Bus

## 4.1 Tổng quan

Có bốn mô hình thường gặp:

| Mô hình | API chính | Phù hợp |
|---|---|---|
| Blocking polling | `gst_bus_timed_pop*()` | CLI, finite diagnostic |
| GLib Bus watch | `gst_bus_add_watch()` | GUI/service dùng `GMainLoop` |
| External poll-fd | `gst_bus_get_pollfd()` + `poll()` | Service có event loop fd riêng |
| Sync handler | `gst_bus_set_sync_handler()` | Trường hợp đặc biệt cần same-thread reaction |

Một pipeline phải có Bus-consumption ownership rõ ràng.

Không chạy đồng thời:

```text
wait() đang pop message
+
service đang drain cùng Bus
```

vì mỗi message chỉ được một consumer lấy ra.

## 4.2 Blocking polling

Ví dụ:

```cpp
GstMessage* message =
    gst_bus_timed_pop(
        bus,
        5 * GST_SECOND);
```

Phù hợp khi:

- application đơn giản;
- một thread chủ động chờ terminal condition;
- không cần GUI main loop;
- finite operation có timeout rõ ràng.

Nhược điểm:

- thread bị block;
- không tích hợp trực tiếp nhiều fd;
- cần policy timeout;
- phải tự xử lý ownership.

## 4.3 `gst_bus_timed_pop_filtered()`

API:

```cpp
GstMessage* message =
    gst_bus_timed_pop_filtered(
        bus,
        timeout,
        message_types);
```

Nếu:

```text
timeout = 0
```

hành vi như nonblocking filtered pop.

Nếu:

```text
timeout = GST_CLOCK_TIME_NONE
```

có thể chờ vô hạn tới khi có matching message.

Điểm đặc biệt:

> **Filtered pop sẽ discard các message không khớp mask trong quá trình tìm matching message.**

Ví dụ mask:

```cpp
GST_MESSAGE_EOS |
GST_MESSAGE_ERROR
```

Các message như:

```text
WARNING
STATE_CHANGED
CLOCK_LOST
LATENCY
```

có thể bị loại bỏ.

Do đó không dùng filtered pop nếu application muốn xử lý đầy đủ mọi message type.

## 4.4 Bus watch và `GMainLoop`

Ví dụ:

```cpp
gboolean on_bus_message(
    GstBus* bus,
    GstMessage* message,
    gpointer user_data) {
    (void)bus;
    (void)user_data;

    switch (GST_MESSAGE_TYPE(message)) {
    case GST_MESSAGE_EOS:
        break;

    case GST_MESSAGE_ERROR:
        break;

    default:
        break;
    }

    return G_SOURCE_CONTINUE;
}
```

Đăng ký:

```cpp
const guint watch_id =
    gst_bus_add_watch(
        bus,
        on_bus_message,
        user_data);
```

Bus watch:

- dispatch callback trong `GMainContext`;
- che giấu streaming-thread context;
- phù hợp GUI và GLib service;
- callback trả `TRUE` để giữ watch;
- callback trả `FALSE` để remove watch;
- chỉ có một Bus watch trên một Bus tại một thời điểm.

Message truyền vào callback sẽ được framework unref sau callback.

Callback không tự unref message, trừ khi code đã tự lấy thêm reference riêng.

## 4.5 Custom `GMainContext`

`gst_bus_add_watch()` gắn watch vào thread-default hoặc default main context theo API contract.

Nếu service có custom context, có thể:

- push thread-default context phù hợp; hoặc
- tạo `GSource` bằng `gst_bus_create_watch()`;
- attach source vào context mong muốn.

Cần quản lý:

- source lifetime;
- callback lifetime;
- user data;
- remove watch trước khi destroy application context.

## 4.6 External poll-fd integration

`gst_bus_get_pollfd()` cho phép tích hợp Bus vào event loop dùng file descriptor.

```cpp
GPollFD descriptor{};

gst_bus_get_pollfd(
    bus,
    &descriptor);
```

Khi `POLLIN`:

```text
Bus có message để consume
```

Quy tắc bắt buộc:

```text
không read fd
không write fd
không close fd
```

Fd chỉ dùng để nhận readiness.

Sau readiness phải dùng Bus API:

```cpp
while (GstMessage* message =
           gst_bus_pop(bus)) {
    process_message(message);
    gst_message_unref(message);
}
```

Phải drain tới khi queue rỗng để tránh:

- message còn lại;
- event loop quay lại ngay;
- busy loop;
- bỏ sót terminal message trong cùng batch.

## 4.7 Sync handler

Sync handler:

```cpp
gst_bus_set_sync_handler(
    bus,
    handler,
    user_data,
    notify);
```

Handler chạy:

```text
trong chính thread đã post message
```

Đây là điểm khác với Bus watch.

Không được làm việc nặng:

- blocking I/O;
- chờ mutex dài;
- gọi shutdown phức tạp;
- update GUI trực tiếp;
- chạy nested event loop.

Các reply chính:

| Reply | Ý nghĩa |
|---|---|
| `GST_BUS_DROP` | Handler giữ trách nhiệm drop và unref message |
| `GST_BUS_PASS` | Chuyển message vào async queue |
| `GST_BUS_ASYNC` | Đưa message vào async queue; thread post chỉ tiếp tục sau khi message được xử lý. Chỉ dùng khi hiểu rõ handshake bất đồng bộ của Bus. |

Sync handler chỉ phù hợp trường hợp đặc biệt như:

- xử lý `STREAM_STATUS` để cấu hình thread;
- đánh thức custom event loop rất sớm;
- platform integration cần same-thread notification.

Application thông thường nên dùng watch hoặc polling.

## 4.8 Không trộn consumer tùy ý

Sai:

```text
Thread A:
gst_bus_timed_pop_filtered()

Thread B:
gst_bus_pop()

Main loop:
gst_bus_add_watch()
```

trên cùng Bus mà không có ownership policy.

Hệ quả:

- consumer A lấy message consumer B đang chờ;
- ERROR bị xử lý ở thread không mong muốn;
- EOS bị mất đối với finite wait;
- cleanup race;
- kết quả không deterministic.

Quy tắc CamStream:

```text
finite diagnostic
    → wait() là Bus consumer

continuous service
    → service poll loop + drain_bus_messages()

hai mode không chạy đồng thời
```

---

# 5. Các message quan trọng

## 5.1 Terminal và non-terminal phụ thuộc application policy

Không phải mọi message đều tự có cùng ý nghĩa terminal cho mọi application.

CamStream finite diagnostic:

| Message | Outcome |
|---|---|
| `EOS` | Expected success |
| `ERROR` | Failure |
| Timeout không có message | Failure |
| `WARNING` | Log và tiếp tục |
| `STATE_CHANGED` | Log và tiếp tục |

CamStream continuous service:

| Message | Outcome |
|---|---|
| `ERROR` | Failure |
| `EOS` | Unexpected failure |
| SIGINT/SIGTERM | Normal stop |
| `WARNING` | Log và tiếp tục |
| `STATE_CHANGED` | Log và tiếp tục |

EOS vì vậy là semantic của pipeline cộng application mode.

## 5.2 `GST_MESSAGE_EOS`

> **`GST_MESSAGE_EOS` thông báo end-of-stream đã tới mức pipeline có thể báo cho application.**

EOS không có parser payload đặc biệt.

```cpp
case GST_MESSAGE_EOS:
    handle_eos();
    break;
```

Điểm cần nhớ:

```text
EOS không tự đổi pipeline về NULL
EOS không tự unref pipeline
EOS không phải ERROR
```

Sau EOS, pipeline state vẫn có thể là `PLAYING`, nhưng media handling không tiếp tục cho run đó.

Application quyết định:

- seek lại;
- chuyển playlist;
- stop;
- rebuild;
- exit.

Trong CamStream finite mode:

```text
num-buffers hoàn tất
    ↓
EOS expected
    ↓
wait() trả true
    ↓
caller stop()
```

## 5.3 EOS event khác EOS message

**EOS event**

```text
đi downstream trong graph
được interleave với dataflow
```

Source hoặc application có thể gửi EOS event.

**EOS message**

```text
đi từ pipeline lên application qua Bus
```

Sequence:

```text
source hoàn tất
    ↓
EOS event
    ↓
decoder/converter/sink drain data trước EOS
    ↓
sink hoàn tất EOS
    ↓
Pipeline/Bin tổng hợp
    ↓
GST_MESSAGE_EOS
```

Nhận EOS event ở một pad chưa đồng nghĩa application đã nhận EOS message.

## 5.4 `GST_MESSAGE_ERROR`

> **`GST_MESSAGE_ERROR` báo một fatal runtime condition làm data-passing bị gián đoạn hoặc không thể tiếp tục đúng contract.**

Message mang:

```text
GError
debug string
source object
```

Error không tự:

- đưa pipeline về `NULL`;
- đóng camera theo application policy;
- retry device;
- exit process.

Application phải phản ứng.

CamStream:

```text
ERROR
    ↓
log source/message/debug
    ↓
started=false
run_finished=true
    ↓
return Error
    ↓
service hoặc CLI stop pipeline
```

## 5.5 `GST_MESSAGE_WARNING`

> **`GST_MESSAGE_WARNING` báo vấn đề non-fatal; pipeline có thể tiếp tục streaming.**

Warning mang:

```text
GError-style warning
debug string
source
```

Warning không nên:

- bị bỏ qua hoàn toàn;
- luôn được nâng thành fatal;
- luôn được xem là vô hại.

Policy phù hợp:

```text
parse
log source
log warning message
log debug details
continue
count/rate-limit nếu production service
```

CamStream hiện parse và log warning rồi trả:

```text
Continue
```

## 5.6 `GST_MESSAGE_STATE_CHANGED`

Message mang:

```text
old state
new state
pending state
```

Parser:

```cpp
gst_message_parse_state_changed(
    message,
    &old_state,
    &new_state,
    &pending_state);
```

Mỗi child element có thể post message này.

Nếu log tất cả:

```text
camera-source
capsfilter
jpegdec
videoconvert
fakesink
pipeline
```

sẽ tạo rất nhiều log.

CamStream chỉ log khi:

```cpp
GST_MESSAGE_SRC(message) ==
    GST_OBJECT(pipeline)
```

Đây là filter observability, không phải vì child messages sai.

## 5.7 `GST_MESSAGE_ASYNC_DONE`

`ASYNC_DONE` báo asynchronous state change đã hoàn tất ở pipeline/bin context phù hợp.

Dùng khi:

- application theo dõi startup bằng Bus;
- không block trong `gst_element_get_state()`;
- cần event-driven state completion.

CamStream `start()` hiện dùng:

```text
gst_element_set_state()
+
gst_element_get_state(timeout)
```

nên không phụ thuộc `ASYNC_DONE` trong Bus consumer.

## 5.8 `GST_MESSAGE_STREAM_START`

Message báo một stream mới bắt đầu.

Có thể dùng để:

- theo dõi stream lifecycle;
- phân biệt restart hoặc stream replacement;
- observability.

Nó không thay thế:

- `STATE_CHANGED`;
- buffer arrival measurement;
- delivered FPS measurement.

## 5.9 `GST_MESSAGE_BUFFERING`

Message thường xuất hiện với network/file buffering element.

Parser:

```cpp
gint percent = 0;

gst_message_parse_buffering(
    message,
    &percent);
```

Với non-live playback:

```text
percent < 100
    → thường giữ pipeline ở PAUSED

percent = 100
    → tiếp tục PLAYING
```

Camera V4L2 pipeline hiện tại không dựa trên network buffering.

Không tự thêm generic buffering policy vào live camera service khi chưa có element yêu cầu.

## 5.10 `GST_MESSAGE_CLOCK_LOST`

Message báo pipeline clock hiện tại không còn sử dụng được.

Recovery phổ biến:

```text
set PAUSED
    ↓
set PLAYING
    ↓
pipeline chọn lại clock
```

Nhưng production policy cần:

- tránh concurrent state transition;
- debounce repeated message;
- xác nhận target state;
- xử lý failure;
- không race với stop.

CamStream hiện không có automatic clock-lost recovery.

## 5.11 `GST_MESSAGE_LATENCY`

Element post khi latency requirement thay đổi.

Application hoặc pipeline có thể gọi:

```cpp
gst_bin_recalculate_latency(
    GST_BIN(pipeline));
```

Không gọi latency recalculation từ nhiều thread không kiểm soát.

CamStream hiện không có explicit latency-message policy; current `fakesink sync=false` diagnostic path cũng không chứng minh latency không quan trọng với future production path.

## 5.12 `GST_MESSAGE_ELEMENT` và `GST_MESSAGE_APPLICATION`

**Element message**

```text
element-specific structure
```

Phải đọc documentation của element đã post.

**Application message**

```text
application tự tạo và post
```

Có thể dùng để marshal control signal vào application event loop.

Không cast hoặc đọc field khi chưa kiểm tra structure name và type.

---

# 6. Parse lỗi, cảnh báo và ownership

## 6.1 Parse `ERROR`

```cpp
GError* error = nullptr;
gchar* debug_details = nullptr;

gst_message_parse_error(
    message,
    &error,
    &debug_details);
```

Sau khi dùng:

```cpp
if (error != nullptr) {
    g_error_free(error);
}

g_free(debug_details);
```

Cả hai allocation thuộc code gọi sau parse.

## 6.2 Parse `WARNING`

```cpp
GError* warning = nullptr;
gchar* debug_details = nullptr;

gst_message_parse_warning(
    message,
    &warning,
    &debug_details);
```

Cleanup giống error:

```cpp
if (warning != nullptr) {
    g_error_free(warning);
}

g_free(debug_details);
```

`g_free(nullptr)` an toàn.

## 6.3 `GError`: domain, code và message

`GError` gồm:

```text
domain
code
message
```

**Domain**

Nhóm lỗi:

```text
GST_CORE_ERROR
GST_LIBRARY_ERROR
GST_RESOURCE_ERROR
GST_STREAM_ERROR
```

**Code**

Lỗi cụ thể trong domain.

Ví dụ:

```text
GST_RESOURCE_ERROR_OPEN_READ
GST_RESOURCE_ERROR_BUSY
GST_STREAM_ERROR_FORMAT
GST_STREAM_ERROR_DECODE
```

**Message**

Chuỗi mô tả hướng tới người dùng hoặc operator.

Không parse text của `message` để quyết định error type nếu domain/code đã có contract.

## 6.4 Debug string

Debug string thường chứa:

- file và line trong plugin;
- internal function;
- low-level reason;
- flow return;
- caps hoặc negotiation context;
- driver detail.

Ví dụ generic message:

```text
Internal data stream error
```

không đủ để xác định root cause.

Debug string có thể cho biết:

```text
streaming stopped, reason not-negotiated
streaming stopped, reason error
failed to allocate required memory
device disconnected
```

Production log nên ghi cả:

```text
source
domain
code
message
debug
```

## 6.5 Source là nơi phát hiện, chưa chắc root cause

Message source:

```text
v4l2src
```

có thể báo error vì downstream trả:

```text
GST_FLOW_NOT_NEGOTIATED
```

Root cause có thể nằm ở:

- capsfilter quá chặt;
- decoder không link;
- sink không chấp nhận format;
- device mode sai.

Do đó:

```text
message source
    = nơi post/phát hiện error

root cause
    = cần phân tích graph và debug string
```

## 6.6 Helper C++ an toàn

```cpp
#include <gst/gst.h>

#include <iostream>

void log_error_message(
    GstMessage* message) {
    if (message == nullptr) {
        return;
    }

    GError* error = nullptr;
    gchar* debug_details = nullptr;

    gst_message_parse_error(
        message,
        &error,
        &debug_details);

    const GstObject* source =
        GST_MESSAGE_SRC(message);

    std::cerr
        << "Source: "
        << (source != nullptr
                ? GST_OBJECT_NAME(source)
                : "unknown")
        << '\n'
        << "Domain: "
        << (error != nullptr
                ? g_quark_to_string(error->domain)
                : "unknown")
        << '\n'
        << "Code: "
        << (error != nullptr
                ? error->code
                : 0)
        << '\n'
        << "Message: "
        << (error != nullptr
                ? error->message
                : "unavailable")
        << '\n'
        << "Debug: "
        << (debug_details != nullptr
                ? debug_details
                : "unavailable")
        << '\n';

    if (error != nullptr) {
        g_error_free(error);
    }

    g_free(debug_details);
}
```

Không unref:

```cpp
source
```

vì đó là borrowed pointer thuộc message.

---

# 7. Finite `wait()` trong CamStream

## 7.1 Preconditions

`GstreamerPipeline::wait()` yêu cầu:

```text
pipeline đã start
pipeline pointer hợp lệ
Bus pointer hợp lệ
buffer_count > 0
```

Nếu:

```text
buffer_count == 0
```

hàm từ chối và yêu cầu dùng Bus polling cho continuous mode.

Điều này giữ contract:

```text
wait()
    = finite diagnostic consumer
```

## 7.2 Message filter hiện tại

Current mask:

```cpp
GST_MESSAGE_EOS |
GST_MESSAGE_ERROR |
GST_MESSAGE_WARNING |
GST_MESSAGE_STATE_CHANGED
```

Nghĩa là `wait()` hiện xử lý:

- EOS;
- ERROR;
- WARNING;
- pipeline state changes.

Đây là khác biệt quan trọng so với phiên bản tài liệu cũ vốn nói WARNING chưa được filter.

Các type khác có thể bị filtered pop discard:

```text
ASYNC_DONE
STREAM_START
CLOCK_LOST
LATENCY
BUFFERING
ELEMENT
APPLICATION
```

Do đó current `wait()` phù hợp finite diagnostic contract, không phải full production Bus monitor.

## 7.3 Nominal duration

Current code tính:

```text
nominal_duration =
    ceil(buffer_count / fps) seconds
```

Integer formula:

```cpp
const GstClockTime nominal_duration =
    ((buffer_count + fps - 1U) / fps) *
    GST_SECOND;
```

Ví dụ:

```text
300 buffers
30 fps
    → 10 seconds
```

Ví dụ:

```text
301 buffers
30 fps
    → 11 seconds
```

Ceiling division tránh timeout sớm khi buffer count không chia hết cho fps.

## 7.4 Stall allowance

Current policy:

```text
wait_timeout =
    nominal_duration + 120 seconds
```

120 giây là project policy cho bounded diagnostic tolerance.

Nó không phải GStreamer recommendation phổ quát.

Mục tiêu:

- cho phép startup hoặc device delay;
- không chờ vô hạn khi source stall;
- giữ CI/manual validation có điểm kết thúc;
- cho cleanup được thực thi.

Production system nên cấu hình timeout theo:

- startup budget;
- expected FPS;
- transport;
- device;
- recovery policy;
- watchdog requirement.

## 7.5 Absolute deadline

Current code dùng monotonic timestamp:

```cpp
const GstClockTime wait_started =
    gst_util_get_timestamp();
```

Sau đó:

```text
deadline =
    wait_started + wait_timeout
```

với saturation guard để tránh overflow gần `GST_CLOCK_TIME_NONE`.

Trong loop:

```text
remaining =
    deadline - current_time
```

Mỗi Bus pop chỉ chờ phần thời gian còn lại.

## 7.6 Vì sao không reset timeout?

Bus có thể liên tục nhận:

```text
STATE_CHANGED
WARNING
other matching non-terminal messages
```

Nếu mỗi message reset timeout:

```text
pipeline stall
    + periodic message
    → wait không bao giờ kết thúc
```

Absolute deadline đảm bảo:

```text
toàn bộ operation có upper bound
```

Message activity không chứng minh dataflow progress.

## 7.7 Terminal outcome

`process_bus_message()` trả:

```text
Continue
EndOfStream
Error
```

`wait()`:

```text
EndOfStream
    → return true

Error
    → return false

Continue
    → tiếp tục tới deadline
```

Khi deadline hết:

```text
started=false
run_finished=true
return false
```

Timeout là application-level terminal failure, không phải `GstMessage`.

## 7.8 Message RAII

Current code:

```cpp
std::unique_ptr<
    GstMessage,
    decltype(&gst_message_unref)>
    owned_message{
        message,
        gst_message_unref,
    };
```

Mọi loop iteration release popped message đúng một lần, kể cả:

- EOS;
- ERROR;
- WARNING;
- state changed;
- early return.

## 7.9 `wait()` không cleanup pipeline

Sau EOS, ERROR hoặc timeout:

```text
pipeline chưa tự về NULL
Bus và pipeline reference vẫn còn
```

Caller phải:

```cpp
pipeline.stop();
```

CamStream policy:

```text
terminal outcome
    ↓
stop
    ↓
GST_STATE_NULL
    ↓
unref Bus
    ↓
unref pipeline
```

---

# 8. Continuous service và Bus poll-fd

## 8.1 Service event loop

Stage 8.2 service dùng:

```text
signal fd
    \
     → application poll loop
    /
GstBus poll fd
```

Mục tiêu:

- một control loop;
- không busy polling;
- không asynchronous Unix signal handler;
- không tạo application-owned GStreamer worker thread;
- deterministic shutdown.

GStreamer vẫn có internal streaming threads.

## 8.2 Borrowed poll descriptor

Library method:

```cpp
int bus_poll_fd() const noexcept;
```

Nó gọi:

```cpp
gst_bus_get_pollfd(
    bus,
    &poll_descriptor);
```

Returned integer là borrowed descriptor.

Service:

- được pass vào `poll()`;
- không read;
- không write;
- không close;
- chỉ dùng khi built Bus còn sống.

## 8.3 Drain toàn bộ queued messages

Khi fd ready:

```cpp
while (GstMessage* message =
           gst_bus_pop(bus)) {
    process_bus_message(message);
    gst_message_unref(message);
}
```

CamStream dùng RAII cho từng message.

Draining tới queue rỗng giúp:

- xử lý warning và terminal message cùng batch;
- tránh một message còn lại giữ fd ready;
- tránh busy loop;
- gom aggregate outcome.

## 8.4 Aggregate outcome

Current logic:

```text
aggregate = Continue
```

Với từng message:

```text
ERROR
    → aggregate = Error

EOS khi aggregate chưa Error
    → aggregate = EndOfStream

WARNING/STATE/other
    → không thay terminal precedence
```

Error có precedence cao hơn EOS nếu cùng drain batch.

Lý do:

```text
EOS và ERROR cùng xuất hiện
    → không được báo success nếu đã có fatal error
```

## 8.5 EOS policy theo mode

Library chỉ báo:

```text
EndOfStream
```

Service quyết định:

**Finite mode**

```text
EOS expected
    → normal completion
    → exit 0
```

**Continuous mode**

```text
EOS unexpected
    → runtime failure
    → controlled stop
    → exit 1
```

Policy nằm ở application/service, không nên hard-code mọi EOS là success trong reusable library.

## 8.6 Warning và state message

Current shared `process_bus_message()`:

- parse và log `WARNING`;
- tiếp tục operation;
- log `STATE_CHANGED` chỉ khi source là top-level pipeline;
- default ignore message khác.

Continuous drain lấy mọi queued message bằng `gst_bus_pop()`.

Khác với finite filtered wait:

```text
drain không discard trước khi process
```

nhưng default case vẫn không có policy cho các type chưa xử lý.

## 8.7 Single-consumer contract

Public interface ghi rõ:

```text
class không thread-safe
một caller phải serialize:
build()
start()
Bus consumption
stop()
```

Không chạy:

```text
wait()
+
drain_bus_messages()
```

đồng thời.

Không gọi:

```text
stop()
```

từ thread khác khi Bus consumer đang chạy mà không có coordination.

Service sở hữu lifecycle và event loop trong một control context.

## 8.8 Controlled shutdown

Event dẫn tới shutdown:

```text
SIGINT
SIGTERM
finite EOS
continuous EOS
ERROR
initialization failure
timeout trong finite diagnostic
```

Luồng:

```text
classify outcome
    ↓
request stop
    ↓
set pipeline NULL
    ↓
confirm state nếu có thể
    ↓
unref Bus
    ↓
unref pipeline
    ↓
return exit status
```

Signal không trực tiếp chạy GStreamer cleanup trong asynchronous signal-handler context.

Service dùng synchronous `signalfd`.

---

# 9. Debug, lỗi thường gặp và tổng kết

## 9.1 Debug generic `Internal data stream error`

Không dừng ở:

```text
Internal data stream error
```

Thu thập:

```text
message source
GError domain
GError code
GError message
debug string
pipeline state
negotiated caps
kernel log
native V4L2 result
```

Workflow:

```text
1. Xác định source
2. Đọc debug string
3. Kiểm tra caps
4. Chạy graph nhỏ nhất
5. So sánh native V4L2
6. Kiểm tra dmesg
7. Thêm lại decoder/converter từng bước
```

## 9.2 USB hoặc device error

Dấu hiệu:

```text
v4l2src source
resource error
device disconnected
poll/read failure
kernel USB reset
```

Kiểm tra:

```bash
dmesg --follow
```

```bash
v4l2-ctl \
    --device=/dev/videoX \
    --stream-mmap \
    --stream-count=300 \
    --stream-to=/dev/null
```

Nếu native V4L2 cũng fail:

```text
không ưu tiên đổ lỗi cho GStreamer
```

## 9.3 `not-negotiated`

Debug string có thể chứa:

```text
reason not-negotiated
```

Kiểm tra:

- source caps;
- capsfilter;
- device mode;
- decoder;
- converter;
- sink pad caps;
- runtime current caps.

Message source có thể là upstream source, nhưng root cause có thể nằm downstream.

## 9.4 Decoder error

Nếu MJPEG passthrough:

```text
v4l2src → image/jpeg → fakesink
```

pass nhưng decode path fail:

```text
jpegdec
parser/framing
corrupted JPEG frame
raw caps downstream
```

là nhóm nghi ngờ chính.

Không kết luận từ một error string duy nhất.

## 9.5 Source stall

Stall có thể không tạo ngay `ERROR`.

Dấu hiệu:

```text
không EOS
không ERROR
không thêm buffer
```

Finite CamStream phát hiện bằng absolute deadline.

Continuous service cần future policy riêng:

- frame watchdog;
- progress counter;
- last-frame timestamp;
- health check;
- bounded recovery.

Bus message activity không đủ thay cho frame-progress monitoring.

## 9.6 Lỗi ownership thường gặp

**Leak Bus**

```cpp
GstBus* bus =
    gst_element_get_bus(pipeline);

// Thiếu gst_object_unref(bus)
```

**Leak popped message**

```cpp
GstMessage* message =
    gst_bus_pop(bus);

// Thiếu gst_message_unref(message)
```

**Free borrowed source**

```cpp
GstObject* source =
    GST_MESSAGE_SRC(message);

gst_object_unref(source); // Sai.
```

**Quên free parsed error**

```cpp
GError* error = nullptr;
gchar* debug = nullptr;

gst_message_parse_error(
    message,
    &error,
    &debug);

// Thiếu g_error_free và g_free.
```

**Unref watch callback message**

Framework unref message sau callback. Không tự unref borrowed callback message.

## 9.7 Lỗi kiến trúc thường gặp

**Sai 1 — EOS là ERROR**

EOS là normal end-of-stream condition. Application mode quyết định expected hay unexpected.

**Sai 2 — ERROR tự cleanup pipeline**

Application vẫn phải đưa pipeline về `NULL`.

**Sai 3 — Warning luôn vô hại**

Warning có thể báo degradation lặp lại. Cần log và production policy.

**Sai 4 — Filtered pop giữ lại message không khớp**

Nó discard nonmatching messages.

**Sai 5 — Poll-fd có thể `read()`**

Không được read/write/close Bus fd.

**Sai 6 — Một Bus có thể có nhiều consumer độc lập**

Message bị cạnh tranh và mất đối với consumer khác.

**Sai 7 — Sync handler giống main-loop callback**

Sync handler chạy trong posting thread.

**Sai 8 — Log mọi state message như pipeline state**

Child element cũng post state messages.

**Sai 9 — Có Bus message nghĩa là frame đang tiến triển**

State/warning message không chứng minh frame arrival.

**Sai 10 — Timeout reset sau mọi message**

Có thể tạo infinite wait.

**Sai 11 — Stop trước rồi mới mong đọc ERROR**

Bus có thể flush message khi pipeline về `NULL`.

**Sai 12 — Functional ERROR handling là automatic recovery**

Log và stop không đồng nghĩa retry/recovery đã được thiết kế.

## 9.8 Workflow phải nhớ

**Finite diagnostic**

```text
build
    ↓
start
    ↓
wait với absolute deadline
    ↓
EOS / ERROR / timeout
    ↓
stop
    ↓
NULL và release
```

**Continuous service**

```text
build
    ↓
lấy borrowed Bus poll fd
    ↓
start
    ↓
poll signal fd + Bus fd
    ↓
drain Bus bằng gst_bus_pop
    ↓
classify outcome
    ↓
controlled stop
```

**Error parsing**

```text
GST_MESSAGE_ERROR
    ↓
parse GError + debug
    ↓
log source/domain/code/message/debug
    ↓
free parsed allocations
    ↓
unref message
    ↓
stop pipeline
```

## 9.9 Kiến thức cần nhớ

1. `GstMessage` là `GstMiniObject`.
2. `GstBus` là `GstObject`.
3. Message là application notification, không phải media payload.
4. Bus marshal message từ pipeline threads tới application-consumption context.
5. `gst_element_get_bus()` trả full Bus reference.
6. Bus pop APIs trả full message reference.
7. Bus watch callback nhận message do framework quản lý.
8. `GST_MESSAGE_SRC()` trả borrowed source pointer.
9. `gst_bus_post()` nhận ownership của message.
10. Bus lưu message theo FIFO.
11. Không consume Bus thì message có thể tích lũy.
12. Pipeline mặc định flush Bus khi `READY → NULL`.
13. EOS event đi trong graph; EOS message đi lên application.
14. Pipeline EOS được tổng hợp từ sink-side EOS completion.
15. EOS không tự đổi pipeline về `NULL`.
16. ERROR là fatal cho data-passing nhưng không tự cleanup application resource.
17. WARNING là non-fatal nhưng cần policy.
18. `STATE_CHANGED` được post bởi cả child và pipeline.
19. Filter source để log top-level state rõ ràng.
20. `ASYNC_DONE` hỗ trợ event-driven state completion.
21. `CLOCK_LOST` thường cần chọn lại clock qua controlled state cycle.
22. `LATENCY` có thể yêu cầu latency recalculation.
23. Filtered pop discard message không khớp mask.
24. Bus watch phù hợp `GMainLoop`.
25. Poll-fd phù hợp external fd event loop.
26. Không read/write/close Bus poll descriptor.
27. Sync handler chạy trong posting thread và không nên block.
28. Chỉ một Bus consumer phải sở hữu message stream.
29. Error parser trả `GError` và debug string cần free.
30. Message source là nơi post error, chưa chắc root cause.
31. Current finite `wait()` filter gồm EOS, ERROR, WARNING và STATE_CHANGED.
32. Current `wait()` dùng nominal duration cộng 120 giây.
33. 120 giây là project policy, không phải GStreamer rule.
34. Absolute deadline không bị kéo dài bởi warning/state message.
35. Timeout là application terminal failure, không phải Bus message.
36. `wait()` chỉ trả success khi nhận EOS.
37. `wait()` không cleanup pipeline.
38. Continuous service dùng Bus poll fd và drain toàn bộ queue.
39. Error có precedence cao hơn EOS trong cùng drain batch.
40. Finite EOS là success; continuous EOS là failure trong CamStream service.
41. Blocking wait và service drain không chạy đồng thời.
42. Stop đưa pipeline về `NULL` rồi unref Bus và pipeline.
43. Bus message không thay thế frame-progress watchdog.
44. Runtime error handling không đồng nghĩa automatic recovery.
45. Functional validation không chứng minh long-duration reliability hoặc production readiness.

## 9.10 Nguồn GStreamer chính thức

- [G1 — `GstMessage`](https://gstreamer.freedesktop.org/documentation/gstreamer/gstmessage.html)
- [G2 — Messages design](https://gstreamer.freedesktop.org/documentation/additional/design/messages.html)
- [G3 — `GstBus`](https://gstreamer.freedesktop.org/documentation/gstreamer/gstbus.html)
- [G4 — Bus application guide](https://gstreamer.freedesktop.org/documentation/application-development/basics/bus.html)
- [G5 — Bus design](https://gstreamer.freedesktop.org/documentation/additional/design/gstbus.html)
- [G6 — `GstPipeline`](https://gstreamer.freedesktop.org/documentation/gstreamer/gstpipeline.html)
- [G7 — Pipeline design](https://gstreamer.freedesktop.org/documentation/additional/design/gstpipeline.html)
- [G8 — Bin message handling](https://gstreamer.freedesktop.org/documentation/additional/design/gstbin.html)
- [G9 — Events design](https://gstreamer.freedesktop.org/documentation/additional/design/events.html)
- [G10 — Sequence numbers](https://gstreamer.freedesktop.org/documentation/additional/design/seqnums.html)
- [G11 — Buffering design](https://gstreamer.freedesktop.org/documentation/additional/design/buffering.html)
- [G12 — Latency design](https://gstreamer.freedesktop.org/documentation/additional/design/latency.html)
- [G13 — Error domains](https://gstreamer.freedesktop.org/documentation/gstreamer/gsterror.html)
- [G14 — Debugging tools](https://gstreamer.freedesktop.org/documentation/tutorials/basic/debugging-tools.html)

## 9.11 Nguồn CamStream Control

- [P1 — `GstreamerPipeline` implementation](https://github.com/tungnhs2014/camstream-control/blob/74a6a2045198ecc7c91fe32d48dbe42b52e87faa/libs/camstream-gstreamer/src/gstreamer_pipeline.cpp)
- [P2 — `GstreamerPipeline` interface](https://github.com/tungnhs2014/camstream-control/blob/74a6a2045198ecc7c91fe32d48dbe42b52e87faa/libs/camstream-gstreamer/include/camstream/gstreamer_pipeline.hpp)
- [P3 — Stage 7 GStreamer integration validation](https://github.com/tungnhs2014/camstream-control/blob/74a6a2045198ecc7c91fe32d48dbe42b52e87faa/docs/validation/stage-07-gstreamer-integration.md)
- [P4 — Stage 8.2 service integration validation](https://github.com/tungnhs2014/camstream-control/blob/74a6a2045198ecc7c91fe32d48dbe42b52e87faa/docs/validation/stage-08.2-gstreamer-service-integration.md)

## 9.12 Cầu nối sang các bài nâng cao

Sau Bài 8, người học đã hiểu nền tảng đầy đủ để xây application camera cơ bản:

```text
Object model
    ↓
Buffer và ownership
    ↓
Format và caps
    ↓
V4L2 capture
    ↓
State lifecycle
    ↓
Bus và runtime handling
```

Các bài tiếp theo sẽ mở rộng:

```text
tee và queue
thread boundaries
backpressure
latency budget
drop policy
appsink và appsrc
service recovery
health monitoring
end-to-end optimization
```
