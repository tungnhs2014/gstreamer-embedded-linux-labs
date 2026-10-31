# Bài 5 — `GstCaps` và quá trình thương lượng định dạng

> **Lộ trình:** [Bảng đối chiếu lý thuyết ↔ lab](README.md) · [Lab 05](../practice/lesson-05/README.md). Phần CamStream Control là case study của dự án ngoài repo lab; chỉ tiêu chí PASS của lab mới là bằng chứng kiểm chứng tại repo này.

> **Dự án:** CamStream Control
> **Đối tượng:** Người mới học Embedded Linux và GStreamer
> **Phạm vi:** Bài này giải thích cách GStreamer mô tả định dạng media, giới hạn khả năng của graph và chọn một format cụ thể để các element trao đổi dữ liệu.

---

# 1. Vai trò của caps trong graph

## 1.1 Mục tiêu và phạm vi bài học

Bài 4 đã giải thích payload YUY2 và MJPEG được biểu diễn như thế nào. Bài 5 trả lời câu hỏi tiếp theo:

```text
Các element làm cách nào để thống nhất
payload đang mang format nào?
```

Sau bài này, người học cần:

- hiểu khái niệm `GstCaps`;
- phân biệt `GstCaps`, `GstStructure` và `GstCapsFeatures`;
- hiểu simple caps, fixed caps, `ANY` và `EMPTY`;
- phân biệt template caps, queried caps, allowed caps và current caps;
- hiểu requested caps chỉ là ràng buộc của ứng dụng;
- phân biệt linking với caps negotiation;
- hiểu quy tắc “element phía sau đề xuất, element phía trước quyết định” trong push-mode;
- hiểu vai trò của CAPS query, fixation, CAPS event, ACCEPT_CAPS query và RECONFIGURE event;
- hiểu `capsfilter` giới hạn format nhưng không chuyển đổi payload;
- tạo, đọc và kiểm tra caps đúng ownership;
- debug lỗi `not-negotiated` theo từng tầng;
- hiểu cách CamStream Control tạo caps YUY2 và MJPEG;
- phân biệt requested, negotiated và delivered frame rate;
- không suy luận zero-copy hoặc hiệu năng chỉ từ caps.

Bài này không đi sâu vào:

| Nội dung | Bài mở rộng |
|---|---:|
| Cấu hình format trực tiếp bằng V4L2 ioctl | Bài 6 |
| V4L2 buffer queue và I/O mode | Bài 6 |
| State transition làm phát sinh negotiation | Bài 7 |
| Bus message và xử lý lỗi ở application | Bài 8 |
| Allocation negotiation chi tiết | Bài về buffer/application integration |

## 1.2 Vì sao graph cần format contract?

Xét graph:

```text
v4l2src
    ↓
jpegdec
    ↓
videoconvert
    ↓
fakesink
```

Các element không thể chỉ truyền một vùng byte mà không thống nhất ý nghĩa của vùng byte đó.

`jpegdec` cần biết input là:

```text
image/jpeg
```

Sau giải mã, element phía sau cần biết output là raw video, ví dụ:

```text
video/x-raw,
format=I420,
width=640,
height=480,
framerate=30/1
```

Nếu hai pad không thống nhất format:

```text
Element phía trước tạo payload theo contract A
    ↓
Element phía sau diễn giải theo contract B
    ↓
dataflow không hợp lệ
```

GStreamer dùng `GstCaps` để mô tả và thương lượng contract đó.

Caps có thể mô tả:

- media type;
- raw hoặc compressed domain;
- pixel format;
- width và height;
- frame rate;
- colorimetry;
- chroma site;
- interlace mode;
- memory requirement qua caps features;
- các field riêng của từng media type.

Caps không chứa payload frame. Payload nằm trong `GstBuffer`.

---

# 2. Cấu trúc của `GstCaps`

## 2.1 Khái niệm `GstCaps`

> **`GstCaps` là đối tượng nhẹ có reference counting, dùng để mô tả một tập các media format.**

`GstCaps` kế thừa `GstMiniObject`.

Quản lý lifetime bằng:

```cpp
gst_caps_ref(caps);
gst_caps_unref(caps);
```

Không dùng:

```cpp
gst_object_unref(caps); // Sai type.
```

Một caps có thể mô tả:

```text
một format cụ thể
```

hoặc:

```text
một tập format có thể chấp nhận
```

Ví dụ format cụ thể:

```text
video/x-raw,
format=YUY2,
width=640,
height=480,
framerate=30/1
```

Ví dụ tập khả năng:

```text
video/x-raw,
format={YUY2,NV12},
width=[320,1280],
height=[240,720],
framerate={15/1,30/1}
```

Caps là format description:

```text
GstCaps
    = dữ liệu có ý nghĩa gì

GstBuffer
    = dữ liệu media cụ thể
```

## 2.2 `GstStructure` và field có kiểu

Một normal `GstCaps` chứa một hoặc nhiều entry. Mỗi entry gồm:

