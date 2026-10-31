# Bài 2 — Mô hình đối tượng GStreamer: Plugin, Element, Pad, Bin và Pipeline

> **Lộ trình:** [Bảng đối chiếu lý thuyết ↔ lab](README.md) · [Lab 02](../practice/lesson-02/README.md). Phần CamStream Control là case study của dự án ngoài repo lab; chỉ tiêu chí PASS của lab mới là bằng chứng kiểm chứng tại repo này.

> **Dự án:** CamStream Control
> **Đối tượng:** Người mới học Embedded Linux và GStreamer
> **Phạm vi:** Bài này giải thích cách GStreamer khám phá, tạo, kết nối, tổ chức và sở hữu các thành phần của graph.

---

# 1. Mô hình tạo và tổ chức graph

## 1.1 Mục tiêu bài học

Sau bài này, người học cần:

- phân biệt plugin, `GstRegistry`, `GstPluginFeature`, `GstElementFactory` và `GstElement`;
- hiểu factory name khác instance name;
- hiểu element cung cấp chức năng gì trong graph;
- hiểu `GstPad` và `GstPadTemplate`;
- phân biệt `ALWAYS`, `SOMETIMES` và `REQUEST` pad;
- biết khi nào cần liên kết động, request pad và ghost pad;
- hiểu các API link ở mức element và pad;
- hiểu vì sao phải add element vào bin trước khi link;
- hiểu `GstBin`, `GstPipeline` và quan hệ parent–child;
- quản lý ownership đúng khi thêm hoặc xóa element;
- đọc được cách CamStream Control xây hai graph YUY2 và MJPEG.

Bài này không đi sâu vào:

- nội dung và ownership của `GstBuffer`;
- caps negotiation;
- V4L2 capture workflow;
- state machine;
- xử lý message trên `GstBus`;
- service event loop;
- zero-copy và tối ưu hiệu năng.

Các chủ đề đó thuộc những bài chuyên sâu sau.

## 1.2 Chuỗi đối tượng tổng thể

GStreamer không tạo graph trực tiếp từ một plugin file. Quá trình từ khả năng đã cài đặt đến element đang nằm trong pipeline đi qua nhiều lớp:

```text
GstPlugin
    ↓ đăng ký
GstPluginFeature
    ↓ được quản lý và tra cứu qua
GstRegistry
    ↓ một loại feature là
GstElementFactory
    ↓ tạo
GstElement
    ↓ cung cấp
GstPad
    ↓ được liên kết thành
Graph xử lý
    ↓ được chứa trong
GstBin / GstPipeline
```

Mỗi lớp có một trách nhiệm khác nhau:

| Thành phần | Trách nhiệm |
|---|---|
| `GstPlugin` | Đăng ký một nhóm tính năng |
| `GstRegistry` | Lưu metadata và hỗ trợ tra cứu plugin/feature |
| `GstPluginFeature` | Lớp cơ sở cho tính năng được đăng ký |
| `GstElementFactory` | Mô tả và tạo một loại element |
| `GstElement` | Đối tượng thực hiện chức năng trong graph |
| `GstPad` | Điểm kết nối thực tế của element |
| `GstBin` | Chứa và quản lý một nhóm element |
| `GstPipeline` | `GstBin` cấp cao nhất của graph |

Mental model cần giữ xuyên suốt bài:

```text
Plugin cung cấp khả năng
    ↓
Factory tạo đối tượng
    ↓
Element thực hiện chức năng
    ↓
Pad tạo đường kết nối
    ↓
Bin/Pipeline tổ chức và sở hữu graph
```

---

# 2. Plugin, Registry và Factory của element

## 2.1 `GstPlugin` và `GstPluginFeature`

**Khái niệm `GstPlugin`**

> **`GstPlugin` là đối tượng đại diện cho một plugin đăng ký các tính năng để GStreamer có thể sử dụng.**

Một plugin có thể đăng ký:

- element factories;
- type-find factories;
- device-provider factories;
- các loại `GstPluginFeature` khác.

Plugin động thường được phân phối dưới dạng shared library trong thư mục plugin. GStreamer cũng hỗ trợ plugin được liên kết và đăng ký tĩnh trong ứng dụng.

Vì vậy:

```text
Plugin động
    thường gắn với một file plugin trên hệ thống

Plugin tĩnh
    được liên kết và đăng ký trong chương trình
```

Không nên đồng nhất khái niệm plugin với mọi file `.so`. Một shared library chỉ trở thành GStreamer plugin khi nó có metadata và entry point đăng ký hợp lệ.

**Khái niệm `GstPluginFeature`**

> **`GstPluginFeature` là lớp cơ sở cho các tính năng được plugin đăng ký và Registry quản lý.**

Các ví dụ thường gặp:

```text
GstElementFactory
GstTypeFindFactory
GstDeviceProviderFactory
```

`GstPluginFeature` có rank. Rank được dùng khi GStreamer cần tự động chọn giữa nhiều feature phù hợp, chẳng hạn trong autoplugging.

CamStream Control tạo element trực tiếp bằng factory name:

```cpp
gst_element_factory_make(
    "v4l2src",
    "camera-source");
```

Do đó rank không quyết định element nào được tạo trong graph hiện tại.

## 2.2 `GstRegistry`

**Khái niệm**

> **`GstRegistry` là đối tượng giữ metadata của một tập plugin và cung cấp chức năng tra cứu plugin cùng các feature đã biết.**

Registry giúp GStreamer:

- phát hiện plugin;
- lưu metadata của plugin;
- tra cứu feature theo tên hoặc tiêu chí;
- tránh phải tải và quét lại toàn bộ plugin trong mỗi lần khởi động.

Mô hình:

```text
Plugin được phát hiện hoặc đăng ký
    ↓
Metadata và feature được biết bởi Registry
    ↓
Ứng dụng tra cứu element factory theo tên
```

