# Bài 7 — GStreamer State Machine và Live-Source Lifecycle

> **Lộ trình:** [Bảng đối chiếu lý thuyết ↔ lab](README.md) · [Lab 07](../practice/lesson-07/README.md). Phần CamStream Control là case study của dự án ngoài repo lab; chỉ tiêu chí PASS của lab mới là bằng chứng kiểm chứng tại repo này.

> **Dự án:** CamStream Control
> **Đối tượng:** Người mới học Embedded Linux và GStreamer
> **Phạm vi:** Bài này giải thích cách GStreamer dùng state machine để chuẩn bị, chạy, tạm dừng và giải phóng camera pipeline; đồng thời làm rõ live source, preroll, `NO_PREROLL`, asynchronous state change, clock và lifecycle hiện tại của CamStream.

---

# 1. Vì sao GStreamer cần state machine?

## 1.1 Mục tiêu và phạm vi bài học

Bài 6 đã giải thích cách `v4l2src` mở V4L2 device, cấu hình capture và đưa frame vào GStreamer graph. Bài 7 trả lời câu hỏi tiếp theo:

```text
Graph đã được xây dựng
    ↓
GStreamer mở resource, chuẩn bị dataflow,
chạy camera và release resource như thế nào?
```

Sau bài này, người học cần:

- hiểu bốn state chính thức: `NULL`, `READY`, `PAUSED`, `PLAYING`;
- hiểu state là lifecycle contract, không chỉ là biến `running`;
- hiểu vì sao `NULL → PLAYING` vẫn đi qua các state trung gian;
- hiểu resource thường được chuẩn bị hoặc giải phóng tại từng transition;
- hiểu preroll;
- phân biệt non-live source với live source;
- hiểu vì sao camera pipeline có thể trả `GST_STATE_CHANGE_NO_PREROLL`;
- phân biệt `SUCCESS`, `ASYNC`, `NO_PREROLL` và `FAILURE`;
- phân biệt current, next, pending và target state;
- hiểu `GST_STATE_VOID_PENDING` chỉ là sentinel;
- dùng đúng `gst_element_set_state()` và `gst_element_get_state()`;
- phân biệt state-change request với state-change completion;
- hiểu cách `GstPipeline` truyền state xuống các element con;
- hiểu vai trò của clock, base time và running time;
- hiểu vì sao `PLAYING → PAUSED` thường không release camera;
- hiểu vì sao phải đưa pipeline về `NULL` trước khi unref;
- hiểu lifecycle `build() → start() → wait()/poll → stop()` của CamStream;
- hiểu restart policy sau EOS hoặc runtime error;
- debug lỗi theo từng transition;
- không suy luận FPS, zero-copy hoặc production readiness từ state success.

Bài này không đi sâu vào:

| Nội dung | Bài mở rộng |
|---|---:|
| EOS event và EOS message | Bài 8 |
| `GstBus`, polling, watch và sync handler | Bài 8 |
| Phân tích `ERROR`, `WARNING`, `STATE_CHANGED` | Bài 8 |
| Latency query và latency compensation | Bài nâng cao |
| `tee`, `queue` và threading | Bài nâng cao |
| `appsink`, `appsrc` và application frame processing | Bài tích hợp ứng dụng |

## 1.2 State là lifecycle contract

Camera pipeline không chỉ có hai trạng thái:

```text
stopped
running
```

Nó sở hữu nhiều loại resource:

```text
GstPipeline object
GstElement instances
GstBus
V4L2 device file descriptor
negotiated caps
V4L2 capture buffers
GstBufferPool
streaming tasks
decoder resources
pipeline clock
```

Các câu hỏi sau phải được trả lời riêng:

```text
Object đã tồn tại chưa?
Device đã được mở chưa?
Pads đã active chưa?
Caps đã được thương lượng chưa?
Streaming thread đã chạy chưa?
Live source đã tạo frame chưa?
Sink đã đồng bộ theo clock chưa?
Device đã được đóng chưa?
```

GStreamer dùng state machine để các element cùng tuân theo một lifecycle thống nhất.

Mental model:

```text
NULL
    object tồn tại nhưng runtime media resource chưa active
    ↓
READY
    resource cơ bản đã sẵn sàng
    ↓
PAUSED
    data path đã chuẩn bị
    ↓
PLAYING
    pipeline chạy theo clock
```

## 1.3 Object graph khác runtime state

Bài 2 đã giải thích graph được tạo bằng element, pad, link, bin và pipeline.

Graph có thể đã xây xong nhưng vẫn ở `NULL`:

```text
GstPipeline đã tồn tại
    ├── v4l2src đã được add
    ├── capsfilter đã được add
    ├── jpegdec đã được add nếu cần
    ├── videoconvert đã được add nếu cần
    ├── fakesink đã được add
    └── các element đã link

Nhưng:
    camera chưa nhất thiết được mở
    capture chưa chạy
    chưa có frame đi qua graph
```

Do đó:

```text
build graph thành công
    ≠
pipeline đã start

link thành công
    ≠
state transition thành công

PLAYING request được chấp nhận
    ≠
pipeline đã hoàn tất việc vào PLAYING
```

---

# 2. Bốn state chính thức

## 2.1 `GST_STATE_NULL`

> **`NULL` là state khởi tạo và là state thấp nhất của một `GstElement`.**

Mental model:

```text
Element object tồn tại
nhưng không giữ runtime media resource cần cho READY trở lên
```

Trong CamStream sau `build()`:

```text
pipeline object đã tồn tại
elements đã được add và link
bus reference đã được lấy
pipeline vẫn ở NULL
```

Không nhầm:

```text
GST_STATE_NULL
    ≠
C++ nullptr
```

