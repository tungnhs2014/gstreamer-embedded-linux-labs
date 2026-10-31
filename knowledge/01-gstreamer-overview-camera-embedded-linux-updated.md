# Bài 1 — Tổng quan GStreamer trong kiến trúc Camera Embedded Linux

> **Lộ trình:** [Bảng đối chiếu lý thuyết ↔ lab](README.md) · [Lab 01](../practice/lesson-01/README.md). Phần CamStream Control là case study của dự án ngoài repo lab; chỉ tiêu chí PASS của lab mới là bằng chứng kiểm chứng tại repo này.

> **Dự án:** CamStream Control
> **Đối tượng:** Người mới học Embedded Linux và GStreamer
> **Phạm vi:** Bài này cung cấp bức tranh tổng quan. Các cơ chế chuyên sâu được trình bày ở những bài sau.

---

# 1. Bài toán camera và vai trò của GStreamer

## 1.1 Mục tiêu bài học

Sau bài này, người học cần:

- hiểu GStreamer là gì;
- phân biệt graph xử lý media với `GstPipeline`;
- nhận biết vai trò tổng quát của plugin, element, pad, caps, buffer, state và Bus;
- hiểu vị trí của GStreamer trong Linux camera stack;
- đọc được một pipeline đơn giản bằng `gst-launch-1.0`;
- hình dung cách tạo pipeline bằng API;
- hiểu cách CamStream Control đóng gói và tái sử dụng pipeline GStreamer;
- phân biệt chế độ chạy hữu hạn với chế độ service liên tục;
- phân biệt đường GStreamer hiện tại với đường Camera PPI;
- phân biệt kết quả kiểm tra chức năng với các tuyên bố về hiệu năng và độ ổn định.

Bài này chưa đi sâu vào:

- plugin registry và ownership;
- `GstBuffer`, `GstMemory` và reference counting;
- định dạng video thô và nén;
- quá trình thương lượng caps;
- hàng đợi buffer của V4L2;
- state machine;
- xử lý message trên Bus;
- tích hợp pipeline vào event loop của service.

Những nội dung đó được mở rộng trong các bài chuyên sâu sau.

## 1.2 Một sản phẩm camera cần làm gì?

Ở mức thấp, một chương trình camera có thể chỉ cần:

```text
Mở thiết bị video
    ↓
Cấu hình định dạng
    ↓
Nhận frame
```

Nhưng một sản phẩm camera thực tế thường cần nhiều bước hơn:

```text
Thu nhận hình ảnh từ camera
    ↓
Giải mã nếu dữ liệu đang nén
    ↓
Chuyển đổi định dạng ảnh
    ↓
Xử lý hình ảnh
    ↓
Mã hóa
    ↓
Hiển thị / ghi file / truyền qua mạng
```

Ngoài dữ liệu hình ảnh, hệ thống còn phải quản lý:

- định dạng media giữa các khâu;
- luồng dữ liệu liên tục;
- vòng đời của các thành phần;
- thời gian và đồng bộ;
- nhiều nhánh xử lý;
- lỗi và kết thúc luồng;
- giải phóng tài nguyên.

Tự xây toàn bộ các phần này bằng code riêng sẽ làm ứng dụng nhanh chóng phức tạp. GStreamer cung cấp một mô hình chung để tổ chức chúng thành graph xử lý media.

**Vì sao dùng GStreamer?**

GStreamer giúp ứng dụng:

- tái sử dụng các thành phần xử lý media có sẵn;
- ghép các thành phần thành graph;
- thay đổi graph mà không viết lại toàn bộ chương trình;
- điều khiển vòng đời graph thông qua state;
- nhận thông báo từ graph qua `GstBus`;
- mở rộng từ capture đơn giản sang giải mã, chuyển đổi, mã hóa, ghi dữ liệu hoặc truyền media qua mạng.

GStreamer không thay thế trình điều khiển camera trong kernel hoặc V4L2. Trong Linux camera stack, nó sử dụng các giao diện thiết bị có sẵn, chẳng hạn `v4l2src` sử dụng V4L2 để lấy dữ liệu camera.

---

# 2. GStreamer, graph và `GstPipeline`

## 2.1 GStreamer và graph xử lý media

**Khái niệm GStreamer**

> **GStreamer là một framework đa phương tiện dùng để xây dựng các ứng dụng xử lý luồng media bằng graph gồm các element được kết nối với nhau.**

Định nghĩa này có ba thành phần chính:

- **framework đa phương tiện:** cung cấp kiến trúc, API và các thành phần cho audio, video và những loại dữ liệu media khác;
- **luồng media:** dữ liệu được xử lý liên tục theo thời gian hoặc thứ tự;
- **graph xử lý:** các thành phần được kết nối theo đường đi của dữ liệu.

Ví dụ một luồng video:

```text
Frame 0 → Frame 1 → Frame 2 → Frame 3 → ...
```

Từ “streaming” trong GStreamer không chỉ có nghĩa là truyền qua mạng. Đọc camera, phát video từ file hoặc xử lý audio liên tục đều là các trường hợp streaming media.

**Khái niệm graph xử lý media**

> **Graph xử lý media là cấu trúc gồm các element và các liên kết giữa chúng, mô tả đường đi và các bước xử lý của dữ liệu media.**

Mô hình tuyến tính cơ bản:

```text
Source → Processing → Sink
```

Trong đó:

- **source** tạo hoặc lấy dữ liệu;
- **element xử lý** xử lý hoặc chuyển đổi dữ liệu;
- **sink** nhận dữ liệu ở cuối một nhánh.

Ví dụ:

```text
Nguồn camera → Bộ giải mã JPEG → Bộ chuyển đổi video → Sink hiển thị
```

`Source → Processing → Sink` chỉ là mô hình cơ bản. Graph thực tế có thể phân nhánh:

```text
                    ┌→ Hiển thị
Camera → tee ───────┤
                    └→ Bộ mã hóa → Mạng
```

hoặc hội tụ:

```text
Video ─┐
       ├→ Muxer → Tệp
Audio ─┘
```

Trong GStreamer:

```text
Thành phần trong graph
    = GstElement

Điểm kết nối
    = GstPad

Định dạng media
    = GstCaps

Dữ liệu media
    = GstBuffer
```

Graph là cách mô tả cấu trúc liên kết. Nó không phải một object riêng đồng nghĩa với `GstPipeline`.

## 2.2 `GstPipeline` và `GstBin`

**Khái niệm `GstPipeline`**

> **`GstPipeline` là một `GstBin` đặc biệt được sử dụng làm bộ chứa cấp cao nhất cho graph xử lý media.**

Phân biệt:

```text
Graph
    = cấu trúc các element được liên kết

GstPipeline
    = đối tượng cấp cao nhất chứa và quản lý graph
```

`GstPipeline` có các trách nhiệm tổng quát:

- chứa các element và bin;
- truyền thay đổi state xuống các phần tử con;
- cung cấp `GstBus` cho ứng dụng;
- lựa chọn và phân phối clock dùng để đồng bộ graph.

Ví dụ:

```text
GstPipeline
┌────────────────────────────────────────┐
│ v4l2src → capsfilter → jpegdec → sink │
└────────────────────────────────────────┘
```

Ứng dụng tạo và điều khiển `GstPipeline`; dữ liệu media được các element xử lý trong graph bên trong.

**Quan hệ giữa `GstBin` và `GstPipeline`**

> **`GstBin` là một element có thể chứa và quản lý một nhóm các element khác như một đơn vị.**

Ví dụ một phân hệ camera có thể được nhóm thành bin:

```text
CameraBin
┌─────────────────────────────────┐
│ v4l2src → capsfilter → decoder │
└─────────────────────────────────┘
```

`GstPipeline` kế thừa khả năng chứa element của `GstBin`, nhưng được thiết kế làm bộ chứa cấp cao nhất. Một `GstBin` thông thường không tự cung cấp Bus và quản lý phân phối clock như `GstPipeline`.

Ở Bài 1 chỉ cần nhớ:

```text
GstBin
    = nhóm một graph con

GstPipeline
    = GstBin cấp cao nhất quản lý toàn graph
```

---

# 3. Các thành phần cốt lõi

Phần này chỉ giới thiệu vai trò tổng quan. Mỗi thành phần sẽ được giải thích sâu hơn ở các bài sau.

## 3.1 Plugin, factory và element

**Plugin**

> **Plugin là mô-đun mở rộng đăng ký các tính năng để GStreamer có thể sử dụng khi chương trình đang chạy.**

Một plugin có thể cung cấp:

- các factory của element;
- typefind functions;
- device providers;
- các loại tính năng khác.

Ví dụ plugin `video4linux2` cung cấp element `v4l2src`.

**Element factory**

> **Element factory là thông tin đăng ký dùng để tạo các đối tượng của một loại `GstElement`.**

Ví dụ:

```text
Tên factory: v4l2src
    ↓ tạo
Đối tượng element: camera-source
```

**`GstElement`**

> **`GstElement` là thành phần cơ bản dùng để xây dựng graph xử lý trong GStreamer.**

Mỗi element đảm nhiệm một chức năng cụ thể.

| Element | Vai trò |
|---|---|
| `videotestsrc` | Tạo video kiểm tra |
| `v4l2src` | Lấy video từ V4L2 device |
| `jpegdec` | Giải mã JPEG thành raw video |
| `videoconvert` | Chuyển đổi định dạng video thô khi cần |
| `fakesink` | Nhận và loại bỏ dữ liệu |