Registry cache chỉ là chi tiết tối ưu của GStreamer. Ứng dụng không nên tự sửa hoặc phụ thuộc vào định dạng nội bộ của cache.

Cần phân biệt:

```text
GstRegistry
    = nơi quản lý và tra cứu thông tin

GstElementFactory
    = feature có khả năng tạo element
```

Registry không xử lý frame và không chạy trong graph.

## 2.3 `GstElementFactory` và việc tạo element

**Khái niệm `GstElementFactory`**

> **`GstElementFactory` là một `GstPluginFeature` chứa metadata và khả năng tạo các đối tượng thuộc một loại `GstElement`.**

Ví dụ:

```text
Plugin: video4linux2
    ↓ đăng ký
Factory: v4l2src
    ↓ tạo
Đối tượng element: camera-source
```

Một factory có thể tạo nhiều đối tượng:

```text
Factory v4l2src
    ├──→ camera-front
    └──→ camera-rear
```

**Factory name và instance name**

Factory name xác định loại element cần tạo:

```text
v4l2src
jpegdec
videoconvert
fakesink
```

Instance name xác định một đối tượng cụ thể trong graph:

```text
camera-source
jpeg-decoder
video-converter
diagnostic-sink
```

Tên instance rõ ràng giúp:

- đọc log dễ hơn;
- nhận biết nguồn của Bus message;
- tìm element trong bin;
- đọc graph DOT;
- gỡ lỗi khi có nhiều đối tượng cùng loại.

**Ví dụ tạo element**

```cpp
GstElement* source =
    gst_element_factory_make(
        "v4l2src",
        "camera-source");

if (source == nullptr) {
    // Factory không tồn tại hoặc factory không tạo được element.
}
```

`gst_element_factory_make()` là API tiện ích kết hợp bước tìm factory và tạo element.

Ba mức thành công phải được phân biệt:

```text
Plugin file tồn tại
    ≠
Registry tìm thấy factory
    ≠
Factory tạo được element
    ≠
Element chạy được với thiết bị và cấu hình thực tế
```

Ví dụ `v4l2src` được tạo thành công chỉ chứng minh factory có thể tạo đối tượng. Nó chưa chứng minh:

- `/dev/videoX` tồn tại;
- node là node capture phù hợp;
- driver hỗ trợ định dạng yêu cầu;
- pipeline có thể negotiate và chạy.

---

# 3. `GstElement`: thành phần xây dựng graph

## 3.1 Khái niệm và vai trò

**Khái niệm**

> **`GstElement` là thành phần cơ bản thực hiện một chức năng trong graph xử lý GStreamer.**

Element có thể đảm nhiệm các vai trò sau:

| Vai trò trong graph | Ví dụ |
|---|---|
| Tạo hoặc lấy dữ liệu | `videotestsrc`, `v4l2src` |
| Giải mã hoặc chuyển đổi | `jpegdec`, `videoconvert` |
| Phân phối hoặc tách nhánh | `tee` |
| Tạo ranh giới hàng đợi | `queue` |
| Kết hợp nhiều luồng | muxer, aggregator |
| Nhận dữ liệu | `fakesink`, display sink |

Cách phân nhóm này dùng để hỗ trợ tư duy thiết kế graph. Đây không phải danh sách class hierarchy đầy đủ của GStreamer.

Một element không nhất thiết chỉ có một input và một output:

```text
Source element
    có thể chỉ có source pad

Sink element
    có thể chỉ có sink pad

Transform element
    thường có cả sink pad và source pad

Demuxer / tee / muxer
    có thể có nhiều pad
```

## 3.2 Tên, property, signal, pad và state

Một element có nhiều mặt khác nhau:

```text
Name
    = định danh đối tượng

Property
    = cấu hình đối tượng

Signal
    = cơ chế thông báo/callback của GObject

Pad
    = giao diện trao đổi dữ liệu

State
    = trạng thái vòng đời
```

**Property**

Property cấu hình hành vi của một element instance.

Ví dụ:

```cpp
g_object_set(
    G_OBJECT(source),
    "device",
    "/dev/video2",
    nullptr);
```

Property thuộc đối tượng element đã tạo. Tên và kiểu property được định nghĩa bởi loại element.

CamStream kiểm tra property trước khi sử dụng và đọc lại giá trị sau khi cấu hình. Cách làm này giúp phát hiện sớm trường hợp plugin trên target có contract khác với điều ứng dụng yêu cầu.

**Signal**

Signal cho phép element thông báo một sự kiện cho ứng dụng.

Ví dụ dynamic element có thể phát signal:

```text
pad-added
```

khi một pad mới xuất hiện.

Signal của GObject không đồng nghĩa với `GstEvent` và cũng không đồng nghĩa với Unix signal.

**State**

Element mới được tạo không tự động chạy:

```text
gst_element_factory_make()
    = tạo đối tượng

không có nghĩa
    = element đang capture hoặc xử lý dữ liệu
```

Element bắt đầu mở tài nguyên và xử lý dữ liệu theo state transition của chính nó và parent bin/pipeline. State machine được trình bày đầy đủ ở Bài 7.

---

# 4. `GstPad` và `GstPadTemplate`

## 4.1 `GstPad`, hướng dữ liệu và đối tượng đi qua pad

**Khái niệm `GstPad`**

> **`GstPad` là điểm kết nối của một element, qua đó element trao đổi dữ liệu và thông tin điều khiển với element khác.**

Hai hướng pad cơ bản:

```text
GST_PAD_SRC
    = pad đưa dữ liệu ra khỏi element

GST_PAD_SINK
    = pad nhận dữ liệu vào element
```

Ví dụ:

```text
v4l2src.src
    ↓
capsfilter.sink
```

Một link hợp lệ đi từ source pad sang sink pad:

```text
Element A                           Element B
┌──────────────┐                   ┌──────────────┐
│       src pad├──────────────────→│sink pad      │
└──────────────┘                   └──────────────┘
```