`GST_STATE_NULL` là giá trị trong enum `GstState`. `nullptr` là null pointer của C++.

Khi pipeline về `NULL`, các device element phải đóng device và reset internal state theo state contract.

## 2.2 `GST_STATE_READY`

> **`READY` là state mà element đã kiểm tra và giữ các resource cơ bản cần thiết để có thể chuyển sang `PAUSED`.**

Transition:

```text
NULL → READY
```

thường gồm:

- kiểm tra resource có tồn tại;
- probe capability;
- mở device nếu cần;
- giữ file descriptor hoặc hardware handle;
- cập nhật caps phụ thuộc device;
- chuẩn bị object gắn với backend.

Với `v4l2src`, lỗi trong vùng này thường liên quan:

```text
device path sai
permission denied
device busy
node không phải image capture node
camera đã bị rút
driver không mở được hardware
```

Điểm quan trọng:

```text
READY có thể vẫn giữ camera device
```

Với device source, state design đặt việc mở device tại `NULL → READY` và đóng device tại `READY → NULL`.

Vì vậy:

```text
PLAYING → PAUSED
    không đủ để chắc chắn release camera

PLAYING → READY
    chưa phải transition đóng device cuối cùng

READY → NULL
    là transition release device theo lifecycle contract
```

## 2.3 `GST_STATE_PAUSED`

> **`PAUSED` là state mà element đã sẵn sàng nhận và xử lý data; sink thường giữ một preroll item rồi chặn thay vì render theo clock.**

Transition:

```text
READY → PAUSED
```

thường gồm:

- activate pads;
- bắt đầu hoặc chuẩn bị streaming tasks;
- chuẩn bị buffer pool;
- thực hiện caps negotiation;
- chuẩn bị decoder và converter;
- đưa data path vào trạng thái có thể hoạt động;
- reset pipeline running time về 0 trong lần chuyển đầu tiên.

`PAUSED` không có nghĩa:

```text
mọi element hoàn toàn không xử lý data
```

Với non-live pipeline:

```text
source có thể tạo dữ liệu
decoder có thể xử lý
sink nhận preroll buffer rồi block
```

Với live pipeline:

```text
live source thường không tạo data trong PAUSED
```

Đây là nguyên nhân của `NO_PREROLL`.

## 2.4 `GST_STATE_PLAYING`

> **`PLAYING` là state mà pipeline chạy theo clock; live source tạo dữ liệu và sink bắt đầu render hoặc tiêu thụ theo synchronization policy.**

Transition:

```text
PAUSED → PLAYING
```

thường gồm:

- pipeline chọn `GstClock`;
- pipeline tính `base_time`;
- clock và base time được phân phối cho các element;
- sink ngừng block trên preroll item;
- sink bắt đầu render hoặc consume data;
- live source bắt đầu tạo buffer.

Đối với CamStream:

```text
v4l2src
    ↓ GstBuffer
capsfilter
    ↓
jpegdec và videoconvert nếu là MJPEG
    ↓
fakesink
```

`fakesink sync=false` không chờ presentation time trước khi consume buffer, nhưng pipeline vẫn phải tuân theo state lifecycle.

## 2.5 Bảng tổng hợp

| State | Runtime resource | Data path | Live camera tạo frame? | Sink |
|---|---|---|---|---|
| `NULL` | Chưa giữ resource mức READY | Chưa active | Không | Chưa xử lý |
| `READY` | Resource cơ bản có thể đã được giữ | Chưa streaming | Không | Chưa preroll |
| `PAUSED` | Data path đã chuẩn bị | Có thể active | Thường không | Preroll hoặc chờ |
| `PLAYING` | Đầy đủ resource | Đang chạy | Có | Render/consume theo policy |

Đây là mental model tổng quát. Một element cụ thể có thể có thêm resource nội bộ, nhưng không được phá state contract.

---

# 3. Preroll và live source

## 3.1 Preroll là gì?

> **Preroll là quá trình đưa sink vào trạng thái đã nhận đủ thông tin hoặc data đầu tiên để có thể bắt đầu phát ngay khi pipeline chuyển sang `PLAYING`.**

Với non-live pipeline:

```text
file source
    ↓ đọc trước buffer
decoder
    ↓
sink nhận buffer đầu tiên
    ↓
sink giữ buffer và block
    ↓
PAUSED hoàn tất
```

Preroll giúp xác nhận:

- source tạo được data;
- caps negotiation hoàn tất;
- decoder đã sẵn sàng;
- sink đã có item đầu tiên;
- pipeline có thể bắt đầu phát mà không cần chờ thêm bước chuẩn bị lớn.

Sink có thể hoàn tất preroll khi nhận:

- buffer;
- GAP event;
- EOS trong trường hợp không có data.

Preroll là hành vi của data path và sink, không phải việc application tự đọc một frame.

## 3.2 Non-live source và live source

**Non-live source**

> **Non-live source đọc dữ liệu đã tồn tại độc lập với thời gian thực.**

Ví dụ:

```text
filesrc
```

Nó có thể đọc trước dữ liệu trong `PAUSED`.

Nếu pipeline pause một file:

```text
data gốc vẫn còn đó
position có thể được giữ
pipeline có thể resume từ timeline phù hợp
```

**Live source**

> **Live source tạo hoặc nhận dữ liệu theo thời gian thực.**

Ví dụ:

```text
camera
microphone
live network capture
```

Frame camera tương ứng một khoảng thời gian thực tế. Khi pipeline không capture:

```text
frame của khoảng thời gian đó không thể được đọc lại như file
```

Live source phải timestamp buffer sao cho running time của buffer phù hợp với running time của pipeline tại thời điểm capture.

## 3.3 `GST_STATE_CHANGE_NO_PREROLL`