```text
GstStructure
+
GstCapsFeatures
```

Mô hình:

```text
GstCaps
    ├── GstStructure 0 + GstCapsFeatures 0
    ├── GstStructure 1 + GstCapsFeatures 1
    └── ...
```

> **`GstStructure` là cấu trúc gồm một tên và các field có kiểu.**

Ví dụ:

```text
Tên structure:
video/x-raw

Các field:
format     = YUY2
width      = 640
height     = 480
framerate = 30/1
```

Tên structure thường chính là media type:

```text
video/x-raw
image/jpeg
audio/x-raw
video/x-h264
```

Field có thể chứa:

| Loại giá trị | Ví dụ |
|---|---|
| Giá trị đơn | `width=640` |
| String | `format=YUY2` |
| Fraction | `framerate=30/1` |
| Range | `width=[320,1920]` |
| List | `format={YUY2,NV12}` |
| Array | Một nhóm giá trị được hiểu như một đơn vị |

Một caps có nhiều structure khi nó biểu diễn nhiều lựa chọn format.

Ví dụ:

```text
video/x-raw,format=YUY2;
video/x-raw,format=NV12;
image/jpeg
```

Mỗi structure là một lựa chọn khác nhau trong tập caps.

## 2.3 Simple caps, fixed caps, `ANY`, `EMPTY` và features

**Simple caps**

> **Simple caps là caps chứa đúng một `GstStructure`.**

Simple caps chưa chắc fixed:

```text
video/x-raw,
width=[320,1920]
```

Caps trên có một structure nhưng vẫn chứa range.

**Fixed caps**

> **Fixed caps là caps biểu diễn đúng một media format cụ thể, không còn list, range hoặc lựa chọn chưa được quyết định.**

Format đã thương lượng trên pad phải là fixed caps.

Ví dụ:

```text
video/x-raw,
format=YUY2,
width=640,
height=480,
framerate=30/1
```

Kiểm tra:

```cpp
if (gst_caps_is_fixed(caps) == FALSE) {
    // Caps vẫn còn nhiều lựa chọn.
}
```

**`ANY` caps**

```text
ANY
    = tương thích với mọi media format
```

Tạo bằng:

```cpp
GstCaps* caps =
    gst_caps_new_any();
```

`ANY` không có nghĩa một element thực sự xử lý đúng mọi payload. Nó thường được dùng khi pad hoặc element không cần giới hạn media type tại vị trí đó.

**`EMPTY` caps**

```text
EMPTY
    = không chứa media format nào
```

Tạo bằng:

```cpp
GstCaps* caps =
    gst_caps_new_empty();
```

Intersection trả `EMPTY` nghĩa là hai tập caps không có format chung.

**`GstCapsFeatures`**

> **`GstCapsFeatures` bổ sung yêu cầu ngoài các field của một `GstStructure`, thường liên quan loại memory hoặc metadata bắt buộc.**

Ví dụ khái niệm:

```text
video/x-raw(memory:DMABuf)
```

Hai structure có cùng tên và field nhưng features không tương thích thì vẫn không tương thích.

Nếu structure không khai báo feature, GStreamer xem nó tương đương yêu cầu system memory.

Caps features `ANY` có nghĩa chấp nhận mọi feature phù hợp. Caps chứa `ANY` features vẫn chưa fixed vì negotiation phải chọn một feature cụ thể.

Bài 5 chỉ giới thiệu features để người học không nhầm:

```text
video/x-raw
```

và:

```text
video/x-raw(memory:DMABuf)
```

là cùng một contract. Chi tiết allocator, DMA-BUF và zero-copy thuộc bài buffer/V4L2 nâng cao.

---

# 3. Những loại caps xuất hiện trong graph

## 3.1 Template caps, queried caps, allowed caps và current caps

Các tên dưới đây mô tả caps trong những ngữ cảnh khác nhau. Chúng không phải các subclass khác nhau của `GstCaps`.

**Template caps**

Template caps nằm trên `GstPadTemplate`.

```text
Pad template caps
    = toàn bộ loại media mà loại pad có thể hỗ trợ
```

Có thể xem bằng:

```bash
gst-inspect-1.0 v4l2src
gst-inspect-1.0 jpegdec
gst-inspect-1.0 videoconvert
```

Template caps thuộc loại element, không phản ánh toàn bộ giới hạn runtime của một thiết bị cụ thể.

Ví dụ `v4l2src` có pad template rất rộng, nhưng camera C270 đang kết nối chỉ hỗ trợ một tập mode cụ thể.

**Queried caps**

```text
Queried caps
    = khả năng mà pad báo có thể xử lý hoặc tạo tại thời điểm query
```

Lấy bằng:

```cpp
GstCaps* possible_caps =
    gst_pad_query_caps(
        pad,
        nullptr);
```

`gst_pad_query_caps()` trả full reference. Code gọi phải `gst_caps_unref()`.

Queried caps có thể phụ thuộc:

- pad template;
- property của element;
- caps của peer;
- trạng thái nội bộ;
- thiết bị hiện tại;
- filter caps truyền vào query.

Queried caps không phải current caps.

**Allowed caps**

```text
Allowed caps
    = tập caps còn có thể dùng sau khi xét khả năng của pad và peer
```

Nó thường là một subset của template caps.

Allowed caps vẫn có thể:

- có nhiều structure;
- có range;
- có list;
- chưa fixed.

**Current caps**

```text
Current caps
    = format hiện đang được thiết lập trên pad sau negotiation
```

Lấy bằng:

```cpp
GstCaps* current_caps =
    gst_pad_get_current_caps(pad);
```

Hàm trả full reference hoặc `nullptr` khi pad chưa có current caps.

Current caps của dataflow đã negotiation phải là fixed caps.

## 3.2 Requested caps khác current caps

“Requested caps” là cách gọi ở tầng ứng dụng hoặc dự án:

```text
Requested caps
    = constraint mà application muốn graph sử dụng
```

Ví dụ CamStream yêu cầu:

```text
video/x-raw,
format=YUY2,
width=640,
height=480,
framerate=30/1
```

và đặt constraint này vào `capsfilter`.

Requested caps không tự động trở thành current caps.

Luồng:

```text
Ứng dụng tạo requested caps
    ↓
capsfilter giới hạn tập format
    ↓
các pad thương lượng
    ↓
một fixed format được chọn
    ↓
current caps xuất hiện trên pad
```

Nếu requested caps không giao với khả năng thực tế:

```text
intersection = EMPTY
    ↓
negotiation thất bại
```

CamStream kiểm tra capsfilter đã lưu đúng property `caps`, nhưng việc property giữ đúng giá trị chỉ chứng minh:

```text
capsfilter giữ đúng requested constraint
```

Nó chưa chứng minh:

```text
pipeline đã negotiation thành công
```

Negotiation chỉ được chứng minh khi pipeline chạy và current caps hoặc runtime evidence xác nhận format đã chọn.

## 3.3 Caps khác allocation contract

Caps mô tả:

```text
payload có format gì
```

Allocation negotiation mô tả:

```text
buffer và memory được cấp phát như thế nào
```

Ví dụ hai pad có thể thống nhất:

```text
video/x-raw,
format=NV12,
width=1280,
height=720
```

nhưng vẫn cần thống nhất:

- buffer pool;
- allocator;
- buffer size;
- alignment;
- metadata;
- memory type.

Do đó:

```text
caps negotiation thành công
    ≠
zero-copy đã được thiết lập
```

Caps features có thể đưa memory requirement vào format contract, nhưng allocation query vẫn xử lý các chi tiết cấp phát tiếp theo.

---

# 4. Caps negotiation

## 4.1 Khái niệm và ba quy tắc cơ bản

> **Caps negotiation là quá trình chọn một media format mà các element ở hai phía của link đều có thể xử lý.**

Trong push-mode thông thường, GStreamer dùng ba quy tắc chính:

1. Element phía sau đề xuất format thông qua kết quả CAPS query trên sink pad.
2. Element phía trước quyết định format và gửi fixed caps xuống dưới bằng CAPS event trên source pad.
3. Element phía sau có thể gửi RECONFIGURE event ngược lên để yêu cầu thương lượng lại.

Mô hình:

```text
Element phía trước
    │
    │ CAPS query xuống phía sau
    ▼
Element phía sau đề xuất khả năng
    │
    │ kết quả CAPS query đi ngược lên
    ▼
Element phía trước chọn và fixate format
    │
    │ CAPS event đi xuống
    ▼
Các element cấu hình lại cho format đã chọn
```

Câu ngắn cần nhớ:

```text
Element phía sau đề xuất
Element phía trước quyết định
```

Quy tắc này mô tả negotiation push-mode ở mức tổng quát. Một số element có behavior riêng cho fixed, transform hoặc dynamic negotiation.

## 4.2 CAPS query, intersection và fixation

**CAPS query**

CAPS query hỏi:

```text
Pad có thể xử lý hoặc tạo những caps nào?
```

Query có thể mang một filter để giảm tập kết quả.

```cpp
GstCaps* result =
    gst_pad_query_caps(
        pad,
        filter);
```

Filter là borrowed input; result là full reference.

**Intersection**

Caps intersection tìm phần format chung.

Ví dụ:

```text
Source:
video/x-raw,
format={YUY2,NV12},
width=[320,1280]

Sink:
video/x-raw,
format=NV12,
width=640
```

Intersection:

```text
video/x-raw,
format=NV12,
width=640
```

Kiểm tra nhanh:

```cpp
const gboolean compatible =
    gst_caps_can_intersect(
        source_caps,
        sink_caps);
```

Hàm không nhận ownership của hai caps.

Nếu intersection rỗng:

```text
không có format chung
```

**Fixation**

Intersection có thể vẫn còn range hoặc list:

```text
video/x-raw,
format={YUY2,NV12},
width=[320,1280],
framerate={15/1,30/1}
```

Fixation chọn một giá trị cụ thể:

```text
video/x-raw,
format=YUY2,
width=640,
framerate=30/1
```

`gst_caps_fixate()` nhận ownership của caps đầu vào và trả full reference của caps đã fixate. Không được tiếp tục dùng reference đầu vào sau khi gọi nếu chưa lấy thêm reference riêng.

Fixation không tự chứng minh thiết bị phần cứng đã cấu hình được mode. Element nguồn vẫn phải áp dụng format được chọn vào backend thực tế.

## 4.3 CAPS event, ACCEPT_CAPS query và RECONFIGURE event

**CAPS event**

CAPS event đi downstream và mang fixed caps đã được chọn.

```text
CAPS event
    = thông báo format hiện tại của dataflow
```

Element nhận event phải:

- kiểm tra format;
- cấu hình xử lý nội bộ;
- lưu current caps nếu chấp nhận;
- chuyển event tiếp theo contract của element.

CAPS event không mang payload frame.

**ACCEPT_CAPS query**

```text
ACCEPT_CAPS
    = kiểm tra nhanh một caps cụ thể có thể được pad chấp nhận không
```

Nó trả lời câu hỏi:

```text
Caps này có thể chấp nhận không?
```

Nó không thay thế toàn bộ negotiation và không tự thiết lập current caps.

**RECONFIGURE event**

RECONFIGURE event đi upstream:

```text
Element phía sau
    ↓ muốn format khác hoặc constraint thay đổi
RECONFIGURE
    ↑
Element phía trước
```

Element phía trước nhận yêu cầu và có thể bắt đầu negotiation lại.

RECONFIGURE không tự mang format mới. Nó chỉ báo upstream cần xem xét lại format.

## 4.4 Fixed, transform và dynamic negotiation

Plugin writer documentation phân biệt ba trường hợp phổ biến.

**Fixed negotiation**

Source chỉ có thể tạo một format cụ thể.

Ví dụ:

- typefinder đã xác định loại stream;
- demuxer đọc format từ container;
- source bị giới hạn bởi phần cứng.

Source gửi fixed caps mà nó thực sự tạo được.

**Transform negotiation**

Output caps phụ thuộc input caps và khả năng chuyển đổi của element.

Ví dụ:

```text
videoconvert
videoscale
audioresample
```

Element chuyển tập caps ở một pad thành tập khả năng ở pad còn lại.

**Dynamic negotiation**

Element có thể tạo nhiều output format và chọn theo đề xuất downstream.

Ví dụ converter có thể chọn output format phù hợp với sink.

Application developer thường không tự cài đặt các hàm negotiation này, nhưng phải hiểu chúng để:

- đặt capsfilter đúng vị trí;
- thêm converter khi cần;
- debug graph không có format chung.

---

# 5. `capsfilter` và filtered linking

## 5.1 Khái niệm và vai trò của `capsfilter`

> **`capsfilter` là element truyền dữ liệu mà không thay đổi payload, đồng thời giới hạn caps được phép đi qua.**

Graph:

```text
source
    ↓
capsfilter
    ↓
downstream
```

Property chính:

```text
caps
```

Nếu property là `ANY`, capsfilter không thêm giới hạn format.

Nếu property là:

```text
video/x-raw,
format=YUY2,
width=640,
height=480,
framerate=30/1
```

chỉ format giao với constraint này mới được phép negotiation qua element.

`capsfilter` không:

- giải mã JPEG;
- chuyển YUY2 thành NV12;
- resize frame;
- thay đổi frame rate bằng thuật toán;
- cấu hình zero-copy.

Muốn chuyển đổi, graph cần element phù hợp:

| Yêu cầu | Element thường dùng |
|---|---|
| Raw pixel format | `videoconvert` |
| Kích thước video | `videoscale` |
| Frame rate | `videorate` |
| JPEG → raw | `jpegdec` |

Ví dụ đúng:

```text
jpegdec
→ videoconvert
→ capsfilter caps=video/x-raw,format=RGB
```

`videoconvert` tạo khả năng chuyển format; `capsfilter` chọn output contract mong muốn.

## 5.2 Inline caps, explicit `capsfilter` và `gst_element_link_filtered()`

Trong `gst-launch-1.0`:

```bash
videotestsrc ! \
    "video/x-raw,format=GRAY8,width=640,height=480" ! \
    fakesink
```

đoạn caps giữa hai dấu `!` là cú pháp rút gọn cho `capsfilter`.

Tương đương về ý tưởng:

```bash
videotestsrc ! \
    capsfilter caps="video/x-raw,format=GRAY8,width=640,height=480" ! \
    fakesink
```

Trong code C/C++, có hai cách phổ biến.

**Explicit `capsfilter`**

```text
source
→ capsfilter
→ sink
```

Ưu điểm:

- có instance name;
- inspect được như một element;
- property có thể đọc lại;
- dễ thay đổi constraint có kiểm soát;
- graph thể hiện rõ policy.

CamStream dùng cách này.

**`gst_element_link_filtered()`**

```cpp
const gboolean linked =
    gst_element_link_filtered(
        source,
        sink,
        filter_caps);
```

API dùng caps làm filter cho link.

Filter caps là borrowed input; code gọi vẫn sở hữu và phải unref reference của mình sau khi link.

Cách này phù hợp với link đơn giản. Explicit `capsfilter` phù hợp hơn khi application cần:

- giữ element để quản lý lifecycle;
- đặt tên rõ ràng;
- inspect property;
- thay đổi constraint;
- thể hiện topology trong graph.

## 5.3 Caps quá chặt và caps quá lỏng

**Caps quá chặt**

Ví dụ:

```text
video/x-raw,
format=YUY2,
width=1920,
height=1080,
framerate=60/1
```

trong khi camera không hỗ trợ mode đó.

Hệ quả:

```text
không có intersection
→ negotiation thất bại
```

Caps cũng có thể quá chặt khi bắt buộc field không cần thiết:

```text
colorimetry
chroma-site
interlace-mode
memory feature
```

mà element hoặc thiết bị không cung cấp đúng giá trị đó.

**Caps quá lỏng**

Ví dụ:

```text
video/x-raw
```

Graph có thể negotiation thành format hợp lệ nhưng không đúng nhu cầu application.

Hệ quả:

- output format không ổn định giữa platform;
- application map buffer theo layout sai;
- converter chọn format không mong muốn;
- test khó tái lập.

Nguyên tắc:

```text
Ràng buộc những field application thực sự cần
Không ràng buộc field chưa có yêu cầu kỹ thuật
```

CamStream ràng buộc:

- media type;
- YUY2 format đối với raw path;
- width;
- height;
- frame rate.

MJPEG path không đặt raw pixel format ở source vì payload còn là `image/jpeg`.

---

# 6. Tạo, đọc và kiểm tra caps trong code

## 6.1 Tạo caps và đặt vào `capsfilter`

Ví dụ YUY2:

```cpp
GstCaps* caps =
    gst_caps_new_simple(
        "video/x-raw",
        "format",
        G_TYPE_STRING,
        "YUY2",
        "width",
        G_TYPE_INT,
        640,
        "height",
        G_TYPE_INT,
        480,
        "framerate",
        GST_TYPE_FRACTION,
        30,
        1,
        nullptr);

if (caps == nullptr ||
    gst_caps_is_empty(caps) != FALSE) {
    if (caps != nullptr) {
        gst_caps_unref(caps);
    }

    return false;
}

g_object_set(
    G_OBJECT(capsfilter),
    "caps",
    caps,
    nullptr);

gst_caps_unref(caps);
```

Property `caps` của capsfilter lấy một reference riêng. Vì vậy code có thể unref reference cục bộ sau `g_object_set()`.

Ví dụ MJPEG:

```cpp
GstCaps* caps =
    gst_caps_new_simple(
        "image/jpeg",
        "width",
        G_TYPE_INT,
        640,
        "height",
        G_TYPE_INT,
        480,
        "framerate",
        GST_TYPE_FRACTION,
        30,
        1,
        nullptr);
```

Không thêm:

```text
format=MJPEG
```

vì media type đã là:

```text
image/jpeg
```

`format` trong raw caps dùng cho pixel format như `YUY2`, `NV12` hoặc `I420`.

## 6.2 Query khả năng và đọc current caps

Ví dụ in caps có thể hỗ trợ của một pad:

```cpp
bool print_possible_caps(
    GstPad* pad) {
    if (pad == nullptr) {
        return false;
    }

    GstCaps* possible_caps =
        gst_pad_query_caps(
            pad,
            nullptr);

    if (possible_caps == nullptr) {
        return false;
    }

    gchar* text =
        gst_caps_to_string(
            possible_caps);

    if (text != nullptr) {
        g_print(
            "Possible caps: %s\n",
            text);

        g_free(text);
    }

    gst_caps_unref(possible_caps);
    return true;
}
```

Ví dụ đọc current video caps:

```cpp
bool read_current_video_caps(
    GstPad* pad) {
    if (pad == nullptr) {
        return false;
    }

    GstCaps* caps =
        gst_pad_get_current_caps(pad);

    if (caps == nullptr) {
        g_print(
            "Pad chưa có current caps\n");
        return false;
    }

    if (gst_caps_is_fixed(caps) == FALSE ||
        gst_caps_get_size(caps) == 0U) {
        gst_caps_unref(caps);
        return false;
    }

    const GstStructure* structure =
        gst_caps_get_structure(
            caps,
            0);

    if (structure == nullptr) {
        gst_caps_unref(caps);
        return false;
    }

    gint width = 0;
    gint height = 0;
    gint fps_numerator = 0;
    gint fps_denominator = 1;

    const gboolean valid =
        gst_structure_get_int(
            structure,
            "width",
            &width) != FALSE &&
        gst_structure_get_int(
            structure,
            "height",
            &height) != FALSE &&
        gst_structure_get_fraction(
            structure,
            "framerate",
            &fps_numerator,
            &fps_denominator) != FALSE;

    const gchar* media_type =
        gst_structure_get_name(
            structure);

    const gchar* format =
        gst_structure_get_string(
            structure,
            "format");

    if (valid != FALSE) {
        g_print(
            "Current caps: %s, format=%s, "
            "%dx%d, %d/%d\n",
            media_type,
            format != nullptr
                ? format
                : "(not present)",
            width,
            height,
            fps_numerator,
            fps_denominator);
    }

    gst_caps_unref(caps);
    return valid != FALSE;
}
```