Mối quan hệ tổng quát:

```text
Plugin
    ↓ đăng ký
Element factory
    ↓ tạo
GstĐối tượng element
    ↓ được thêm vào
GstPipeline
```

Registry, factory lookup, instance ownership và plugin lifecycle được trình bày ở Bài 2.

## 3.2 Pad và link

**`GstPad`**

> **`GstPad` là giao diện kết nối của một element với các element khác.**

Hai hướng cơ bản:

```text
Source pad
    = dữ liệu đi ra khỏi element

Sink pad
    = dữ liệu đi vào element
```

Ví dụ:

```text
Element A                           Element B
┌──────────────┐                   ┌──────────────┐
│       src pad├──────────────────→│sink pad      │
└──────────────┘       link        └──────────────┘
```

**Link**

> **Link là kết nối giữa source pad của một element và sink pad của element tiếp theo.**

Khi các element được link, graph được hình thành và dữ liệu có đường để di chuyển.

Bài 2 sẽ mở rộng:

- pad templates;
- always, sometimes và request pads;
- dynamic pads;
- ghost pads;
- điều kiện link;
- ownership của element và bin.

## 3.3 `GstCaps` và `GstBuffer`

**`GstCaps`**

> **`GstCaps` là đối tượng mô tả một tập các định dạng media mà một pad có thể xử lý hoặc định dạng đã được chọn cho luồng dữ liệu.**

Ví dụ:

```text
video/x-raw,
format=YUY2,
width=640,
height=480,
framerate=30/1
```

Caps có thể mô tả:

- loại media;
- định dạng pixel;
- độ phân giải;
- tốc độ khung hình;
- các thuộc tính định dạng khác.

Trong Bài 1, chỉ cần hiểu:

```text
GstCaps
    = hợp đồng định dạng của luồng media
```

Caps negotiation được trình bày đầy đủ ở Bài 5.

**`GstBuffer`**

> **`GstBuffer` là đối tượng mang dữ liệu media và thông tin thời gian khi dữ liệu đi qua graph.**

Ví dụ với camera:

```text
GstBuffer 0 → chứa frame 0
GstBuffer 1 → chứa frame 1
GstBuffer 2 → chứa frame 2
```

Buffer có thể tham chiếu đến memory chứa payload và mang các thông tin như timestamp, flags hoặc metadata.

`GstBuffer`, `GstMemory`, ownership, reference counting và buffer pool được trình bày ở Bài 3.

## 3.4 State và `GstBus`

**State**

> **State biểu diễn trạng thái vòng đời hiện tại của một element hoặc pipeline.**

Bốn state chính:

```text
NULL → READY → PAUSED → PLAYING
```

Ở mức tổng quan:

- `NULL`: chưa giữ tài nguyên vận hành;
- `READY`: đã sẵn sàng ở mức thiết bị hoặc tài nguyên cơ bản;
- `PAUSED`: graph đã được chuẩn bị cho dataflow;
- `PLAYING`: graph đang vận hành theo clock và chính sách của pipeline.

Ý nghĩa chính xác của từng chuyển đổi, source trực tiếp và chuyển state bất đồng bộ được trình bày ở Bài 7.

**`GstBus`**

> **`GstBus` là kênh chuyển `GstMessage` từ pipeline và các element lên ứng dụng.**

Các message thường gặp:

- lỗi;
- cảnh báo;
- kết thúc luồng;
- thay đổi state.

Luồng điều khiển:

```text
Element / Pipeline
    ↓ gửi GstMessage
GstBus
    ↓
Application
```

Dữ liệu video không đi qua Bus. Dữ liệu media đi qua pad dưới dạng buffer; Bus dành cho thông báo điều khiển và trạng thái.

Cách chờ, đọc, phân tích và giải phóng Bus message được trình bày ở Bài 8.

---

# 4. Ví dụ GStreamer cơ bản

## 4.1 Pipeline không phụ thuộc camera

Lệnh sau tạo 100 frame kiểm tra rồi gửi chúng đến `fakesink`:

```bash
gst-launch-1.0 \
    videotestsrc num-buffers=100 ! \
    videoconvert ! \
    fakesink
```

Ý nghĩa:

```text
videotestsrc
    = source element tạo dữ liệu video kiểm tra

videoconvert
    = element xử lý hỗ trợ chuyển đổi định dạng video thô

fakesink
    = sink element nhận và loại bỏ dữ liệu

!
    = yêu cầu liên kết các thành phần trong cú pháp mô tả pipeline
```

Graph:

```text
videotestsrc → videoconvert → fakesink
```

`gst-launch-1.0` phù hợp để thử và quan sát pipeline; ứng dụng sản phẩm thường tạo graph bằng API.

Ví dụ này phù hợp để kiểm tra:

- GStreamer hoạt động trên target;
- các factory của element tồn tại;
- pipeline có thể tạo, chạy, nhận EOS và dừng;
- không phụ thuộc camera hoặc V4L2.

## 4.2 Pipeline camera V4L2

Ví dụ raw YUY2:

```bash
gst-launch-1.0 \
    v4l2src device=/dev/videoX num-buffers=300 ! \
    "video/x-raw,format=YUY2,width=640,height=480,framerate=30/1" ! \
    fakesink sync=false
```

Graph:

```text
v4l2src
    ↓
YUY2 640×480 tại 30/1
    ↓
fakesink
```

Trong đó:

- `v4l2src` mở node thiết bị video qua V4L2;
- phần caps ràng buộc định dạng mong muốn;
- `num-buffers=300` tạo một lần chạy hữu hạn;
- `fakesink` nhận dữ liệu nhưng không hiển thị;
- `sync=false` làm sink không chờ theo pipeline clock.

`/dev/videoX` là giá trị đại diện. Sản phẩm không nên giả định một số node cố định như `/dev/video0`; node phải được phát hiện và xác thực theo thiết bị, capabilities và định dạng cần dùng.

Ví dụ camera MJPEG:

```bash
gst-launch-1.0 \
    v4l2src device=/dev/videoX num-buffers=300 ! \
    "image/jpeg,width=640,height=480,framerate=30/1" ! \
    jpegdec ! \
    videoconvert ! \
    fakesink sync=false
```

Graph:

```text
v4l2src
    ↓ image/jpeg
jpegdec
    ↓ video/x-raw
videoconvert
    ↓
fakesink
```

## 4.3 Ví dụ tạo pipeline bằng API

Ví dụ C++ dưới đây sử dụng API C của GStreamer để tạo một graph nhỏ:

```cpp
#include <gst/gst.h>

#include <iostream>

int main(int argc, char* argv[]) {
    GError* initialization_error = nullptr;

    if (gst_init_check(&argc, &argv, &initialization_error) == FALSE) {
        std::cerr << "Không thể khởi tạo GStreamer";

        if (initialization_error != nullptr) {
            std::cerr << ": " << initialization_error->message;
            g_error_free(initialization_error);
        }

        std::cerr << '\n';
        return 1;
    }

    GstElement* pipeline =
        gst_pipeline_new("example-pipeline");

    GstElement* source =
        gst_element_factory_make(
            "videotestsrc",
            "test-source");

    GstElement* sink =
        gst_element_factory_make(
            "fakesink",
            "test-sink");

    if (pipeline == nullptr ||
        source == nullptr ||
        sink == nullptr) {
        std::cerr << "Không thể tạo pipeline hoặc element\n";

        if (source != nullptr) {
            gst_object_unref(source);
        }

        if (sink != nullptr) {
            gst_object_unref(sink);
        }

        if (pipeline != nullptr) {
            gst_object_unref(pipeline);
        }

        return 1;
    }

    g_object_set(
        G_OBJECT(source),
        "num-buffers",
        100,
        nullptr);

    if (gst_bin_add(
            GST_BIN(pipeline),
            source) == FALSE) {
        std::cerr << "Không thể thêm source vào pipeline\n";
        gst_object_unref(source);
        gst_object_unref(sink);
        gst_object_unref(pipeline);
        return 1;
    }

    if (gst_bin_add(
            GST_BIN(pipeline),
            sink) == FALSE) {
        std::cerr << "Không thể thêm sink vào pipeline\n";
        gst_object_unref(sink);
        gst_object_unref(pipeline);
        return 1;
    }

    if (gst_element_link(source, sink) == FALSE) {
        std::cerr << "Không thể liên kết các element\n";
        gst_object_unref(pipeline);
        return 1;
    }

    GstBus* bus =
        gst_element_get_bus(pipeline);

    if (bus == nullptr) {
        std::cerr << "Không thể lấy GstBus\n";
        gst_object_unref(pipeline);
        return 1;
    }

    if (gst_element_set_state(
            pipeline,
            GST_STATE_PLAYING) ==
        GST_STATE_CHANGE_FAILURE) {
        std::cerr << "Pipeline không thể chạy\n";

        gst_element_set_state(
            pipeline,
            GST_STATE_NULL);

        gst_object_unref(bus);
        gst_object_unref(pipeline);
        return 1;
    }

    const auto message_types =
        static_cast<GstMessageType>(
            GST_MESSAGE_ERROR |
            GST_MESSAGE_EOS);

    GstMessage* message =
        gst_bus_timed_pop_filtered(
            bus,
            GST_CLOCK_TIME_NONE,
            message_types);

    bool success = true;

    if (message != nullptr &&
        GST_MESSAGE_TYPE(message) ==
            GST_MESSAGE_ERROR) {
        GError* error = nullptr;
        gchar* debug_information = nullptr;

        gst_message_parse_error(
            message,
            &error,
            &debug_information);

        std::cerr << "Pipeline lỗi: "
                  << (error != nullptr
                          ? error->message
                          : "không rõ nguyên nhân")
                  << '\n';

        g_clear_error(&error);
        g_free(debug_information);
        success = false;
    }

    gst_element_set_state(
        pipeline,
        GST_STATE_NULL);

    if (message != nullptr) {
        gst_message_unref(message);
    }

    gst_object_unref(bus);
    gst_object_unref(pipeline);

    return success ? 0 : 1;
}
```