> **`GST_STATE_CHANGE_NO_PREROLL` cho biết state change đã thành công nhưng element hoặc pipeline là live và không thể tạo preroll data trong `PAUSED`.**

Khi live source đi:

```text
READY → PAUSED
```

nó thường:

- không tạo buffer;
- trả `GST_STATE_CHANGE_NO_PREROLL`.

Điểm cần nhớ:

```text
NO_PREROLL
    ≠
FAILURE
```

`NO_PREROLL` là kết quả thành công có semantic riêng.

Nó nói:

```text
pipeline không thể dùng preroll theo kiểu non-live
```

Nó không nói:

```text
camera lỗi
caps lỗi
pipeline không thể PLAYING
```

Khi chuyển:

```text
PAUSED → PLAYING
```

live source bắt đầu tạo data.

Khi chuyển:

```text
PLAYING → PAUSED
```

live source dừng tạo data và có thể lại trả `NO_PREROLL`.

## 3.4 `PAUSED` và `PLAYING` với camera

Đối với file:

```text
PAUSED
    sink giữ preroll buffer
    running time đứng yên

PLAYING
    sink render khi clock tới đúng running time
```

Đối với camera:

```text
PAUSED
    data path có thể đã chuẩn bị
    live source thường không tạo frame

PLAYING
    camera source tạo frame theo thời gian thực
```

Do đó không nên mô tả:

```text
PAUSED = camera đóng băng đúng frame hiện tại
```

Với live camera, frame tiếp theo chỉ tồn tại khi hardware capture nó. Các frame trong thời gian pause có thể không được giữ lại.

---

# 4. Cơ chế đổi state

## 4.1 `gst_element_set_state()` đi qua state trung gian

Application yêu cầu:

```cpp
gst_element_set_state(
    pipeline,
    GST_STATE_PLAYING);
```

Nếu pipeline đang ở `NULL`, GStreamer không nhảy bỏ qua các state.

Luồng:

```text
NULL
    ↓ NULL_TO_READY
READY
    ↓ READY_TO_PAUSED
PAUSED
    ↓ PAUSED_TO_PLAYING
PLAYING
```

Tương tự khi dừng:

```text
PLAYING
    ↓ PLAYING_TO_PAUSED
PAUSED
    ↓ PAUSED_TO_READY
READY
    ↓ READY_TO_NULL
NULL
```

Mỗi transition có thể fail ở một tầng khác nhau.

Không cần application gọi `set_state()` riêng cho từng child element. `GstPipeline`, thông qua `GstBin`, truyền state xuống children.

## 4.2 Current, next, pending và target state

GStreamer quản lý nhiều biến state nội bộ.

| Biến | Ý nghĩa |
|---|---|
| Current state | State element đã đạt hiện tại |
| Next state | State trung gian tiếp theo |
| Pending state | Final state element đang cố đạt |
| Target state | Final state application yêu cầu |
| Last return | Kết quả state change gần nhất |

Ví dụ application yêu cầu:

```text
NULL → PLAYING
```

Trong một thời điểm:

```text
current = READY
next = PAUSED
pending = PLAYING
target = PLAYING
```

Current state chưa bằng target state không nhất thiết là lỗi. Element có thể đang transition.

## 4.3 `GST_STATE_VOID_PENDING`

> **`GST_STATE_VOID_PENDING` là sentinel nghĩa là không còn pending state phù hợp để báo, không phải state runtime thứ năm.**

Không được viết:

```text
NULL
READY
PAUSED
PLAYING
VOID_PENDING
```

như năm state ngang hàng.

Ví dụ khi element đã đạt target:

```text
current = PLAYING
pending = VOID_PENDING
```

## 4.4 Bốn giá trị `GstStateChangeReturn`

**`GST_STATE_CHANGE_SUCCESS`**

```text
State change đã hoàn tất đồng bộ
```

**`GST_STATE_CHANGE_ASYNC`**

```text
Yêu cầu đã được chấp nhận
nhưng state change sẽ hoàn tất sau
```

Application phải:

- gọi `gst_element_get_state()` với timeout; hoặc
- chờ message phù hợp trên Bus.

Chi tiết Bus thuộc Bài 8.

**`GST_STATE_CHANGE_NO_PREROLL`**

```text
State change thành công
nhưng live pipeline không preroll trong PAUSED
```

Không phải lỗi.

**`GST_STATE_CHANGE_FAILURE`**

```text
State change thất bại
```

Element nên post `GST_MESSAGE_ERROR` lên Bus với thông tin cụ thể hơn.

Điểm chính thức quan trọng:

```text
State change tới READY hoặc NULL
không trả ASYNC
```

Tuy vậy, code vẫn nên kiểm tra:

- return value;
- current state;
- error message nếu thất bại.

## 4.5 Yêu cầu đổi state khác hoàn tất đổi state

`gst_element_set_state()` trả kết quả của request tại thời điểm gọi.

Ví dụ:

```cpp
const GstStateChangeReturn request_result =
    gst_element_set_state(
        pipeline,
        GST_STATE_PLAYING);
```

Nếu kết quả là `ASYNC`:

```text
request hợp lệ
nhưng chưa hoàn tất
```

Không được lập tức đánh dấu:

```text
started = true
```

mà chưa kiểm tra final state.

Nếu request trả `NO_PREROLL`, pipeline là live nhưng application vẫn cần xác minh target state đã đạt nếu contract yêu cầu synchronous startup confirmation.

## 4.6 `gst_element_get_state()` và timeout

API:

```cpp
GstState current_state = GST_STATE_VOID_PENDING;
GstState pending_state = GST_STATE_VOID_PENDING;

const GstStateChangeReturn result =
    gst_element_get_state(
        pipeline,
        &current_state,
        &pending_state,
        timeout);
```