Ownership:

- `gst_pad_get_current_caps()` trả full reference;
- `gst_caps_get_structure()` trả borrowed pointer;
- `gst_structure_get_name()` trả borrowed string;
- `gst_structure_get_string()` trả borrowed string;
- chỉ unref `GstCaps`.

Current caps có thể chưa tồn tại khi pipeline chưa negotiation.

## 6.3 Kiểm tra compatibility và caps features

Kiểm tra hai tập caps có format chung:

```cpp
const gboolean compatible =
    gst_caps_can_intersect(
        source_caps,
        sink_caps);
```

Hàm không thay đổi ownership.

Đọc features của structure đầu tiên:

```cpp
if (gst_caps_get_size(caps) > 0U) {
    const GstCapsFeatures* features =
        gst_caps_get_features(
            caps,
            0);

    if (features != nullptr) {
        gchar* text =
            gst_caps_features_to_string(
                features);

        if (text != nullptr) {
            g_print(
                "Caps features: %s\n",
                text);

            g_free(text);
        }
    }
}
```

`gst_caps_get_features()` trả borrowed pointer. Không tự `gst_caps_features_free()` pointer này.

Nếu cần sửa caps đang shared:

```cpp
caps =
    gst_caps_make_writable(caps);
```

Hàm tiêu thụ reference đầu vào và trả full reference có thể là pointer mới. Không tiếp tục dùng pointer cũ riêng biệt.

---

# 7. Lỗi `not-negotiated` và cách debug

## 7.1 `not-negotiated` nghĩa là gì?

`GST_FLOW_NOT_NEGOTIATED` cho biết dataflow không có format đã thương lượng hợp lệ tại vị trí cần xử lý buffer.

Nó không chỉ có một nguyên nhân.

Các nhóm nguyên nhân thường gặp:

- source và sink không có caps giao nhau;
- capsfilter yêu cầu mode thiết bị không hỗ trợ;
- thiếu decoder giữa compressed và raw domain;
- thiếu converter giữa hai raw pixel format;
- field quá chặt;
- dynamic pad chưa được link;
- element không gửi hoặc không chấp nhận CAPS event;
- format backend thực tế khác format được chọn;
- renegotiation chưa hoàn tất;
- custom element triển khai negotiation sai.

Ví dụ sai domain:

```text
v4l2src
→ image/jpeg
→ element chỉ nhận video/x-raw
```

Cần decoder:

```text
v4l2src
→ image/jpeg
→ jpegdec
→ video/x-raw
```

Ví dụ raw format không tương thích:

```text
source: video/x-raw,format=YUY2
sink:   video/x-raw,format=RGB
```

Nếu không có converter:

```text
intersection = EMPTY
```

Thêm:

```text
videoconvert
```

để tạo khả năng chuyển đổi.

## 7.2 Quy trình debug theo tầng

Không bắt đầu bằng cách thêm ngẫu nhiên `videoconvert`.

**Bước 1 — Kiểm tra factory và pad template**

```bash
gst-inspect-1.0 v4l2src
gst-inspect-1.0 jpegdec
gst-inspect-1.0 videoconvert
gst-inspect-1.0 fakesink
```

Xác định:

- media type của sink/src pad;
- format;
- range width/height;
- pad presence;
- caps features.

**Bước 2 — Kiểm tra thiết bị**

```bash
v4l2-ctl \
    --device=/dev/videoX \
    --list-formats-ext
```

Pad template rộng không chứng minh camera cụ thể hỗ trợ mode yêu cầu.

**Bước 3 — Chạy graph nhỏ nhất**

Raw:

```bash
gst-launch-1.0 -v \
    v4l2src device=/dev/videoX num-buffers=30 ! \
    "video/x-raw,format=YUY2,width=320,height=240,framerate=30/1" ! \
    fakesink sync=false
```

MJPEG passthrough:

```bash
gst-launch-1.0 -v \
    v4l2src device=/dev/videoX num-buffers=30 ! \
    "image/jpeg,width=640,height=480,framerate=30/1" ! \
    fakesink sync=false
```

**Bước 4 — Thêm từng element**

```text
source + capsfilter
    ↓ pass
thêm decoder
    ↓ pass
thêm converter
    ↓ pass
thêm sink thật
```