**Upstream và downstream**

Hai thuật ngữ này phụ thuộc vào vị trí đang xét.

Trong graph:

```text
v4l2src → jpegdec → videoconvert → fakesink
```

đối với `jpegdec`:

```text
v4l2src
    = upstream

videoconvert và fakesink
    = downstream
```

**Các đối tượng đi qua pad**

| Đối tượng | Vai trò tổng quát |
|---|---|
| `GstBuffer` | Mang dữ liệu media |
| `GstEvent` | Mang thông tin điều khiển hoặc timeline |
| `GstQuery` | Yêu cầu và trả lời thông tin đồng bộ |

`GstMessage` không phải dữ liệu đi qua pad như `GstBuffer`. Message được đưa lên ứng dụng thông qua `GstBus`.

Bài 2 chỉ định vị các đối tượng này. Chi tiết thuộc Bài 3, Bài 5 và Bài 8.

## 4.2 `GstPadTemplate`

**Khái niệm**

> **`GstPadTemplate` mô tả loại pad mà một element có thể cung cấp, gồm tên mẫu, hướng, thời điểm xuất hiện và caps hỗ trợ.**

Pad template cho phép quan sát khả năng của element ngay cả trước khi một pad thực tế được tạo.

Ví dụ một pad template có thể mô tả:

```text
Tên mẫu: src_%u
Hướng: GST_PAD_SRC
Presence: GST_PAD_REQUEST
Caps: video/x-raw
```

Phân biệt:

```text
GstPadTemplate
    = contract ở cấp loại element

GstPad
    = điểm kết nối thực tế của một element instance
```

Template caps mô tả tập định dạng có thể hỗ trợ. Chúng không phải lúc nào cũng là định dạng đang chạy.

```text
Template caps
    = khả năng tổng quát

Current caps
    = định dạng hiện tại sau negotiation
```

Caps và caps negotiation được trình bày đầy đủ ở Bài 5.

Có thể xem pad template của element bằng:

```bash
gst-inspect-1.0 v4l2src
gst-inspect-1.0 jpegdec
```

---

# 5. Các loại pad và graph không cố định

## 5.1 `ALWAYS` pad

> **`GST_PAD_ALWAYS` mô tả pad luôn có mặt trên mỗi element instance theo contract của pad template.**

Ví dụ phổ biến:

```text
videoconvert.sink
videoconvert.src
fakesink.sink
```

Với `ALWAYS` pad, ứng dụng thường có thể link ngay sau khi:

1. tạo element;
2. thêm element vào cùng bin hoặc pipeline;
3. cấu hình property cần thiết.

Ví dụ:

```cpp
if (gst_element_link(source, sink) == FALSE) {
    // Không tạo được link phù hợp.
}
```

Các graph hiện tại của CamStream chỉ dùng những pad có sẵn khi build, vì vậy không cần callback `pad-added`, request pad hoặc ghost pad.

## 5.2 `SOMETIMES` pad và liên kết động

> **`GST_PAD_SOMETIMES` mô tả pad chỉ xuất hiện khi nội dung stream hoặc trạng thái xử lý làm pad đó cần thiết.**

Ví dụ:

```text
demuxer
decodebin
```

Một demuxer chỉ biết stream chứa audio, video hoặc subtitle nào sau khi đọc dữ liệu đầu vào. Khi biết cấu trúc stream, nó tạo pad tương ứng và phát signal `pad-added`.

Workflow:

```text
tạo dynamic element
    ↓
kết nối callback pad-added
    ↓
element phát hiện stream
    ↓
pad mới xuất hiện
    ↓
callback kiểm tra pad
    ↓
link pad với nhánh phù hợp
```

Ví dụ callback ngắn cho video pad:

```cpp
static void on_pad_added(
    GstElement*,
    GstPad* new_pad,
    gpointer user_data) {
    auto* downstream =
        GST_ELEMENT(user_data);

    GstPad* sink_pad =
        gst_element_get_static_pad(
            downstream,
            "sink");

    if (sink_pad == nullptr) {
        return;
    }

    if (gst_pad_is_linked(sink_pad) != FALSE) {
        gst_object_unref(sink_pad);
        return;
    }

    GstCaps* caps =
        gst_pad_get_current_caps(new_pad);

    if (caps == nullptr) {
        caps =
            gst_pad_query_caps(
                new_pad,
                nullptr);
    }

    gboolean is_video = FALSE;

    if (caps != nullptr &&
        gst_caps_is_empty(caps) == FALSE &&
        gst_caps_get_size(caps) > 0U) {
        const GstStructure* structure =
            gst_caps_get_structure(caps, 0);

        const gchar* media_type =
            gst_structure_get_name(structure);

        is_video =
            g_str_has_prefix(
                media_type,
                "video/");
    }

    if (is_video != FALSE) {
        const GstPadLinkReturn result =
            gst_pad_link(
                new_pad,
                sink_pad);

        if (result != GST_PAD_LINK_OK) {
            g_printerr(
                "Không thể link dynamic video pad\n");
        }
    }

    if (caps != nullptr) {
        gst_caps_unref(caps);
    }

    gst_object_unref(sink_pad);
}
```

Kết nối callback:

```cpp
g_signal_connect(
    decodebin,
    "pad-added",
    G_CALLBACK(on_pad_added),
    converter);
```

Những điểm phải hiểu:

- `new_pad` được signal callback cung cấp; callback không tự unref pad này;
- `gst_element_get_static_pad()` trả về một reference và phải unref;
- không link mọi pad mới vào cùng một sink;
- cần kiểm tra loại media hoặc khả năng tương thích;
- cần kiểm tra sink pad đã được link chưa;
- `SOMETIMES` pad do element quản lý vòng đời.

Nếu ứng dụng thêm một element mới vào pipeline đã chạy, element mới không tự động đạt state của parent. Ứng dụng phải đặt state thích hợp hoặc gọi:

```cpp
gst_element_sync_state_with_parent(element);
```

## 5.3 `REQUEST` pad

> **`GST_PAD_REQUEST` mô tả pad chỉ được element tạo khi ứng dụng yêu cầu.**

Các element thường dùng request pad:

```text
tee
muxer
aggregator
```

Ví dụ `tee` có thể tạo nhiều source pad:

```text
              ┌→ queue → hiển thị
input → tee ──┤
              └→ queue → bộ mã hóa
```

Ứng dụng phải quản lý đầy đủ vòng đời:

```text
request
    ↓
link
    ↓
sử dụng
    ↓
ngăn dataflow hoặc dừng pipeline
    ↓
unlink
    ↓
release khỏi element
    ↓
unref reference của caller
```

Ví dụ dưới đây giả định pipeline đã dừng hoặc pad đã được chặn an toàn trước khi unlink:

```cpp
GstPad* tee_src_pad =
    gst_element_request_pad_simple(
        tee,
        "src_%u");

GstPad* queue_sink_pad =
    gst_element_get_static_pad(
        queue,
        "sink");

if (tee_src_pad == nullptr ||
    queue_sink_pad == nullptr) {
    if (tee_src_pad != nullptr) {
        gst_element_release_request_pad(
            tee,
            tee_src_pad);

        gst_object_unref(tee_src_pad);
    }

    if (queue_sink_pad != nullptr) {
        gst_object_unref(queue_sink_pad);
    }

    return false;
}

if (gst_pad_link(
        tee_src_pad,
        queue_sink_pad) !=
    GST_PAD_LINK_OK) {
    gst_element_release_request_pad(
        tee,
        tee_src_pad);

    gst_object_unref(tee_src_pad);
    gst_object_unref(queue_sink_pad);
    return false;
}

/* ... sử dụng nhánh ... */

gst_pad_unlink(
    tee_src_pad,
    queue_sink_pad);

gst_element_release_request_pad(
    tee,
    tee_src_pad);

gst_object_unref(tee_src_pad);
gst_object_unref(queue_sink_pad);
```

Hai thao tác cuối có ý nghĩa khác nhau:

```text
gst_element_release_request_pad()
    = yêu cầu element bỏ pad đã tạo theo request

gst_object_unref()
    = trả reference mà caller đang giữ
```

Chỉ `unref` mà không `release` có thể để element tiếp tục giữ request pad. Chỉ `release` mà không `unref` làm caller rò reference.

`gst_element_link()` có thể tự request pad khi cần. Nếu link đó bị tháo, ứng dụng vẫn phải nhận biết và release request pad đã được tạo.

## 5.4 Ghost pad và giao diện của bin

> **Ghost pad là pad đặt trên `GstBin`, đại diện cho một pad của element con để graph bên ngoài có thể kết nối với graph bên trong bin.**

Không có ghost pad:

```text
Element ngoài
    ──X──> private child pad bên trong bin
```

Có ghost pad:

```text
Element ngoài
    ↓
Ghost pad của bin
    ↓
Pad thật của element con
```

Ví dụ tạo một conversion bin có thể tái sử dụng với ghost sink pad và ghost source pad:

```cpp
static GstElement* create_conversion_bin() {
    GstElement* bin =
        gst_bin_new("conversion-bin");

    GstElement* converter =
        gst_element_factory_make(
            "videoconvert",
            "converter");

    if (bin == nullptr ||
        converter == nullptr) {
        if (converter != nullptr) {
            gst_object_unref(converter);
        }

        if (bin != nullptr) {
            gst_object_unref(bin);
        }

        return nullptr;
    }

    if (gst_bin_add(
            GST_BIN(bin),
            converter) == FALSE) {
        gst_object_unref(converter);
        gst_object_unref(bin);
        return nullptr;
    }

    GstPad* sink_target =
        gst_element_get_static_pad(
            converter,
            "sink");

    GstPad* source_target =
        gst_element_get_static_pad(
            converter,
            "src");

    if (sink_target == nullptr ||
        source_target == nullptr) {
        if (sink_target != nullptr) {
            gst_object_unref(sink_target);
        }

        if (source_target != nullptr) {
            gst_object_unref(source_target);
        }

        gst_object_unref(bin);
        return nullptr;
    }

    GstPad* ghost_sink =
        gst_ghost_pad_new(
            "sink",
            sink_target);

    GstPad* ghost_source =
        gst_ghost_pad_new(
            "src",
            source_target);

    gst_object_unref(sink_target);
    gst_object_unref(source_target);

    if (ghost_sink == nullptr ||
        ghost_source == nullptr) {
        if (ghost_sink != nullptr) {
            gst_object_unref(ghost_sink);
        }

        if (ghost_source != nullptr) {
            gst_object_unref(ghost_source);
        }

        gst_object_unref(bin);
        return nullptr;
    }

    if (gst_element_add_pad(
            bin,
            ghost_sink) == FALSE) {
        gst_object_unref(ghost_sink);
        gst_object_unref(ghost_source);
        gst_object_unref(bin);
        return nullptr;
    }

    if (gst_element_add_pad(
            bin,
            ghost_source) == FALSE) {
        gst_object_unref(ghost_source);
        gst_object_unref(bin);
        return nullptr;
    }

    return bin;
}
```

Các contract quan trọng:

- target pad phải chưa được link khi tạo ghost pad;
- hướng của ghost pad được lấy từ target;
- ghost pad giữ một reference đến target;
- `gst_element_add_pad()` nhận ownership của floating reference khi thành công;
- ghost pad tạo thêm một lớp proxy nên có chi phí xử lý;
- dùng ghost pad khi cần tạo bin tái sử dụng hoặc che giấu graph con.

CamStream hiện chưa cần ghost pad vì graph còn nhỏ và các element được đặt trực tiếp trong pipeline cấp cao nhất.