Nếu state change đang `ASYNC`, hàm chờ tối đa `timeout`.

Kết quả có thể là:

| Kết quả | Diễn giải |
|---|---|
| `SUCCESS` | Target đã đạt |
| `NO_PREROLL` | Live state change đã hoàn tất theo contract |
| `ASYNC` | Timeout hết nhưng transition vẫn chưa hoàn tất |
| `FAILURE` | Transition abort do lỗi |

Nếu timeout là:

```text
GST_CLOCK_TIME_NONE
```

hàm có thể chờ vô hạn.

Đối với CLI hoặc service:

```text
finite timeout
```

thường an toàn hơn để tránh treo cleanup hoặc startup vô hạn.

## 4.7 Mẫu xác nhận `PLAYING`

Ví dụ C++17:

```cpp
#include <gst/gst.h>

bool reach_playing(
    GstElement* pipeline,
    GstClockTime timeout) {
    if (pipeline == nullptr) {
        return false;
    }

    const GstStateChangeReturn request_result =
        gst_element_set_state(
            pipeline,
            GST_STATE_PLAYING);

    if (request_result ==
        GST_STATE_CHANGE_FAILURE) {
        return false;
    }

    GstState current_state =
        GST_STATE_VOID_PENDING;

    GstState pending_state =
        GST_STATE_VOID_PENDING;

    const GstStateChangeReturn completion_result =
        gst_element_get_state(
            pipeline,
            &current_state,
            &pending_state,
            timeout);

    const bool completed =
        completion_result ==
            GST_STATE_CHANGE_SUCCESS ||
        completion_result ==
            GST_STATE_CHANGE_NO_PREROLL;

    return completed &&
           current_state ==
               GST_STATE_PLAYING;
}
```

Mẫu này:

- không coi `NO_PREROLL` là lỗi;
- không coi `ASYNC` sau timeout là success;
- kiểm tra final current state;
- không tự xử lý Bus error.

Production code vẫn phải đọc Bus để biết nguyên nhân failure.

---

# 5. State propagation, clock và timeline

## 5.1 Pipeline truyền state xuống children

`GstPipeline` là top-level `GstBin`.

Khi application gọi:

```cpp
gst_element_set_state(
    pipeline,
    target_state);
```

bin quản lý state của children.

GStreamer chuyển children theo thứ tự phù hợp để dataflow không chạy vào downstream chưa sẵn sàng.

Mental model:

```text
Application
    ↓ set state trên pipeline
GstPipeline / GstBin
    ↓ propagate
sink, converter, decoder, source
```

Application thường không nên gọi `set_state()` riêng trên từng child của một static pipeline.

Ngoại lệ nâng cao:

- element được thêm động khi pipeline đang chạy;
- locked state;
- custom lifecycle.

Khi thêm element động, code thường phải dùng:

```cpp
gst_element_sync_state_with_parent(
    element);
```

Nội dung dynamic graph chi tiết không thuộc Bài 7.

## 5.2 Upward và downward transition

**Upward transition**

```text
NULL → READY → PAUSED → PLAYING
```

Mục tiêu:

- acquire resource;
- prepare data path;
- bắt đầu chạy.

**Downward transition**

```text
PLAYING → PAUSED → READY → NULL
```

Mục tiêu:

- dừng clock-driven dataflow;
- stop streaming tasks;
- deactivate pads;
- quên negotiated format ở tầng phù hợp;
- đóng device;
- reset internal state.

Khi đi xuống, downstream được shut down theo cách làm upstream push nhận `GST_FLOW_FLUSHING`, giúp các streaming task thoát khỏi dataflow đang active.

## 5.3 `GstClock`, absolute time và base time

`GstClock` trả một thời gian tăng đơn điệu:

```text
absolute_time
```

Giá trị này không nhất thiết bắt đầu từ 0.

Pipeline tạo running time bằng:

```text
running_time =
    absolute_time - base_time
```

`base_time` là mốc được pipeline chọn khi vào `PLAYING`.

Mục tiêu:

```text
mọi element dùng chung clock và base time
    ↓
tính cùng running time
    ↓
đồng bộ buffer giữa các stream
```

Application bình thường không cần tự chọn clock hoặc base time. `GstPipeline` quản lý việc này.

## 5.4 Running time theo state

**`NULL` và `READY`**

```text
running time chưa có ý nghĩa runtime
```

**`PAUSED`**

```text
running time đứng yên
```

Lần đầu pipeline vào `PAUSED`:

```text
running time = 0
```

**`PLAYING`**

```text
running time tăng theo pipeline clock
```

Khi:

```text
PLAYING → PAUSED
```

pipeline lưu running time hiện tại.

Khi:

```text
PAUSED → PLAYING
```

pipeline tính base time mới để running time tiếp tục từ giá trị đã lưu, không nhảy tùy ý theo absolute clock.

## 5.5 Buffer timestamp và live capture

Live source timestamp buffer theo running time tại thời điểm capture.

Mô hình:

```text
camera capture frame
    ↓
v4l2src tạo timestamp
    ↓
buffer running time
    ↓
sink so sánh với pipeline running time
```

Điều này cho phép đồng bộ:

- camera với audio;
- nhiều live source;
- live source với stream khác.

Bài 3 đã giới thiệu PTS và duration. Bài 7 chỉ giải thích timestamp liên hệ với state và clock.

## 5.6 `sync=false` không bỏ state machine

Với:

```text
fakesink sync=false
```

sink không chờ pipeline clock để consume buffer.

Nhưng `sync=false` không:

- bỏ qua `NULL`, `READY`, `PAUSED`, `PLAYING`;
- làm live source tạo data trong `NULL`;
- thay thế caps negotiation;
- tự release device;
- chứng minh source đạt requested FPS.