Luồng chính trong code:

```text
gst_init_check()
    ↓
gst_pipeline_new()
    ↓
gst_element_factory_make()
    ↓
gst_bin_add()
    ↓
gst_element_link()
    ↓
gst_element_set_state(PLAYING)
    ↓
đọc EOS hoặc ERROR từ GstBus
    ↓
gst_element_set_state(NULL)
    ↓
giải phóng tài nguyên
```

Ví dụ này nhằm giúp người học nhìn thấy các khái niệm trong code. Ownership chi tiết, state completion và cách xử lý Bus dành cho sản phẩm thực tế được trình bày ở các bài chuyên sâu.

---

# 5. GStreamer trong Linux camera stack

## 5.1 Kiến trúc và vai trò của từng tầng

**Kiến trúc tổng thể**

Đường đi tổng quát của một USB camera:

```text
Phần cứng camera
    ↓
Đường truyền USB
    ↓
uvcvideo
    ↓
V4L2
    ↓
/dev/videoX
    ↓
v4l2src
    ↓
GStreamer graph
    ↓
Ứng dụng
```

Mỗi tầng có trách nhiệm riêng.

**Vai trò của từng thành phần**

**`uvcvideo`**

> **`uvcvideo` là kernel driver của Linux dành cho các camera tuân theo USB Video Class.**

Trình điều khiển giao tiếp với USB camera, quản lý truyền dữ liệu ở kernel và đăng ký thiết bị video qua V4L2.

**V4L2**

> **V4L2 là Linux userspace API dành cho thiết bị videos.**

V4L2 cung cấp các giao diện chuẩn để userspace:

- query capabilities;
- enumerate và cấu hình format;
- điều khiển streaming;
- trao đổi capture buffers;
- truy cập camera controls.

V4L2 là giao diện thiết bị. Nó không tổ chức graph decode, convert, encode hoặc truyền media qua mạng như GStreamer.

**`/dev/videoX`**

> **`/dev/videoX` là node video thiết bị qua đó userspace truy cập một chức năng V4L2.**

Một thiết bị vật lý có thể tạo nhiều node, chẳng hạn:

- thu nhận hình ảnh;
- thu nhận metadata;
- output;
- những chức năng khác.

Số `X` phụ thuộc quá trình probe và không phải định danh ổn định của camera.

**`v4l2src`**

> **`v4l2src` là GStreamer source element sử dụng V4L2 để capture dữ liệu từ một node video thiết bị.**

Nó là cầu nối:

```text
V4L2 device contract
    ↓
GStreamer source element
    ↓
GstBuffer trong graph
```

Quy trình chi tiết như format configuration, I/O mode, QBUF, DQBUF và buffer pool thuộc Bài 6.

## 5.2 Vì sao dự án dùng cả V4L2 và GStreamer?

Hai công nghệ giải quyết hai tầng khác nhau:

```text
V4L2
    = giao tiếp với thiết bị video

GStreamer
    = tổ chức graph xử lý media
```

Chương trình V4L2 trực tiếp phù hợp cho:

- đưa driver vào hoạt động ban đầu;
- kiểm tra capabilities và format;
- kiểm tra buffer queue;
- cô lập lỗi ở tầng thiết bị;
- đo capture trực tiếp.

GStreamer phù hợp cho:

- nối camera với decoder, converter, encoder và sink;
- xây nhiều nhánh xử lý;
- quản lý pipeline;
- tích hợp hiển thị, ghi file hoặc network;
- tái sử dụng plugin.

Vì vậy, native V4L2 application vẫn có giá trị ngay cả khi product sử dụng GStreamer:

```text
Chương trình V4L2 trực tiếp
    = công cụ kiểm tra tầng thiết bị

GStreamer
    = tầng tích hợp multimedia
```

---

# 6. Áp dụng vào CamStream Control

## 6.1 Kiến trúc và graph hiện tại

**Đường GStreamer đã được tích hợp trên thiết bị đích**

```text
Logitech C270
    ↓
USB
    ↓
uvcvideo
    ↓
V4L2
    ↓
v4l2src
    ↓
camstream-gstreamer
    ↓
camstream-service hoặc camstream-gst-test
```

Dự án cũng có một nguồn V4L2 tổng hợp:

```text
camstream-video
    ↓
V4L2
    ↓
v4l2src
    ↓
camstream-gstreamer
    ↓
camstream-service hoặc camstream-gst-test
```

`camstream-video` là driver V4L2 chạy trong kernel. Nó không phải plugin hoặc element GStreamer tùy biến. Hai graph hiện tại chỉ sử dụng các element chuẩn như `v4l2src`, `capsfilter`, `jpegdec`, `videoconvert` và `fakesink`.

Driver tổng hợp giúp kiểm tra phần tích hợp phía trên mà không phụ thuộc:

- camera vật lý;
- cáp USB và nguồn;
- đường truyền UVC;
- hành vi riêng của camera.

Kiểm tra bằng driver tổng hợp không thay thế kiểm tra với camera thật. Hai đường kiểm tra loại trừ những nhóm nguyên nhân khác nhau.

**Một phần triển khai pipeline dùng chung**

```text
camstream-gst-test ──┐
                     ├──→ camstream-gstreamer
camstream-service ───┘             ↓
                            GstreamerPipeline
                                  ↓
                         GstPipeline + GstBus
```

- `camstream-gst-test` là chương trình chẩn đoán chạy hữu hạn;
- `camstream-service` là service chạy trực tiếp ở foreground;
- `camstream-gstreamer` chứa phần triển khai pipeline dùng chung;
- service không chứa một bản sao thứ hai của mã nguồn xây pipeline.

**Ranh giới với Camera PPI**

Stage 8.3 bổ sung một đường kiến trúc riêng:

```text
camstream-camera-test
    ↓
CameraSession
    ↓
Camera PPI
    ↓
backend mô phỏng
```

Đường Camera PPI này mới được kiểm tra trên máy phát triển. Nó chưa được nối với `camstream-gstreamer` hoặc `camstream-service`, chưa có V4L2 backend và chưa có libcamera backend.

Vì vậy kiến trúc hiện tại không được mô tả thành:

```text
GStreamer → Camera PPI → V4L2
```

Đường GStreamer trên thiết bị đích và đường Camera PPI trên máy phát triển hiện vẫn là hai đường độc lập.

**Hai graph hiện tại**

Mã nguồn hiện tại tạo các element bằng factory, thêm từng element vào pipeline, cấu hình thuộc tính, link graph rồi lấy Bus từ pipeline. [P1]

**Raw YUY2 path**

```text
v4l2src
    ↓
capsfilter(video/x-raw, format=YUY2)
    ↓
fakesink
```

Vai trò:

- `v4l2src`: lấy dữ liệu từ V4L2;
- `capsfilter`: giới hạn format, độ phân giải và tốc độ khung hình;
- `fakesink`: xác nhận luồng dữ liệu mà không cần màn hình.

Trong V4L2, camera/driver dùng tên định dạng pixel `YUYV`; GStreamer biểu diễn định dạng đóng gói tương ứng bằng tên `YUY2`.

**MJPEG path**

```text
v4l2src
    ↓
capsfilter(image/jpeg)
    ↓
jpegdec
    ↓
videoconvert
    ↓
fakesink
```

Vai trò:

- `v4l2src`: lấy các frame JPEG đã nén;
- `jpegdec`: giải mã JPEG thành video thô;
- `videoconvert`: hỗ trợ tương thích định dạng video thô cho phần tử phía sau;
- `fakesink`: nhận dữ liệu cuối graph.

Sự hiện diện của `videoconvert` không tự chứng minh rằng một phép chuyển đổi format luôn xảy ra; format thực tế phụ thuộc quá trình thương lượng caps. Chi tiết này thuộc Bài 4 và Bài 5.

## 6.2 Hai chế độ vận hành pipeline

`GstreamerPipeline` dùng chung các bước:

```text
build()
    ↓
start()
    ↓
tiêu thụ GstBus theo một trong hai chế độ
    ↓
stop()
```

**`build()`**

- kiểm tra và lưu cấu hình cho một lần chạy;
- tạo `GstPipeline` và các element;
- cấu hình source, caps và sink;
- thêm và link các element;
- lấy `GstBus`.

**`start()`**

- yêu cầu pipeline chuyển sang `GST_STATE_PLAYING`;
- kiểm tra pipeline có thực sự đạt trạng thái chạy hay không.

**Chế độ hữu hạn**

Khi:

```text
buffer_count > 0
```

lớp `GstreamerPipeline` đặt giới hạn `num-buffers` cho `v4l2src`.

```text
build()
    ↓
start()
    ↓
wait() chờ EOS hoặc ERROR
    ↓
stop()
```

Đây là chế độ được `camstream-gst-test` sử dụng để kiểm tra một số lượng buffer xác định. EOS là kết thúc bình thường được mong đợi.

**Chế độ service liên tục**

Khi:

```text
buffer_count == 0
```