---

# 6. Linking và xây dựng graph

## 6.1 Khái niệm và quy trình xây graph cố định

**Khái niệm**

> **Linking là việc kết nối source pad của một element với sink pad của element khác để tạo đường đi cho dữ liệu trong graph.**

Quy trình thông thường:

```text
khởi tạo GStreamer
    ↓
tạo pipeline
    ↓
tạo element
    ↓
thêm element vào pipeline
    ↓
cấu hình property
    ↓
link element
    ↓
kiểm tra kết quả
```

Ví dụ:

```cpp
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
    // Xử lý lỗi tạo đối tượng.
}

if (gst_bin_add(
        GST_BIN(pipeline),
        source) == FALSE) {
    // Add thất bại: caller vẫn phải unref source.
}

if (gst_bin_add(
        GST_BIN(pipeline),
        sink) == FALSE) {
    // Add thất bại: caller vẫn phải unref sink.
}

if (gst_element_link(
        source,
        sink) == FALSE) {
    // Không tìm được cặp pad phù hợp để link.
}
```

Element nên được thêm vào bin hoặc pipeline trước khi link. Tài liệu API chính thức yêu cầu điều này đối với các convenience linking functions.

Ngoài ra, `gst_bin_add()` có thể unlink các pad đã được link trước đó khi element được đưa vào bin. Add-before-link tạo hierarchy đúng ngay từ đầu và tránh hành vi khó đoán.

## 6.2 Các API link

| API | Mục đích |
|---|---|
| `gst_element_link()` | Tự chọn một cặp pad phù hợp giữa hai element |
| `gst_element_link_many()` | Link một chuỗi element bằng `gst_element_link()` |
| `gst_element_link_pads()` | Chọn pad cụ thể theo tên |
| `gst_element_link_pads_filtered()` | Chọn pad cụ thể và áp dụng caps filter |
| `gst_element_link_filtered()` | Link hai element với ràng buộc caps |
| `gst_pad_link()` | Link trực tiếp hai `GstPad` |

**`gst_element_link()`**

```cpp
if (gst_element_link(
        source,
        sink) == FALSE) {
    // Link thất bại.
}
```

API này:

- tìm source pad chưa link trên element nguồn;
- tìm sink pad chưa link trên element đích;
- có thể request pad mới nếu cần;
- chỉ tạo một link nếu có nhiều lựa chọn.

**`gst_element_link_many()`**

```cpp
if (gst_element_link_many(
        source,
        capsfilter,
        decoder,
        converter,
        sink,
        nullptr) == FALSE) {
    // Một link trong chuỗi đã thất bại.
}
```

CamStream dùng API này cho graph YUY2 và MJPEG.

**`gst_element_link_pads()`**

Dùng khi element có nhiều pad và ứng dụng biết tên pad cần chọn:

```cpp
gst_element_link_pads(
    source,
    "src",
    sink,
    "sink");
```

**`gst_pad_link()`**

Dùng khi ứng dụng đã lấy trực tiếp hai pad:

```cpp
GstPad* source_pad =
    gst_element_get_static_pad(
        source,
        "src");

GstPad* sink_pad =
    gst_element_get_static_pad(
        sink,
        "sink");

const GstPadLinkReturn result =
    gst_pad_link(
        source_pad,
        sink_pad);

gst_object_unref(source_pad);
gst_object_unref(sink_pad);
```

Mọi API `get_*_pad()` có transfer-full phải được unref sau khi dùng.

## 6.3 Linking, hierarchy và caps negotiation

Linking và caps negotiation là hai bước khác nhau:

```text
Linking
    = tạo kết nối có khả năng tương thích giữa pad

Caps negotiation
    = chọn định dạng media thực tế khi graph vận hành
```

Vì vậy:

```text
gst_element_link() trả TRUE
    ≠
camera đã chấp nhận exact mode
    ≠
pipeline đã negotiate thành công
    ≠
buffer chắc chắn đang chạy
```

Link có thể thành công dựa trên template caps, nhưng khi pipeline chuyển state, negotiation vẫn có thể thất bại.

Ví dụ:

```text
v4l2src template caps hỗ trợ video/x-raw
    ↓
link với capsfilter thành công
    ↓
camera thật không hỗ trợ width/height/framerate yêu cầu
    ↓
khi chạy báo lỗi hoặc `not-negotiated`
```

Chi tiết negotiation thuộc Bài 5.

**Hierarchy**

Hai element phải nằm trong hierarchy phù hợp. Với convenience API, cách an toàn là:

```text
cùng parent bin
hoặc
có quan hệ hierarchy mà API hỗ trợ
```

Không nên truy cập private child pad xuyên qua ranh giới bin. Khi cần expose graph con, dùng ghost pad.

---

# 7. `GstBin`, `GstPipeline` và ownership

## 7.1 `GstBin` và `GstPipeline`

**Khái niệm `GstBin`**

> **`GstBin` là một `GstElement` có khả năng chứa và quản lý các element con như một nhóm.**

Bin cung cấp:

- quan hệ parent–child;
- nhóm graph con thành một đơn vị;
- truyền state tới các child;
- gom và chuyển tiếp message của child;
- khả năng tạo ghost pad;
- khả năng lồng bin.

**Khái niệm `GstPipeline`**

> **`GstPipeline` là một `GstBin` đặc biệt được dùng làm bộ chứa cấp cao nhất của graph xử lý.**

So sánh:

| `GstBin` | `GstPipeline` |
|---|---|
| Có thể nhóm một graph con | Quản lý toàn graph |
| Có thể là child của bin khác | Thường là root |
| Chuyển tiếp message lên parent | Cung cấp `GstBus` cho ứng dụng |
| Dùng ghost pad để expose interface | Quản lý clock toàn graph |

Ứng dụng thường lấy Bus từ pipeline cấp cao nhất, không dùng một bin con như Bus độc lập của ứng dụng.