Nó chỉ thay đổi sink synchronization policy.

---

# 6. Liên hệ với V4L2 lifecycle

## 6.1 Mapping chỉ là conceptual

Có thể dùng mental model:

```text
NULL → READY
    mở hoặc probe V4L2 device

READY → PAUSED
    cấu hình format
    chuẩn bị buffer queue
    activate data path

PAUSED → PLAYING
    bắt đầu live capture

PLAYING → PAUSED
    ngừng tạo live data

PAUSED → READY
    stop streaming path
    deactivate pads và tasks

READY → NULL
    đóng device
```

Nhưng đây không phải bảng ioctl bắt buộc một-một.

Không được suy luận:

```text
mỗi state transition
    = đúng một ioctl cố định
```

Implementation có thể:

- mở device sớm hoặc muộn trong phạm vi state contract;
- cấu hình format nhiều lần;
- chuẩn bị pool qua allocation negotiation;
- dùng MMAP, USERPTR hoặc DMA-BUF;
- reconfigure khi caps thay đổi;
- thực hiện internal fallback.

## 6.2 `NULL → READY`: acquire device resource

Với `v4l2src`, vùng này thường kiểm tra:

- device path;
- quyền truy cập;
- capability;
- device busy;
- hardware tồn tại.

Lỗi ví dụ:

```text
Could not open device
Permission denied
Device or resource busy
Not a capture device
```

Nếu transition fail:

```text
current state thường vẫn ở NULL
pending state phản ánh target chưa đạt
Bus có error chi tiết
```

## 6.3 `READY → PAUSED`: chuẩn bị data path

Vùng này thường liên quan:

- caps negotiation;
- V4L2 format;
- buffer pool;
- allocation;
- pad activation;
- streaming task preparation;
- sink preroll hoặc live `NO_PREROLL`.

Lỗi thường gặp:

```text
not-negotiated
unsupported mode
buffer allocation failure
decoder setup failure
pool configuration failure
```

Với live source:

```text
NO_PREROLL
```

là expected success semantic, không phải lỗi setup.

## 6.4 `PAUSED → PLAYING`: live data bắt đầu

Pipeline:

- chọn clock;
- tính base time;
- sink bắt đầu consume;
- live source bắt đầu tạo buffer.

Với V4L2 camera, capture data thực sự bắt đầu chảy trong vùng lifecycle này theo `v4l2src` implementation.

Nếu pipeline vào `PLAYING` nhưng không có frame:

- state machine có thể vẫn thành công;
- source có thể stall sau startup;
- USB hoặc driver có thể lỗi runtime;
- downstream có thể block.

Do đó:

```text
PLAYING reached
    ≠
frame throughput đã đạt yêu cầu
```

## 6.5 Downward transition và release camera

**`PLAYING → PAUSED`**

- live source dừng tạo data;
- running time được giữ;
- camera device có thể vẫn mở;
- negotiated path có thể vẫn tồn tại.

**`PAUSED → READY`**

- streaming tasks dừng;
- pads deactivate;
- capture streaming path được teardown ở tầng phù hợp;
- negotiated formats có thể bị quên.

**`READY → NULL`**

- device được đóng;
- internal device state reset.

Muốn chắc chắn application khác có thể mở camera:

```text
đưa pipeline về NULL
```

Không nên chỉ:

```text
unref pipeline đang PLAYING
```

rồi kỳ vọng framework đã thực hiện clean lifecycle.

---

# 7. Lifecycle hiện tại của CamStream Control

## 7.1 `build()`

`build()` hiện tại:

1. kiểm tra configuration;
2. gọi `gst_init_check()`;
3. tạo `GstPipeline`;
4. tạo element;
5. add element vào pipeline;
6. cấu hình source, capsfilter và sink;
7. link graph;
8. lấy `GstBus`;
9. đánh dấu `built=true`.

Sau `build()`:

```text
Graph tồn tại
Bus reference tồn tại
Pipeline vẫn ở NULL
Camera chưa được start
```

`build()` idempotent trong state hợp lệ:

```text
nếu built đã true
    → trả true
```

Nếu còn partial object từ lần trước, code gọi `stop()` để cleanup trước khi build lại.

## 7.2 `start()`

`start()` hiện tại:

```text
nếu started
    → trả true

nếu previous run đã finished
    → yêu cầu stop và rebuild

nếu chưa build
    → fail

set pipeline PLAYING
    ↓
nếu request FAILURE
    → fail

get_state với timeout 5 giây
    ↓
FAILURE hoặc ASYNC timeout
    → fail

current state không phải PLAYING
    → fail

SUCCESS hoặc NO_PREROLL
và current state là PLAYING
    → started=true
```

Điểm đúng:

- không coi request `ASYNC` là failure ngay lập tức;
- không coi `NO_PREROLL` là failure;
- xác nhận current state;
- dùng timeout hữu hạn;
- không đánh dấu started trước completion check.

## 7.3 Finite run và continuous run

**Finite diagnostic mode**

```text
buffer_count > 0
```

`v4l2src` được cấu hình `num-buffers`.

Application dùng:

```text
wait()
```

để chờ terminal outcome.

**Continuous service mode**

```text
buffer_count == 0
```

Source giữ `num-buffers=-1`.

Service không gọi blocking `wait()`. Nó poll:

- signal descriptor;
- borrowed `GstBus` poll descriptor.

Khi Bus fd ready, service drain message bằng GStreamer Bus API.

Hai cách consume Bus:

```text
blocking wait()
```

và:

```text
service-side drain
```

là alternative mode và không được chạy đồng thời trên cùng pipeline.

Chi tiết message handling thuộc Bài 8.

## 7.4 `stop()`

`stop()` hiện tại:

1. nếu pipeline tồn tại, yêu cầu `GST_STATE_NULL`;
2. nếu request không failure, gọi `gst_element_get_state()` với timeout;
3. kiểm tra current state là `NULL`;
4. reset lifecycle flags;
5. unref `GstBus`;
6. unref pipeline;
7. trả kết quả cleanup.

State change tới `NULL` theo API không trả `ASYNC`, nhưng code vẫn kiểm tra final state để làm cleanup observable.

Ngay cả khi request hoặc completion báo failure, `stop()` vẫn:

- reset flags;
- unref owned Bus;
- unref owned pipeline.

Đây là best-effort deterministic object cleanup.

Tuy nhiên, nếu element thật sự không thể chuyển về `NULL`, code không được tuyên bố chắc chắn hardware resource đã release đúng. Normal validated paths đã đạt `NULL` và cleanup thành công.

## 7.5 Destructor và RAII boundary

Destructor:

```cpp
GstreamerPipeline::~GstreamerPipeline()
    noexcept {
    if (!stop()) {
        std::cerr
            << "Error: GStreamer cleanup "
               "failed during destruction\n";
    }
}
```

Mục tiêu:

- object không rời scope khi vẫn còn owned GStreamer references;
- partial build cũng được cleanup;
- caller không cần nhớ unref từng internal object.

Destructor không thể trả lỗi. Vì vậy explicit `stop()` vẫn cần khi application muốn xử lý cleanup failure có chủ đích.

## 7.6 Restart policy

Current policy:

```text
EOS hoặc ERROR hoặc timeout
    ↓
run_finished=true
started=false
    ↓
start() lại trên object đó bị từ chối
    ↓
phải stop()
    ↓
build()
    ↓
start()
```

Lý do:

- finite source đã hoàn thành contract;
- Bus có thể còn message;
- element internal state có thể cần reset;
- V4L2 resource cần teardown sạch;
- recreate dễ reasoning hơn resume mơ hồ.

Đây là project policy, không phải quy tắc rằng mọi GStreamer pipeline bắt buộc phải destroy sau EOS.

[Ví dụ restart trong Lab 07](../practice/lesson-07/src/05_restart_pipeline.cpp) cố ý chạy lại **cùng pipeline tổng hợp** sau khi đã về `NULL`. Nó chứng minh một pipeline có thể được tái sử dụng trong trường hợp đó; không thay thế policy stop–rebuild của CamStream Control với camera và service lifecycle.

Một application khác có thể thiết kế restart trên cùng pipeline nếu:

- state reset rõ ràng;
- source hỗ trợ;
- Bus được drain;
- timeline và EOS được xử lý đúng;
- behavior đã được test.

## 7.7 Idempotence và thread safety

Current behavior:

```text
build() khi đã built
    → success

start() khi đã started
    → success

stop() khi object đã sạch
    → success
```

Điều này giúp cleanup:

- sau partial construction;
- sau startup failure;
- trong destructor;
- trong nhiều error path.

`gst_element_set_state()` là MT-safe ở API level.

Nhưng application lifecycle vẫn phải được serialize.

Ví dụ race semantic:

```text
Thread A: start()
Thread B: stop()
Thread C: drain Bus
```

Framework lock không tự quyết định policy nào thắng.

Service nên có một owner/control thread hoặc mutex/state model rõ ràng.

---

# 8. Debug state failure và lỗi thường gặp

## 8.1 Debug theo transition

Không chỉ log:

```text
Failed to start pipeline
```

Phải xác định transition nào fail.

**`NULL → READY`**

Kiểm tra:

- device path;
- permission;
- device busy;
- driver;
- node capability;
- hot unplug.

**`READY → PAUSED`**

Kiểm tra:

- caps;
- negotiation;
- V4L2 format;
- buffer allocation;
- decoder;
- pool;
- `NO_PREROLL` có bị hiểu sai không.

**`PAUSED → PLAYING`**

Kiểm tra:

- clock selection;
- source start;
- sink;
- runtime USB failure;
- downstream block.

**`PLAYING → NULL`**

Kiểm tra:

- streaming thread có thoát không;
- driver có unblock `DQBUF` hoặc wait không;
- device disconnect;
- element cleanup;
- application có concurrent Bus consumer không.

## 8.2 State-aware startup tối thiểu

```cpp
#include <gst/gst.h>

bool start_pipeline(
    GstElement* pipeline) {
    if (pipeline == nullptr) {
        return false;
    }

    constexpr GstClockTime timeout =
        5 * GST_SECOND;

    const GstStateChangeReturn request_result =
        gst_element_set_state(
            pipeline,
            GST_STATE_PLAYING);

    if (request_result ==
        GST_STATE_CHANGE_FAILURE) {
        return false;
    }

    GstState current_state =
        GST_STATE_VOID_PENDING;

    GstState pending_state =
        GST_STATE_VOID_PENDING;

    const GstStateChangeReturn completion_result =
        gst_element_get_state(
            pipeline,
            &current_state,
            &pending_state,
            timeout);

    if (completion_result ==
            GST_STATE_CHANGE_FAILURE ||
        completion_result ==
            GST_STATE_CHANGE_ASYNC) {
        return false;
    }

    return current_state ==
           GST_STATE_PLAYING;
}
```

Đoạn code này chưa log error details. Bài 8 sẽ bổ sung Bus processing.

## 8.3 Stop tối thiểu