**Bước 5 — Quan sát caps**

`-v` in caps khi pipeline chạy:

```bash
gst-launch-1.0 -v ...
```

Để tăng log caps/pad:

```bash
GST_DEBUG=GST_CAPS:6,GST_PADS:5 \
gst-launch-1.0 ...
```

Tập trung tìm:

- CAPS query;
- intersection;
- CAPS event;
- current caps;
- vị trí trả `not-negotiated`.

**Bước 6 — Phân biệt link và negotiation**

```text
gst_element_link() trả TRUE
    = tìm được cặp pad có khả năng tương thích

pipeline đạt PLAYING và có current caps
    = negotiation runtime đã thành công
```

Link success không chứng minh:

- camera mở được;
- exact mode được backend chấp nhận;
- CAPS event được xử lý hoàn tất;
- buffer đã chạy.

---

# 8. Áp dụng vào CamStream Control

## 8.1 Cách dự án tạo capture caps

CamStream tạo fixed requested caps trong:

```text
GstreamerPipeline::configure_capsfilter()
```

**YUY2**

```cpp
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
```

**MJPEG**

```cpp
gst_caps_new_simple(
    "image/jpeg",
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
```

Sau đó code:

1. kiểm tra property `caps` tồn tại và có type `GST_TYPE_CAPS`;
2. tạo `GstCaps`;
3. kiểm tra caps không `nullptr` và không `EMPTY`;
4. đặt vào `capsfilter`;
5. đọc lại property;
6. so sánh bằng `gst_caps_is_equal()`;
7. unref caps đọc lại;
8. unref caps cục bộ.

Việc đọc lại property chứng minh:

```text
capsfilter lưu đúng constraint ứng dụng yêu cầu
```

Nó không thay thế runtime negotiation validation.

## 8.2 Bằng chứng negotiation hiện tại

Stage 7 đã ghi nhận các caps sau.

**Driver tổng hợp**

```text
video/x-raw,
format=YUY2,
width=640,
height=480,
framerate=30/1
```

Kết quả:

- `v4l2src` mở node capture;
- caps negotiation thành công;
- finite runs đạt EOS;
- pipeline về `NULL`.

**Logitech C270 raw**

```text
video/x-raw,
format=YUY2,
width=320,
height=240,
framerate=30/1
```

Caps negotiation thành công trong các lần chạy được chấp nhận.

**Logitech C270 MJPEG**

Source caps:

```text
image/jpeg,
width=640,
height=480,
framerate=30/1
```

Sau `jpegdec`, validation ghi nhận:

```text
video/x-raw,
format=I420
```

I420 là current output quan sát được trong validation đó. Đây không phải output cố định duy nhất của `jpegdec`.

Project evidence chứng minh functional negotiation trong phạm vi test. Nó không chứng minh:

- stable delivered 30 fps;
- zero-copy;
- crop support;
- long-duration USB reliability.

## 8.3 Requested, negotiated và delivered

Ba tầng:

```text
Requested caps
    = application constraint

Negotiated/current caps
    = fixed format được graph chấp nhận

Delivered behavior
    = buffer/frame thực sự được giao theo thời gian
```

Ví dụ:

```text
Requested:
framerate=30/1

Negotiated:
framerate=30/1

Measured:
300 buffers / 15.39 s
≈ 19.5 buffers/s
```

Do đó:

```text
negotiated 30/1
    ≠
delivered ổn định 30 fps
```

Caps mô tả media timeline contract. Throughput thực tế còn phụ thuộc:

- camera;
- USB;
- driver;
- scheduling;
- decoder;
- converter;
- sink;
- dropped buffer;
- hệ thống tải cao.

EOS sau `num-buffers=300` chỉ chứng minh source hữu hạn đã hoàn tất, không chứng minh elapsed time đúng với 30 fps.

## 8.4 Ranh giới với V4L2

Đối với `v4l2src`, negotiated caps phải được ánh xạ sang format và frame interval mà V4L2 device hỗ trợ.

Mô hình:

```text
GStreamer caps
    ↓
v4l2src
    ↓
V4L2 format/frame interval configuration
    ↓
driver và thiết bị
```

Có thể xảy ra:

- mode được chấp nhận đúng;
- driver normalize width/height;
- frame interval khác yêu cầu;
- node không phải image capture node;
- format không được hỗ trợ;
- buffer layout có stride khác giá trị danh nghĩa.

Bài 5 chỉ xác định ranh giới:

```text
caps negotiation ở GStreamer
    phải phù hợp
V4L2 device contract
```

Bài 6 sẽ trình bày:

- `VIDIOC_ENUM_FMT`;
- `VIDIOC_ENUM_FRAMESIZES`;
- `VIDIOC_ENUM_FRAMEINTERVALS`;
- `VIDIOC_TRY_FMT`;
- `VIDIOC_S_FMT`;
- `VIDIOC_G_FMT`;
- `VIDIOC_S_PARM`;
- cách `v4l2src` nối hai tầng.