Khi pipeline bị unref, các child element mà nó sở hữu cũng được dereference, trừ khi nơi khác đang giữ reference riêng.

## 7.2 Ownership khi thêm element

`GstObject` kế thừa floating reference từ `GInitiallyUnowned`. Element mới tạo thường mang floating reference.

Trước khi add thành công:

```text
caller
    = chịu trách nhiệm với element mới tạo
```

Sau khi:

```cpp
gst_bin_add(
    GST_BIN(pipeline),
    element);
```

trả `TRUE`:

```text
bin
    = parent và owner của element

local raw pointer
    = borrowed pointer
```

Pattern phù hợp:

```cpp
static GstElement* create_and_add_element(
    GstElement* pipeline,
    const char* factory_name,
    const char* element_name) {
    GstElement* element =
        gst_element_factory_make(
            factory_name,
            element_name);

    if (element == nullptr) {
        return nullptr;
    }

    if (gst_bin_add(
            GST_BIN(pipeline),
            element) == FALSE) {
        gst_object_unref(element);
        return nullptr;
    }

    // Pipeline đã nhận ownership.
    return element;
}
```

Nếu add thất bại:

```text
element chưa có parent
    ↓
caller phải gst_object_unref(element)
```

Nếu add thành công:

```text
không unref pointer như thể caller vẫn sở hữu reference tạo ban đầu
```

Muốn giữ element độc lập với bin, caller phải lấy thêm reference rõ ràng bằng `gst_object_ref()`.

**Vì sao CamStream không dùng `gst_bin_add_many()`?**

`gst_bin_add_many()` gọi `gst_bin_add()` cho từng element nhưng bỏ qua từng return value.

CamStream thêm từng element:

```cpp
if (gst_bin_add(
        GST_BIN(pipeline),
        element) == FALSE) {
    // Biết chính xác element nào add thất bại.
}
```

Cách này phù hợp với yêu cầu xử lý lỗi rõ ràng và dọn dẹp có tính xác định.

## 7.3 Xóa element và thêm element khi pipeline đang chạy

**Xóa element**

`gst_bin_remove()`:

- bỏ element khỏi parent bin;
- unlink pad của element;
- bỏ reference mà bin đang giữ.

Nếu bin giữ reference cuối cùng, element có thể bị hủy ngay trong lúc remove.

Nếu cần dùng element sau remove:

```cpp
gst_object_ref(element);

if (gst_bin_remove(
        GST_BIN(bin),
        element) != FALSE) {
    // Caller vẫn giữ reference riêng.
}

/* ... sử dụng element ... */

gst_object_unref(element);
```

Một element chỉ có thể thuộc một parent bin tại một thời điểm.

**Thêm element vào pipeline đang chạy**

Khi thêm element vào pipeline đang `PAUSED` hoặc `PLAYING`, parent không tự động đưa element mới đến state hiện tại.

Ứng dụng phải:

```cpp
gst_element_sync_state_with_parent(element);
```

hoặc đặt state theo policy riêng.

Dynamic insertion còn cần xử lý:

- dataflow đang chạy;
- pad blocking;
- link/unlink an toàn;
- rollback khi một bước thất bại.

Bài 2 chỉ nêu contract về hierarchy và state synchronization. Dynamic graph manipulation hoàn chỉnh thuộc bài tích hợp ứng dụng.

---

# 8. Áp dụng vào CamStream Control

## 8.1 Factory, đối tượng và hai graph hiện tại

Mã nguồn branch hiện tại tạo các factory sau:

| Factory name | Instance name | Khi sử dụng |
|---|---|---|
| `v4l2src` | `camera-source` | Mọi input format |
| `capsfilter` | `capture-caps` | Mọi input format |
| `jpegdec` | `jpeg-decoder` | Chỉ MJPEG |
| `videoconvert` | `video-converter` | Chỉ MJPEG |
| `fakesink` | `diagnostic-sink` | Mọi input format |

**YUY2 graph**

```text
camera-source
    ↓
capture-caps
    ↓
diagnostic-sink
```

Tương ứng:

```text
v4l2src
    ↓
capsfilter
    ↓
fakesink
```

**MJPEG graph**

```text
camera-source
    ↓
capture-caps
    ↓
jpeg-decoder
    ↓
video-converter
    ↓
diagnostic-sink
```

Tương ứng:

```text
v4l2src
    ↓
capsfilter
    ↓
jpegdec
    ↓
videoconvert
    ↓
fakesink
```

Các pad dùng trong hai graph đều tồn tại khi build. Dự án hiện:

- không cần `pad-added`;
- không request pad;
- không tạo ghost pad;
- không có custom GStreamer element;
- dùng `camstream-video` như một V4L2 driver trong kernel, không phải plugin GStreamer.

**Quy trình `build()`**

```text
kiểm tra cấu hình
    ↓
gst_init_check()
    ↓
gst_pipeline_new()
    ↓
create_and_add_element()
    ↓
configure properties
    ↓
gst_element_link_many()
    ↓
gst_element_get_bus()
```

Element decoder và converter chỉ được tạo khi input format là MJPEG.

Đây là topology policy của ứng dụng:

```text
YUY2
    = raw path, không cần JPEG decoder

MJPEG
    = compressed path, cần jpegdec
```

Caps và format được giải thích ở Bài 4 và Bài 5.

## 8.2 Tạo graph dở dang và dọn dẹp

Một lần build có thể thất bại giữa chừng:

```text
pipeline đã tạo
    ↓
source đã add
    ↓
capsfilter đã add
    ↓
jpegdec tạo thất bại
```

Trong trường hợp này:

- source và capsfilter đã thuộc pipeline;
- element chưa add thành công vẫn thuộc trách nhiệm caller;
- unref pipeline sẽ dereference các child đã add;
- wrapper phải reset mọi pointer và trạng thái nội bộ;
- quá trình dọn dẹp phải an toàn khi được gọi từ nhiều nhánh lỗi.