```cpp
bool stop_pipeline(
    GstElement* pipeline) {
    if (pipeline == nullptr) {
        return true;
    }

    const GstStateChangeReturn request_result =
        gst_element_set_state(
            pipeline,
            GST_STATE_NULL);

    if (request_result ==
        GST_STATE_CHANGE_FAILURE) {
        return false;
    }

    GstState current_state =
        GST_STATE_VOID_PENDING;

    GstState pending_state =
        GST_STATE_VOID_PENDING;

    const GstStateChangeReturn completion_result =
        gst_element_get_state(
            pipeline,
            &current_state,
            &pending_state,
            5 * GST_SECOND);

    return completion_result !=
               GST_STATE_CHANGE_FAILURE &&
           current_state ==
               GST_STATE_NULL;
}
```

Sau khi stop thành công:

```cpp
gst_object_unref(pipeline);
pipeline = nullptr;
```

Nếu stop thất bại, application vẫn cần best-effort ownership cleanup và log rõ rằng resource release chưa được xác nhận.

## 8.4 `STATE_CHANGED` message và `get_state()`

Hai cơ chế phục vụ mục đích khác nhau.

**`gst_element_get_state()`**

```text
Application chủ động hỏi current và pending state
Có thể chờ completion với timeout
```

**`GST_MESSAGE_STATE_CHANGED`**

```text
Element thông báo state transition lên Bus
Asynchronous notification
```

Mọi child element có thể post state message. Vì vậy nếu chỉ muốn log lifecycle tổng quát:

```cpp
if (GST_MESSAGE_SRC(message) ==
    GST_OBJECT(pipeline)) {
    // Log top-level pipeline state.
}
```

Nếu không filter source, log có thể rất nhiều và khó đọc.

Chi tiết ownership và parsing message thuộc Bài 8.

## 8.5 Lệnh debug

Theo dõi state:

```bash
GST_DEBUG=GST_STATES:6 \
gst-launch-1.0 ...
```

Kết hợp pipeline và V4L2:

```bash
GST_DEBUG=GST_STATES:6,v4l2*:6 \
gst-launch-1.0 ...
```

Theo dõi tổng quát hơn:

```bash
GST_DEBUG=3 \
gst-launch-1.0 ...
```

Kernel song song:

```bash
dmesg --follow
```

Device state:

```bash
v4l2-ctl \
    --device=/dev/videoX \
    --all
```

## 8.6 Lỗi tư duy thường gặp

**Sai 1 — `PAUSED` nghĩa là mọi element không xử lý data**

Non-live source và decoder có thể xử lý đến preroll.

**Sai 2 — `NO_PREROLL` là lỗi**

Đây là expected success semantic của live pipeline.

**Sai 3 — `ASYNC` là lỗi**

Nó nghĩa là transition chưa hoàn tất tại thời điểm return.

**Sai 4 — Gọi `PLAYING` từ `NULL` nhảy thẳng tới `PLAYING`**

GStreamer đi qua `READY` và `PAUSED`.

**Sai 5 — `gst_element_set_state()` return không failure nghĩa là target đã đạt**

`ASYNC` cần confirmation.

**Sai 6 — `READY` chắc chắn đã release camera**

Device source thường chỉ đóng device tại `READY → NULL`.

**Sai 7 — `PLAYING → PAUSED` giống pause file**

Live frame trong thời gian pause không được đảm bảo giữ lại.

**Sai 8 — Unref pipeline đang `PLAYING` là cleanup đúng**

Phải yêu cầu về `NULL` trước khi release final reference.

**Sai 9 — Set state từng child sẽ an toàn hơn**

Pipeline phải quản lý child state order.

**Sai 10 — `GST_STATE_VOID_PENDING` là state thứ năm**

Nó chỉ là sentinel.

**Sai 11 — Reached `PLAYING` nghĩa là camera đạt 30 fps**

State success không đo throughput.

**Sai 12 — State map một-một với V4L2 ioctl**

Chỉ được dùng như conceptual mapping.

## 8.7 Bằng chứng hiện tại của CamStream

Stage 7 validation đã ghi nhận:

- dynamic capture-node identification pass;
- YUY2 negotiation pass;
- MJPEG negotiation pass;
- repeated raw pipeline lifecycle pass;
- repeated MJPEG pipeline lifecycle pass;
- finite runs nhận EOS;
- accepted runs exit status 0;
- pipeline về `NULL`;
- cleanup pass trong phạm vi quan sát.

Stage 8.2 bổ sung:

- continuous service mode;
- signal fd và Bus fd trong một poll loop;
- controlled shutdown cho SIGINT và SIGTERM;
- deterministic, idempotent `stop()`;
- blocking `wait()` và service Bus drain là hai mode loại trừ nhau.

Các kết quả này chưa chứng minh:

- stable delivered 30 fps;
- long-duration USB reliability;
- end-to-end zero-copy;
- mọi startup/shutdown race;
- recovery hoàn chỉnh sau hot unplug;
- production readiness ngoài phạm vi validation.

---

# 9. Tổng kết và nguồn tham khảo

## 9.1 Workflow phải nhớ

**Startup**

```text
build graph
    ↓
pipeline ở NULL
    ↓
set_state(PLAYING)
    ↓
NULL → READY
    acquire/probe resource
    ↓
READY → PAUSED
    prepare data path
    live source có thể NO_PREROLL
    ↓
PAUSED → PLAYING
    select clock
    calculate base time
    live source tạo buffer
    ↓
confirm current state
```

**Finite run**

```text
PLAYING
    ↓
source tạo đủ num-buffers
    ↓
EOS
    ↓
wait returns terminal outcome
    ↓
stop
    ↓
NULL
```

**Continuous service**

```text
PLAYING
    ↓
application poll signal fd + Bus fd
    ↓
SIGINT / SIGTERM / EOS / ERROR
    ↓
stop
    ↓
NULL
```

**Cleanup**

```text
request NULL
    ↓
confirm NULL nếu có thể
    ↓
unref Bus
    ↓
unref Pipeline
    ↓
clear application lifecycle flags
```