source giữ chế độ chạy không giới hạn.

```text
build()
    ↓
lấy file descriptor của GstBus dùng cho `poll()`
    ↓
start()
    ↓
service chờ signal và trạng thái sẵn sàng của Bus
    ↓
đọc message bằng API GstBus
    ↓
stop()
```

`camstream-service` chờ đồng thời signal descriptor và Bus descriptor trong một vòng lặp sự kiện do ứng dụng sở hữu. Descriptor của `GstBus` chỉ dùng để báo trạng thái sẵn sàng; service không được `read()` hoặc `close()` descriptor đó.

`wait()` và việc service tự lấy hết các message đang chờ trên Bus là hai cách tiêu thụ cùng một Bus. Chúng không được chạy đồng thời trên một pipeline.

**`stop()`**

- yêu cầu pipeline về `GST_STATE_NULL`;
- giải phóng Bus và pipeline;
- an toàn khi gọi lặp lại;
- xử lý cả trường hợp build hoặc start chỉ hoàn thành một phần.

Bài 1 chỉ giới thiệu hai chế độ ở mức kiến trúc. Chuyển đổi state, ownership của message và cách tích hợp Bus vào `poll()` được trình bày trong các bài chuyên sâu.

## 6.3 Phạm vi bằng chứng hiện tại

**Stage 7 — Tích hợp GStreamer**

Các đường chức năng sau đã được xác nhận trên BeagleBone Black:

- node V4L2 của driver tổng hợp được `v4l2src` mở thành công;
- YUY2 640×480 tại 30/1 được thương lượng thành công trên driver tổng hợp;
- nhiều lần chạy hữu hạn đạt EOS và trở về `NULL`;
- C270 raw YUY2 320×240 tại 30/1 hoạt động;
- C270 MJPEG 640×480 tại 30/1 đi qua `jpegdec` và `videoconvert`;
- thư viện C++ tạo được cả hai graph và xử lý vòng đời hữu hạn. [P2]

**Stage 8.2 — Tích hợp GStreamer vào service**

Các điểm sau đã được kiểm tra trong phạm vi kiểm tra chức năng trên máy phát triển, Buildroot và BeagleBone Black:

- `camstream-service` sở hữu một `GstreamerPipeline`;
- service và ứng dụng chẩn đoán dùng chung `camstream-gstreamer`;
- hỗ trợ chế độ hữu hạn và chế độ liên tục;
- signal và Bus được tích hợp vào một vòng lặp sự kiện dùng `poll()`;
- EOS, warning, error và thay đổi state quan trọng được xử lý;
- tín hiệu dừng đưa pipeline về `GST_STATE_NULL`;
- quá trình dọn dẹp có tính xác định và an toàn khi gọi lặp lại. [P3]

**Stage 8.3 — Camera PPI**

Camera PPI core, backend mô phỏng và ứng dụng chẩn đoán đã được kiểm tra trên host trong phạm vi Stage 8.3. Chúng chưa được tích hợp vào GStreamer, Buildroot, BeagleBone Black hoặc Raspberry Pi. [P4]

Các kết quả trên chứng minh **tích hợp chức năng trong phạm vi đã ghi nhận**.

Dự án chưa tuyên bố:

- tốc độ thực tế ổn định 30 fps;
- độ ổn định USB trong thời gian dài;
- zero-copy;
- hỗ trợ crop;
- khôi phục sau khi tháo/cắm lại thiết bị;
- truyền media qua mạng ở mức sản phẩm.

Một giá trị caps:

```text
framerate=30/1
```

chỉ biểu diễn định dạng danh nghĩa đã được thương lượng. Nó không tự chứng minh ứng dụng thực tế luôn nhận đủ 30 frame mỗi giây.

Tương tự:

```text
pipeline chạy thành công
```

không tự chứng minh:

```text
zero-copy
```

hoặc:

```text
độ tin cậy ở mức sản phẩm
```

Mọi kết luận phải bám theo bằng chứng đã đo và phạm vi kiểm tra thực tế.

---

# 7. Tổng kết và lộ trình học tiếp

## 7.1 Mô hình cần nhớ

```text
Ứng dụng
    ↓ tạo và điều khiển
GstPipeline
    ↓ chứa
Graph các GstElement
    ↓ kết nối qua
GstPad
    ↓ trao đổi
GstBuffer theo GstCaps
```

Trong Linux camera stack:

```text
Camera
    ↓
Trình điều khiển trong kernel
    ↓
V4L2
    ↓
v4l2src
    ↓
GStreamer graph
    ↓
Ứng dụng
```

Các ý trọng tâm:

1. GStreamer là framework đa phương tiện xây graph bằng các element.
2. Graph là cấu trúc liên kết của các element.
3. `GstPipeline` là `GstBin` cấp cao nhất chứa và quản lý graph.
4. Plugin đăng ký factory; factory tạo một đối tượng element.
5. Element kết nối với nhau qua pad.
6. `GstCaps` mô tả định dạng; `GstBuffer` mang dữ liệu.
7. State điều khiển vòng đời; Bus chuyển message lên ứng dụng.
8. V4L2 cung cấp giao diện thiết bị video cho userspace.
9. `v4l2src` đưa dữ liệu capture từ V4L2 vào GStreamer.
10. `camstream-gst-test` và `camstream-service` dùng chung thư viện `camstream-gstreamer`.
11. Pipeline có thể chạy hữu hạn để kiểm tra hoặc chạy liên tục trong service.
12. Camera PPI hiện là một đường riêng, chưa nằm trong đường GStreamer.
13. Kiểm tra chức năng đạt không tự chứng minh hiệu năng, zero-copy hoặc độ tin cậy ở mức sản phẩm.

## 7.2 Nội dung được mở rộng ở các bài sau

| Bài | Nội dung |
|---:|---|
| Bài 2 | Plugin, registry, factory, element, pad, link, bin và pipeline |
| Bài 3 | `GstBuffer`, `GstMemory`, ownership và buffer lifecycle |
| Bài 4 | Video thô, YUYV/YUY2 và MJPEG |
| Bài 5 | `GstCaps` và quá trình thương lượng caps |
| Bài 6 | V4L2, `v4l2src` và quy trình capture camera |
| Bài 7 | State machine và vòng đời của live source |
| Bài 8 | `GstBus`, `GstMessage` và xử lý lỗi |
| Bài 9 | Tích hợp pipeline vào ứng dụng và service C++ |

Bài 1 chỉ xây bản đồ tổng quan. Mỗi bài tiếp theo sẽ mở rộng một nhóm khái niệm theo đúng phạm vi, tránh lặp lại hoặc trộn nhiều tầng kiến thức.

## 7.3 Nguồn tham khảo

**Tài liệu GStreamer chính thức**

- [GStreamer — What is GStreamer?](https://gstreamer.freedesktop.org/documentation/plugin-development/introduction/preface.html)
- [GStreamer Application Development Manual — Foundations](https://gstreamer.freedesktop.org/documentation/application-development/introduction/basics.html)
- [`GstElement` API Reference](https://gstreamer.freedesktop.org/documentation/gstreamer/gstelement.html)
- [Pads and capabilities](https://gstreamer.freedesktop.org/documentation/application-development/basics/pads.html)
- [`GstBin` API Reference](https://gstreamer.freedesktop.org/documentation/gstreamer/gstbin.html)
- [`GstPipeline` API Reference](https://gstreamer.freedesktop.org/documentation/gstreamer/gstpipeline.html)
- [Buffers and Events](https://gstreamer.freedesktop.org/documentation/application-development/basics/data.html)
- [Bus](https://gstreamer.freedesktop.org/documentation/application-development/basics/bus.html)
- [`GstBus` API Reference](https://gstreamer.freedesktop.org/documentation/gstreamer/gstbus.html)
- [`v4l2src` Element Reference](https://gstreamer.freedesktop.org/documentation/video4linux2/v4l2src.html)

**Tài liệu Linux chính thức**

- [Linux Kernel Documentation — Video for Linux API version 2](https://docs.kernel.org/userspace-api/media/v4l/v4l2.html)
- [Linux Kernel Documentation — V4L2 Input/Output](https://docs.kernel.org/userspace-api/media/v4l/io.html)

**CamStream Control**

- [P1 — GStreamer pipeline implementation](https://github.com/tungnhs2014/camstream-control/blob/74a6a2045198ecc7c91fe32d48dbe42b52e87faa/libs/camstream-gstreamer/src/gstreamer_pipeline.cpp)
- [P2 — Báo cáo kiểm tra tích hợp GStreamer Stage 7](https://github.com/tungnhs2014/camstream-control/blob/74a6a2045198ecc7c91fe32d48dbe42b52e87faa/docs/validation/stage-07-gstreamer-integration.md)
- [P3 — Báo cáo tích hợp GStreamer vào service Stage 8.2](https://github.com/tungnhs2014/camstream-control/blob/74a6a2045198ecc7c91fe32d48dbe42b52e87faa/docs/validation/stage-08.2-gstreamer-service-integration.md)
- [P4 — Tổng quan kiến trúc hệ thống và Camera PPI](https://github.com/tungnhs2014/camstream-control/blob/74a6a2045198ecc7c91fe32d48dbe42b52e87faa/docs/architecture/system-overview.md)
- [P5 — Public interface của `GstreamerPipeline`](https://github.com/tungnhs2014/camstream-control/blob/74a6a2045198ecc7c91fe32d48dbe42b52e87faa/libs/camstream-gstreamer/include/camstream/gstreamer_pipeline.hpp)