CamStream dùng `stop()` làm điểm dọn dẹp tập trung:

```text
partial build failure
    ↓
stop()
    ↓
đưa pipeline về NULL nếu có
    ↓
unref Bus nếu có
    ↓
unref Pipeline
    ↓
reset lifecycle flags
```

Destructor dùng cùng cơ chế dọn dẹp dự phòng và `stop()` có tính idempotent.

Bài 2 tập trung vào ownership của graph. Việc hoàn tất chuyển state và ownership của Bus được trình bày ở Bài 7 và Bài 8.

## 8.3 Ví dụ C++ xây YUY2 graph

Ví dụ sau minh họa đúng chuỗi:

```text
create
→ checked add
→ configure
→ link
→ return owned pipeline
```

```cpp
#include <gst/gst.h>

#include <iostream>

namespace {

GstElement* create_and_add_element(
    GstElement* pipeline,
    const char* factory_name,
    const char* element_name) {
    GstElement* element =
        gst_element_factory_make(
            factory_name,
            element_name);

    if (element == nullptr) {
        std::cerr
            << "Không thể tạo factory '"
            << factory_name
            << "'\n";
        return nullptr;
    }

    if (gst_bin_add(
            GST_BIN(pipeline),
            element) == FALSE) {
        std::cerr
            << "Không thể thêm element '"
            << element_name
            << "' vào pipeline\n";

        gst_object_unref(element);
        return nullptr;
    }

    // Pipeline đã nhận ownership.
    return element;
}

GstElement* build_yuy2_pipeline(
    const char* device_path,
    int width,
    int height,
    int fps) {
    GstElement* pipeline =
        gst_pipeline_new(
            "yuy2-pipeline");

    if (pipeline == nullptr) {
        return nullptr;
    }

    GstElement* source =
        create_and_add_element(
            pipeline,
            "v4l2src",
            "camera-source");

    GstElement* capsfilter =
        create_and_add_element(
            pipeline,
            "capsfilter",
            "capture-caps");

    GstElement* sink =
        create_and_add_element(
            pipeline,
            "fakesink",
            "diagnostic-sink");

    if (source == nullptr ||
        capsfilter == nullptr ||
        sink == nullptr) {
        gst_object_unref(pipeline);
        return nullptr;
    }

    g_object_set(
        G_OBJECT(source),
        "device",
        device_path,
        nullptr);

    g_object_set(
        G_OBJECT(sink),
        "sync",
        FALSE,
        nullptr);

    GstCaps* caps =
        gst_caps_new_simple(
            "video/x-raw",
            "format",
            G_TYPE_STRING,
            "YUY2",
            "width",
            G_TYPE_INT,
            width,
            "height",
            G_TYPE_INT,
            height,
            "framerate",
            GST_TYPE_FRACTION,
            fps,
            1,
            nullptr);

    if (caps == nullptr) {
        gst_object_unref(pipeline);
        return nullptr;
    }

    g_object_set(
        G_OBJECT(capsfilter),
        "caps",
        caps,
        nullptr);

    gst_caps_unref(caps);

    if (gst_element_link_many(
            source,
            capsfilter,
            sink,
            nullptr) == FALSE) {
        std::cerr
            << "Không thể link YUY2 graph\n";

        gst_object_unref(pipeline);
        return nullptr;
    }

    return pipeline;
}

} // namespace

int main(int argc, char* argv[]) {
    GError* initialization_error = nullptr;

    if (gst_init_check(
            &argc,
            &argv,
            &initialization_error) == FALSE) {
        std::cerr
            << "Không thể khởi tạo GStreamer";

        if (initialization_error != nullptr) {
            std::cerr
                << ": "
                << initialization_error->message;

            g_error_free(
                initialization_error);
        }

        std::cerr << '\n';
        return 1;
    }

    GstElement* pipeline =
        build_yuy2_pipeline(
            "/dev/video2",
            640,
            480,
            30);

    if (pipeline == nullptr) {
        return 1;
    }

    /*
     * Bài 7 và Bài 8 sẽ bổ sung:
     * - state transition;
     * - GstBus;
     * - EOS/ERROR;
     * - shutdown có kiểm soát.
     */

    gst_object_unref(pipeline);
    return 0;
}
```

Ownership trong ví dụ:

```text
pipeline
    = reference do caller của build_yuy2_pipeline() sở hữu

source / capsfilter / sink
    = child do pipeline sở hữu sau successful add

caps
    = reference cục bộ, unref sau khi set property
```

Ví dụ dừng trước state và Bus để giữ đúng phạm vi Bài 2.

---

# 9. Kiểm tra, tổng kết và nguồn tham khảo

## 9.1 Dùng `gst-inspect-1.0` và gỡ lỗi theo tầng

`gst-inspect-1.0` in thông tin về plugin hoặc element:

```bash
gst-inspect-1.0 v4l2src
gst-inspect-1.0 capsfilter
gst-inspect-1.0 jpegdec
gst-inspect-1.0 videoconvert
gst-inspect-1.0 fakesink
```

Khi đọc output, tập trung vào:

1. **Plugin Details**
   Element thuộc plugin nào.

2. **Factory Details**
   Factory name, metadata và rank.

3. **Element Hierarchy**
   Loại element kế thừa từ class nào.

4. **Pad Templates**
   Tên pad, hướng, presence và template caps.

5. **Element Properties**
   Property name, kiểu dữ liệu, giá trị mặc định và quyền đọc/ghi.

6. **Element Signals**
   Signal mà ứng dụng có thể kết nối.

Ví dụ kiểm tra pad presence:

```text
Pad Templates:
  SRC template: 'src'
    Availability: Always

  SINK template: 'sink'
    Availability: Always
```

Danh sách kiểm tra khi gỡ lỗi:

| Hiện tượng | Kiểm tra trọng tâm |
|---|---|
| Factory tạo thất bại | Plugin có được cài và Registry có thấy factory không? |
| `gst_bin_add()` thất bại | Element đã có parent chưa? Bin có hợp lệ không? |
| Static link thất bại | Hướng pad, hierarchy và template caps |
| Link thành công nhưng chạy lỗi | Caps negotiation và thiết bị thực tế |
| Dynamic pad không link | Callback, media type, sink pad đã link chưa |
| Request pad còn tồn tại | Đã unlink, release và unref chưa |
| Không kết nối được graph con | Bin đã có ghost pad phù hợp chưa |
| Element mới không chạy | Đã sync state với parent chưa |

Không nên suy luận quá mức:

```text
gst-inspect thấy factory
    ≠
pipeline chạy thành công

link thành công
    ≠
negotiation thành công

pipeline build thành công
    ≠
frame đã được capture
```

## 9.2 Kiến thức cần nhớ và nguồn tham khảo

**Mô hình cần nhớ**

```text
Plugin
    ↓ đăng ký feature
Registry
    ↓ tra cứu
Factory của element
    ↓ tạo
Element
    ↓ cung cấp pad
Link
    ↓ tạo graph
Bin / Pipeline
    ↓ tổ chức và sở hữu graph
```

Các ý trọng tâm:

1. Plugin đăng ký feature; Registry quản lý metadata và hỗ trợ tra cứu.
2. `GstElementFactory` là feature dùng để tạo element; factory name khác instance name.
3. Tạo được element không có nghĩa element đã chạy hoặc thiết bị đã hoạt động.
4. Element trao đổi dữ liệu qua source pad và sink pad.
5. `GstPadTemplate` mô tả khả năng tạo pad; `GstPad` là pad thực tế.
6. `ALWAYS`, `SOMETIMES` và `REQUEST` mô tả cách pad xuất hiện.
7. Dynamic pad cần callback, kiểm tra loại media và tránh link lặp.
8. Request pad phải được unlink an toàn, release khỏi element và unref reference của caller.
9. `gst_element_link()` có thể tự request pad; ứng dụng vẫn phải quản lý pad đó khi tháo link.
10. Ghost pad đưa giao diện của child element ra ranh giới bin.
11. Element nên được add vào hierarchy phù hợp trước khi link.
12. Link thành công không chứng minh caps negotiation hoặc thiết bị đã hoạt động.
13. `GstBin` sở hữu child sau khi add thành công; remove có thể hủy child nếu không còn reference khác.
14. Element thêm vào pipeline đang chạy phải được đồng bộ state với parent.
15. CamStream dùng graph cố định với `ALWAYS` pads và add từng element để kiểm tra chính xác lỗi.

**Tài liệu GStreamer chính thức**

- [G1 — `GstPlugin`](https://gstreamer.freedesktop.org/documentation/gstreamer/gstplugin.html)
- [G2 — `GstRegistry`](https://gstreamer.freedesktop.org/documentation/gstreamer/gstregistry.html)
- [G3 — `GstPluginFeature`](https://gstreamer.freedesktop.org/documentation/gstreamer/gstpluginfeature.html)
- [G4 — `GstElementFactory`](https://gstreamer.freedesktop.org/documentation/gstreamer/gstelementfactory.html)
- [G5 — Elements](https://gstreamer.freedesktop.org/documentation/application-development/basics/elements.html)
- [G6 — `GstElement`](https://gstreamer.freedesktop.org/documentation/gstreamer/gstelement.html)
- [G7 — Pads and capabilities](https://gstreamer.freedesktop.org/documentation/application-development/basics/pads.html)
- [G8 — `GstPad`](https://gstreamer.freedesktop.org/documentation/gstreamer/gstpad.html)
- [G9 — `GstPadTemplate`](https://gstreamer.freedesktop.org/documentation/gstreamer/gstpadtemplate.html)
- [G10 — Dynamic pipelines](https://gstreamer.freedesktop.org/documentation/tutorials/basic/dynamic-pipelines.html)
- [G11 — Request and Sometimes pads](https://gstreamer.freedesktop.org/documentation/plugin-development/advanced/request.html)
- [G12 — `GstGhostPad`](https://gstreamer.freedesktop.org/documentation/gstreamer/gstghostpad.html)
- [G13 — `GstObject` ownership](https://gstreamer.freedesktop.org/documentation/gstreamer/gstobject.html)
- [G14 — `GstBin`](https://gstreamer.freedesktop.org/documentation/gstreamer/gstbin.html)
- [G15 — Bins](https://gstreamer.freedesktop.org/documentation/application-development/basics/bins.html)
- [G16 — `GstPipeline`](https://gstreamer.freedesktop.org/documentation/gstreamer/gstpipeline.html)
- [G17 — `gst-inspect-1.0`](https://gstreamer.freedesktop.org/documentation/tools/gst-inspect.html)

**CamStream Control**

- [P1 — `GstreamerPipeline` implementation](https://github.com/tungnhs2014/camstream-control/blob/74a6a2045198ecc7c91fe32d48dbe42b52e87faa/libs/camstream-gstreamer/src/gstreamer_pipeline.cpp)
- [P2 — `GstreamerPipeline` public interface](https://github.com/tungnhs2014/camstream-control/blob/74a6a2045198ecc7c91fe32d48dbe42b52e87faa/libs/camstream-gstreamer/include/camstream/gstreamer_pipeline.hpp)
- [P3 — Stage 7 GStreamer integration validation](https://github.com/tungnhs2014/camstream-control/blob/74a6a2045198ecc7c91fe32d48dbe42b52e87faa/docs/validation/stage-07-gstreamer-integration.md)
- [P4 — Stage 8.2 service integration validation](https://github.com/tungnhs2014/camstream-control/blob/74a6a2045198ecc7c91fe32d48dbe42b52e87faa/docs/validation/stage-08.2-gstreamer-service-integration.md)