## 9.2 Kiến thức cần nhớ

1. GStreamer có bốn state: `NULL`, `READY`, `PAUSED`, `PLAYING`.
2. `GST_STATE_VOID_PENDING` không phải state runtime thứ năm.
3. State machine quản lý resource ownership và dataflow lifecycle.
4. Graph được build không có nghĩa pipeline đã start.
5. `NULL → PLAYING` luôn đi qua `READY` và `PAUSED`.
6. `NULL → READY` thường acquire hoặc probe device resource.
7. `READY → PAUSED` activate data path, negotiation và preroll preparation.
8. `PAUSED → PLAYING` chọn clock, tính base time và bắt đầu live data.
9. `PLAYING → PAUSED` dừng live data nhưng thường không release camera.
10. `READY → NULL` là transition đóng device theo state contract.
11. Non-live source có thể tạo data trong `PAUSED`.
12. Live source thường không tạo data trong `PAUSED`.
13. Preroll chuẩn bị sink bằng buffer, GAP hoặc EOS phù hợp.
14. `NO_PREROLL` là thành công có semantic live-source.
15. `ASYNC` nghĩa là state change sẽ hoàn tất sau.
16. `FAILURE` cần được giải thích bằng Bus error.
17. `set_state()` phát yêu cầu; `get_state()` xác nhận completion.
18. State change tới `READY` hoặc `NULL` không trả `ASYNC`.
19. Dùng timeout hữu hạn giúp tránh chờ vô hạn.
20. Pipeline truyền state xuống child; application không set từng child tùy ý.
21. Pipeline clock và base time tạo running time chung.
22. Running time đứng yên trong `PAUSED`.
23. Live source timestamp buffer theo pipeline running time.
24. `sync=false` không bỏ qua state machine.
25. GStreamer state chỉ ánh xạ khái niệm với V4L2 lifecycle, không một-một với ioctl.
26. CamStream `build()` tạo graph trong `NULL`.
27. CamStream `start()` yêu cầu `PLAYING` rồi xác nhận current state.
28. CamStream chấp nhận `SUCCESS` hoặc `NO_PREROLL` nếu current state là `PLAYING`.
29. Finite `wait()` và continuous Bus drain không được chạy đồng thời.
30. CamStream `stop()` cố đưa pipeline về `NULL` trước khi unref.
31. Destructor gọi `stop()` làm safety net, nhưng explicit stop vẫn tốt hơn.
32. Sau terminal run, current policy là stop, build và start lại.
33. Framework API MT-safe không thay thế application lifecycle serialization.
34. Reached `PLAYING` không chứng minh FPS, zero-copy hoặc stability.
35. Lifecycle validation pass không đồng nghĩa production readiness.

## 9.3 Nguồn GStreamer chính thức

- [G1 — States design](https://gstreamer.freedesktop.org/documentation/additional/design/states.html)
- [G2 — `GstElement`](https://gstreamer.freedesktop.org/documentation/gstreamer/gstelement.html)
- [G3 — Element states for plugin writers](https://gstreamer.freedesktop.org/documentation/plugin-development/basics/states.html)
- [G4 — Elements and state changes](https://gstreamer.freedesktop.org/documentation/application-development/basics/elements.html)
- [G5 — Live sources](https://gstreamer.freedesktop.org/documentation/additional/design/live-source.html)
- [G6 — Clocking](https://gstreamer.freedesktop.org/documentation/plugin-development/advanced/clock.html)
- [G7 — Clocks and synchronization](https://gstreamer.freedesktop.org/documentation/application-development/advanced/clocks.html)
- [G8 — Synchronisation design](https://gstreamer.freedesktop.org/documentation/additional/design/synchronisation.html)
- [G9 — `GstPipeline`](https://gstreamer.freedesktop.org/documentation/gstreamer/gstpipeline.html)
- [G10 — Latency design](https://gstreamer.freedesktop.org/documentation/additional/design/latency.html)

## 9.4 Nguồn CamStream Control

- [P1 — `GstreamerPipeline` implementation](https://github.com/tungnhs2014/camstream-control/blob/74a6a2045198ecc7c91fe32d48dbe42b52e87faa/libs/camstream-gstreamer/src/gstreamer_pipeline.cpp)
- [P2 — `GstreamerPipeline` interface](https://github.com/tungnhs2014/camstream-control/blob/74a6a2045198ecc7c91fe32d48dbe42b52e87faa/libs/camstream-gstreamer/include/camstream/gstreamer_pipeline.hpp)
- [P3 — Stage 7 GStreamer integration validation](https://github.com/tungnhs2014/camstream-control/blob/74a6a2045198ecc7c91fe32d48dbe42b52e87faa/docs/validation/stage-07-gstreamer-integration.md)
- [P4 — Stage 8.2 service integration validation](https://github.com/tungnhs2014/camstream-control/blob/74a6a2045198ecc7c91fe32d48dbe42b52e87faa/docs/validation/stage-08.2-gstreamer-service-integration.md)

## 9.5 Cầu nối sang Bài 8

Sau Bài 7, application đã biết:

```text
cách yêu cầu pipeline đổi state
cách xác nhận current state
cách đưa pipeline về NULL
```

Nhưng state API không tự trả đầy đủ nguyên nhân runtime.

Ví dụ:

```text
camera bị rút sau khi start
decoder lỗi sau vài frame
finite source gửi EOS
pipeline đổi state bất đồng bộ
```

Các thông tin đó được element post dưới dạng `GstMessage` lên `GstBus`.

Bài 8 sẽ giải thích:

```text
GstBus
GstMessage
EOS
ERROR
WARNING
STATE_CHANGED
ASYNC_DONE
polling
Bus watch
ownership
timeout
clean shutdown
```