---

# 9. Tổng kết và nguồn tham khảo

## 9.1 Kiến thức cần nhớ

Mô hình tổng thể:

```text
Pad template caps
    ↓ khả năng tổng quát
Queried / allowed caps
    ↓ khả năng runtime
Capsfilter constraint
    ↓ giới hạn lựa chọn
Intersection + fixation
    ↓ chọn một format
CAPS event
    ↓ thông báo downstream
Current caps
    ↓ mô tả dataflow hiện tại
GstBuffer
    ↓ mang payload theo contract đó
```

Các ý trọng tâm:

1. `GstCaps` mô tả một tập media format và là `GstMiniObject`.
2. `GstCaps` không chứa payload frame.
3. Mỗi entry của caps gồm `GstStructure` và `GstCapsFeatures`.
4. Simple caps chỉ có một structure; fixed caps biểu diễn đúng một format.
5. `ANY` tương thích mọi format; `EMPTY` không chứa format nào.
6. Caps features có thể yêu cầu memory type hoặc metadata.
7. Template caps mô tả khả năng của loại pad; queried caps mô tả khả năng runtime.
8. Current caps là fixed format hiện đang được thiết lập trên pad.
9. Requested caps là constraint của application, không tự động là current caps.
10. Trong push-mode, element phía sau đề xuất và element phía trước quyết định.
11. CAPS query tìm khả năng; CAPS event thông báo format đã chọn.
12. ACCEPT_CAPS chỉ kiểm tra nhanh; RECONFIGURE yêu cầu negotiation lại.
13. Intersection tìm phần format chung; fixation chọn giá trị cụ thể.
14. `capsfilter` giới hạn caps nhưng không thay đổi payload.
15. Inline caps của `gst-launch-1.0` là cú pháp rút gọn của capsfilter.
16. `gst_element_link_filtered()` dùng filter cho một link; explicit capsfilter thể hiện policy rõ hơn.
17. `gst_pad_query_caps()` và `gst_pad_get_current_caps()` trả full reference.
18. `gst_caps_get_structure()` và `gst_caps_get_features()` trả borrowed pointer.
19. Link thành công không chứng minh runtime negotiation thành công.
20. `not-negotiated` có thể do domain, format, field, backend hoặc element implementation.
21. Caps negotiation khác allocation negotiation.
22. Negotiated 30/1 không chứng minh delivered ổn định 30 fps.
23. Caps features hoặc current caps không tự chứng minh zero-copy.
24. CamStream đã chứng minh functional negotiation cho các path YUY2 và MJPEG đã ghi nhận.

**Tài liệu GStreamer chính thức**

- [G1 — `GstCaps`](https://gstreamer.freedesktop.org/documentation/gstreamer/gstcaps.html)
- [G2 — Caps design](https://gstreamer.freedesktop.org/documentation/additional/design/caps.html)
- [G3 — `GstStructure`](https://gstreamer.freedesktop.org/documentation/gstreamer/gststructure.html)
- [G4 — `GstCapsFeatures`](https://gstreamer.freedesktop.org/documentation/gstreamer/gstcapsfeatures.html)
- [G5 — Pads and capabilities](https://gstreamer.freedesktop.org/documentation/application-development/basics/pads.html)
- [G6 — Caps negotiation](https://gstreamer.freedesktop.org/documentation/plugin-development/advanced/negotiation.html)
- [G7 — Negotiation design](https://gstreamer.freedesktop.org/documentation/additional/design/negotiation.html)
- [G8 — `GstPad`](https://gstreamer.freedesktop.org/documentation/gstreamer/gstpad.html)
- [G9 — `capsfilter`](https://gstreamer.freedesktop.org/documentation/coreelements/capsfilter.html)
- [G10 — Handy elements: capsfilter](https://gstreamer.freedesktop.org/documentation/tutorials/basic/handy-elements.html)
- [G11 — `gst-launch-1.0`](https://gstreamer.freedesktop.org/documentation/tools/gst-launch.html)
- [G12 — Memory allocation](https://gstreamer.freedesktop.org/documentation/plugin-development/advanced/allocation.html)

**CamStream Control**

- [P1 — `GstreamerPipeline` implementation](https://github.com/tungnhs2014/camstream-control/blob/74a6a2045198ecc7c91fe32d48dbe42b52e87faa/libs/camstream-gstreamer/src/gstreamer_pipeline.cpp)
- [P2 — `GstreamerPipeline` public interface](https://github.com/tungnhs2014/camstream-control/blob/74a6a2045198ecc7c91fe32d48dbe42b52e87faa/libs/camstream-gstreamer/include/camstream/gstreamer_pipeline.hpp)
- [P3 — Stage 7 GStreamer integration validation](https://github.com/tungnhs2014/camstream-control/blob/74a6a2045198ecc7c91fe32d48dbe42b52e87faa/docs/validation/stage-07-gstreamer-integration.md)
