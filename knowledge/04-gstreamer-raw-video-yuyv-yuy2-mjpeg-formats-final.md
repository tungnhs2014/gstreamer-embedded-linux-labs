# Bài 4 — Định dạng video thô và nén: YUYV/YUY2 và MJPEG

> **Lộ trình:** [Bảng đối chiếu lý thuyết ↔ lab](README.md) · [Lab 04](../practice/lesson-04/README.md). Phần CamStream Control là case study của dự án ngoài repo lab; chỉ tiêu chí PASS của lab mới là bằng chứng kiểm chứng tại repo này.

> **Dự án:** CamStream Control
> **Đối tượng:** Người mới học Embedded Linux và GStreamer
> **Phạm vi:** Bài này giải thích payload trong `GstBuffer` biểu diễn hình ảnh như thế nào, tập trung vào YUYV/YUY2 và MJPEG trong CamStream Control.

---

# 1. Từ `GstBuffer` đến định dạng video

## 1.1 Mục tiêu và phạm vi bài học

Bài 3 đã giải thích `GstBuffer` có thể chứa hoặc tham chiếu đến payload qua một hay nhiều `GstMemory`. Để đọc đúng payload đó, element và ứng dụng phải biết dữ liệu đang biểu diễn loại video nào.

Sau bài này, người học cần:

- phân biệt video thô với video nén;
- phân biệt pixel format với compression format;
- hiểu `video/x-raw` và `image/jpeg` trong GStreamer;
- hiểu Y'CbCr và chroma subsampling ở mức cần thiết;
- phân biệt packed, planar và semi-planar layout;
- đọc đúng byte layout của YUYV/YUY2;
- hiểu quan hệ giữa V4L2 `YUYV` và GStreamer `YUY2`;
- tính đúng kích thước tối thiểu của một frame YUY2;
- hiểu stride, `bytesperline`, `sizeimage` và `bytesused`;
- hiểu MJPEG và đường giải mã bằng `jpegdec`;
- hiểu vai trò của `videoconvert`;
- phân biệt requested, negotiated và delivered frame rate;
- chọn YUY2 hoặc MJPEG theo tài nguyên và mục tiêu hệ thống;
- không suy luận zero-copy, stable FPS hoặc bottleneck khi chưa có bằng chứng.

Bài này không đi sâu vào:

| Nội dung | Bài mở rộng |
|---|---:|
| Cách các element thống nhất caps | Bài 5 |
| Cấu hình V4L2 format và capture queue | Bài 6 |
| Clock, timestamp và đồng bộ theo thời gian | Bài 7 |
| Tích hợp frame vào ứng dụng qua `appsink` | Bài tích hợp ứng dụng |

## 1.2 Pixel, frame, độ phân giải và frame rate

**Pixel**

> **Pixel là phần tử mẫu của một hình ảnh số tại một vị trí xác định.**

Giá trị của pixel không phải lúc nào cũng được lưu trực tiếp dưới dạng ba byte RGB. Tùy pixel format, dữ liệu màu có thể:

- dùng RGB;
- dùng Y'CbCr;
- chia sẻ chroma giữa nhiều pixel;
- nằm trong nhiều plane;
- được nén trong một codestream.

**Frame**

> **Frame là một hình ảnh hoàn chỉnh tại một thời điểm trong luồng video.**

Một frame có:

- width;
- height;
- pixel format hoặc compression format;
- layout trong memory;
- timestamp;
- metadata liên quan.

Như Bài 3 đã lưu ý, không có quy tắc chung rằng mọi `GstBuffer` luôn chứa đúng một frame. Tuy nhiên, trong các raw-video và MJPEG camera path của bài này, buffer thường được xử lý theo ranh giới frame.

**Độ phân giải**

```text
width × height
```

Ví dụ:

```text
640 × 480
```

nghĩa là hình ảnh có 640 vị trí theo chiều ngang và 480 dòng theo chiều dọc.

Số pixel danh nghĩa:

```text
pixel_count = width × height
```

Độ phân giải không cho biết:

- số byte của frame;
- pixel format;
- stride;
- frame đang nén hay chưa;
- frame rate.

**Frame rate**

Frame rate được biểu diễn bằng phân số:

```text
30/1
30000/1001
```

Trong caps GStreamer:

```text
framerate=30/1
```

mô tả frame rate của format đã yêu cầu hoặc thương lượng. Nó không tự chứng minh hệ thống thực tế luôn giao đủ 30 frame mỗi giây.

## 1.3 Video format, pixel format và compression format

Thuật ngữ “video format” có thể được dùng theo nhiều nghĩa. Trong tài liệu này cần tách rõ ba tầng.

**Media type**

Media type xác định loại dữ liệu tổng quát:

```text
video/x-raw
image/jpeg
video/x-h264
```

**Raw pixel format**

> **Raw pixel format mô tả cách các mẫu màu của video thô được sắp xếp trong memory.**

Ví dụ:

```text
YUY2
UYVY
I420
NV12
RGB
BGR
```

Trong GStreamer, các format raw phổ biến được biểu diễn bằng `GstVideoFormat`.

**Compression format**

> **Compression format mô tả cách hình ảnh hoặc video được mã hóa thành dữ liệu nén.**

Ví dụ:

```text
JPEG
H.264
H.265
AV1
```

MJPEG camera path sử dụng các frame JPEG liên tiếp và được biểu diễn bằng:

```text
image/jpeg
```

MJPEG không phải raw pixel format và không phải một giá trị raw `GstVideoFormat`.

Trong V4L2, trường `pixelformat` có thể chứa:

- raw pixel format như `V4L2_PIX_FMT_YUYV`;
- loại compression như `V4L2_PIX_FMT_MJPEG`.

Tên trường `pixelformat` của V4L2 vì vậy không có nghĩa mọi giá trị đều là video thô.

---

# 2. Video thô và video nén

## 2.1 Khái niệm video thô

> **Video thô là chuỗi mẫu hình ảnh chưa được nén bằng một video hoặc image codec.**

Trong GStreamer, video thô thường dùng media type:

```text
video/x-raw
```

Caps raw video có thể gồm:

```text
video/x-raw,
format=YUY2,
width=640,
height=480,
framerate=30/1
```

`video/x-raw` có nghĩa payload chứa các mẫu video chưa được nén ở tầng media.

Nó không đồng nghĩa với sensor RAW.

Sensor RAW thường là dữ liệu trực tiếp từ cảm biến, chẳng hạn Bayer mosaic, trước nhiều bước xử lý của image signal processor. YUY2 là video thô ở tầng multimedia nhưng đã trải qua quá trình tạo mẫu Y'CbCr và có thể đã qua xử lý camera.

Đặc điểm của video thô:

- kích thước mỗi frame thường có thể tính từ layout;
- không cần decoder trước khi đọc pixel;
- dễ đưa vào thuật toán xử lý ảnh nếu format phù hợp;
- thường cần băng thông memory và bus lớn;
- mọi lỗi về stride hoặc byte order có thể làm hình ảnh sai ngay lập tức.

## 2.2 Khái niệm video nén

> **Video nén là dữ liệu hình ảnh hoặc video đã được codec biến đổi để giảm số byte cần lưu hoặc truyền.**

Trong CamStream, format nén hiện tại là MJPEG:

```text
image/jpeg
```

Đặc điểm:

- payload thường nhỏ hơn raw video;
- kích thước frame có thể thay đổi;
- phải giải mã trước khi thuật toán raw-video đọc pixel;
- giảm lưu lượng qua USB hoặc lưu trữ;
- tăng chi phí decoder và có thể tăng latency;
- chất lượng phụ thuộc codec configuration và nguồn camera.

Không được dùng công thức raw frame size cho MJPEG.

Ví dụ:

```text
640 × 480 × 2
```

là kích thước danh nghĩa của YUY2 tightly packed, không phải kích thước cố định của một MJPEG frame.

## 2.3 Các đánh đổi tổng quát

| Thuộc tính | Video thô | Video nén |
|---|---|---|
| Payload trên bus | Lớn và tương đối ổn định | Thường nhỏ hơn và biến thiên |
| Decoder | Không cần để đọc pixel | Cần giải mã |
| CPU | Thường thấp hơn ở bước decode | Phụ thuộc decoder |
| Memory bandwidth sau decode | Cao | Vẫn tăng lại sau decode |
| Debug layout | Phải đúng byte order và stride | Phải đúng framing và codestream |
| Random frame access | Trực tiếp với raw frame | Cần decode |
| Chất lượng | Không có mất mát do codec ở bước này | JPEG thường là lossy |

Không có format tốt nhất cho mọi hệ thống. Lựa chọn phụ thuộc:

- camera capabilities;
- bus bandwidth;
- CPU hoặc hardware decoder;
- latency;
- thuật toán downstream;
- mục tiêu ghi file hoặc truyền mạng.

---

# 3. Y'CbCr, chroma subsampling và layout

## 3.1 Y'CbCr trong video số

Các tài liệu và API thường dùng từ “YUV” như tên nhóm format. Với video số 8-bit như YUY2, các thành phần thực tế thường được hiểu theo mô hình Y'CbCr.

```text
Y'
    = thành phần độ sáng đã áp dụng transfer characteristic

Cb
    = thành phần sai khác màu xanh dương

Cr
    = thành phần sai khác màu đỏ
```

Dấu `'` trong Y' nhắc rằng đây không phải luminance tuyến tính thuần túy.

Trong thực tế kỹ thuật:

```text
YUV
    thường được dùng như tên gọi quen thuộc

Y'CbCr
    mô tả chính xác hơn các mẫu video số
```

Tài liệu vẫn có thể gọi “YUV format” để phù hợp tên API, nhưng byte layout phải được diễn giải theo Y', Cb và Cr.

## 3.2 Chroma subsampling

Mắt người thường nhạy với chi tiết độ sáng hơn chi tiết màu. Chroma subsampling giảm số mẫu Cb/Cr nhưng giữ nhiều mẫu Y'.

Ký hiệu phổ biến:

```text
4:4:4
4:2:2
4:2:0
```

**4:4:4**

- mỗi vị trí có đầy đủ mẫu luma và chroma;
- không giảm độ phân giải chroma;
- payload lớn hơn.

**4:2:2**

- giữ mẫu Y' cho mỗi pixel;
- giảm độ phân giải chroma theo chiều ngang còn một nửa;
- hai pixel ngang dùng chung một cặp Cb/Cr.

Mô hình:

```text
Pixel 0: Y0 + Cb0 + Cr0
Pixel 1: Y1 + Cb0 + Cr0
```

YUY2 là packed 4:2:2 8-bit.

**4:2:0**

- giảm chroma theo cả chiều ngang và chiều dọc;
- một nhóm 2 × 2 pixel thường chia sẻ chroma;
- các format phổ biến gồm I420, YV12 và NV12.

Không thể suy ra layout memory chỉ từ ký hiệu subsampling.

```text
4:2:0
    có thể là I420
    có thể là YV12
    có thể là NV12
```

## 3.3 Packed, planar và semi-planar

**Packed**

> **Packed format đặt các thành phần mẫu xen kẽ trong một layout liên tục theo quy tắc của format.**

Ví dụ YUY2:

```text
Y0 Cb0 Y1 Cr0
```

**Planar**

> **Planar format tách các thành phần thành các plane riêng.**

Ví dụ I420:

```text
Plane Y
Plane U
Plane V
```

**Semi-planar**

> **Semi-planar format có một plane luma riêng và một plane chroma xen kẽ.**

Ví dụ NV12:

```text
Plane Y
Plane UV
```

Ba khái niệm thường bị nhầm nhưng không đồng nhất:

```text
Video plane
    = vùng logic của một format

GstMemory
    = object quản lý một vùng memory

V4L2 multi-planar API
    = biến thể API dùng một địa chỉ/buffer descriptor cho mỗi memory plane
```

Một format nhiều plane có thể nằm trong:

- một vùng memory liên tục;
- nhiều `GstMemory`;
- nhiều V4L2 memory plane.

Ngược lại, một packed format vẫn có thể được chứa bởi nhiều memory block do allocator hoặc buffer construction.

Do đó:

```text
số plane
    ≠
số GstMemory
    ≠
số V4L2 buffer object
```

**Pixel format chưa phải toàn bộ video contract**

Ngoài pixel format và kích thước, raw video còn có thể cần các thuộc tính sau:

| Thuộc tính | Vai trò |
|---|---|
| `colorimetry` | Mô tả primaries, transfer function, matrix và range liên quan |
| `chroma-site` | Mô tả vị trí mẫu chroma so với mẫu luma |
| `pixel-aspect-ratio` | Tỷ lệ hình học của một pixel |
| `interlace-mode` | Progressive hay interlaced |
| `field-order` | Thứ tự field khi video interlaced |

Hai buffer có cùng FourCC, width và height vẫn có thể cho màu hoặc hình học khác nhau nếu các thuộc tính này khác nhau.

Bài 5 sẽ giải thích cách những trường này tham gia caps và caps negotiation.

---

# 4. YUYV và YUY2

## 4.1 Khái niệm và byte layout

> **YUYV/YUY2 là packed 4:2:2 format 8-bit, lưu hai pixel trong bốn byte theo thứ tự Y'0, Cb0, Y'1, Cr0.**

Byte layout:

```text
Byte offset:   0    1    2    3    4    5    6    7
             ┌────┬────┬────┬────┬────┬────┬────┬────┐
Payload:     │ Y0 │ Cb0│ Y1 │ Cr0│ Y2 │ Cb2│ Y3 │ Cr2│
             └────┴────┴────┴────┴────┴────┴────┴────┘
Pixel:          0         1         2         3
```

Hai pixel đầu sử dụng:

```text
Pixel 0
    = Y0 + Cb0 + Cr0

Pixel 1
    = Y1 + Cb0 + Cr0
```

Mỗi pixel có mẫu Y' riêng. Hai pixel ngang chia sẻ Cb và Cr.

Bốn byte biểu diễn hai pixel:

```text
4 bytes / 2 pixels = 2 bytes per pixel
```

Đây là giá trị trung bình cho payload 8-bit YUY2 tightly packed.

## 4.2 V4L2 `YUYV` và GStreamer `YUY2`

V4L2 sử dụng FourCC:

```c
V4L2_PIX_FMT_YUYV
```

FourCC hiển thị:

```text
YUYV
```

GStreamer sử dụng raw video format name:

```text
YUY2
```

Hai tên này mô tả cùng byte order 8-bit packed 4:2:2:

```text
Y0 Cb0 Y1 Cr0
```

Mối quan hệ trong CamStream:

```text
V4L2
    V4L2_PIX_FMT_YUYV
          ↓ mapping
GStreamer
    video/x-raw,format=YUY2
```

Không cần thực hiện phép chuyển đổi byte chỉ vì tên khác nhau.

## 4.3 Các thứ tự packed 4:2:2 khác

Các format sau đều là packed 4:2:2 nhưng thứ tự byte khác nhau:

| Format | Byte 0 | Byte 1 | Byte 2 | Byte 3 |
|---|---|---|---|---|
| YUYV / YUY2 | Y0 | Cb0 | Y1 | Cr0 |
| UYVY | Cb0 | Y0 | Cr0 | Y1 |
| YVYU | Y0 | Cr0 | Y1 | Cb0 |
| VYUY | Cr0 | Y0 | Cb0 | Y1 |

Đọc YUY2 như UYVY hoặc YVYU có thể làm:

- màu tím hoặc xanh bất thường;
- độ sáng sai;
- ảnh vẫn có hình dạng đúng nhưng màu hoàn toàn sai.

Byte order là một phần của pixel-format contract. Không được chỉ kiểm tra “đều là 4:2:2”.

## 4.4 Width và cặp pixel

YUY2 tổ chức mẫu theo nhóm hai pixel ngang. Vì vậy thiết bị và element thường yêu cầu width chẵn.

Tuy nhiên, không nên viết một quy tắc tuyệt đối rằng mọi API đều từ chối width lẻ. GStreamer định nghĩa default row stride của YUY2 bằng:

```text
RU4(width × 2)
```

nghĩa là làm tròn `width × 2` lên bội số của 4.

Quy tắc thực tế:

- dùng width do driver trả về hoặc caps đã thương lượng;
- kiểm tra stride thực tế;
- không tự giả định width lẻ được hỗ trợ;
- CamStream hiện chỉ dùng width chẵn.

---

# 5. Stride, kích thước frame và các trường V4L2

## 5.1 Stride và `bytesperline`

> **Stride là khoảng cách theo byte từ vị trí bắt đầu của một dòng đến vị trí bắt đầu của dòng kế tiếp trong cùng plane.**

Với YUY2 tightly packed:

```text
minimum_row_bytes = width × 2
```

Nhưng stride thực tế có thể lớn hơn:

```text
stride = minimum_row_bytes + padding
```

Ví dụ:

```text
width = 640
minimum_row_bytes = 1280
driver stride = 1344
padding mỗi dòng = 64 bytes
```

Layout:

```text
Dòng 0: 1280 byte hình ảnh + 64 byte padding
Dòng 1: 1280 byte hình ảnh + 64 byte padding
...
```

Padding có thể xuất hiện do:

- alignment của DMA;
- yêu cầu phần cứng;
- cache line;
- allocator;
- định dạng nội bộ.

Trong V4L2 single-planar API:

```text
bytesperline
    = khoảng cách giữa điểm bắt đầu của hai dòng liền kề
```

Driver có thể:

- chấp nhận giá trị ứng dụng yêu cầu;
- bỏ qua yêu cầu;
- trả `width × bytes_per_pixel`;
- trả giá trị lớn hơn do alignment.

Ứng dụng phải sử dụng giá trị driver trả về sau `VIDIOC_S_FMT` hoặc `VIDIOC_G_FMT`.

Với compressed format, `bytesperline` không có ý nghĩa và phải bằng 0.

Trong GStreamer, layout thực tế có thể được mô tả bằng `GstVideoInfo` hoặc `GstVideoMeta`. Không được giả định mọi YUY2 buffer luôn có stride đúng bằng `width × 2`.

## 5.2 `sizeimage`, `bytesused` và `data_offset`

Ba giá trị này phục vụ ba mục đích khác nhau.

**`sizeimage`**

> **`sizeimage` là kích thước buffer cần thiết để chứa một image theo format đã cấu hình.**

Với raw single-planar format, giá trị thường gần:

```text
bytesperline × height
```

nhưng phải dùng giá trị driver trả về.

Với variable-length compressed format:

```text
sizeimage
    = dung lượng cần thiết cho trường hợp payload lớn nhất mà codec/driver hỗ trợ
```

Nó là capacity requirement, không phải kích thước thực của mọi MJPEG frame.

**`bytesused`**

> **`bytesused` là số byte payload hợp lệ trong buffer đã hoàn thành.**

Với MJPEG:

```text
bytesused
    = kích thước codestream thực của frame hiện tại
```

Mỗi frame có thể có `bytesused` khác nhau.

Với raw capture, `bytesused` thường phản ánh payload của frame, nhưng application vẫn phải tuân theo contract của driver và layout đã trả về.

**`data_offset` trong multi-planar API**

Với `struct v4l2_plane`:

```text
payload bắt đầu tại data_offset
```

Theo contract V4L2:

```text
actual_image_bytes = bytesused - data_offset
```

và dữ liệu bắt đầu tại:

```text
plane_base + data_offset
```

Không được dùng `bytesused` như kích thước hình ảnh từ byte đầu của plane khi `data_offset` khác 0.

Tóm tắt:

| Trường | Ý nghĩa |
|---|---|
| `bytesperline` | Khoảng cách giữa hai dòng |
| `sizeimage` | Dung lượng buffer cần cấp |
| `bytesused` | Payload hợp lệ của buffer hiện tại |
| `data_offset` | Vị trí bắt đầu payload trong plane |

## 5.3 Tính kích thước YUY2 an toàn

Nếu frame YUY2 tightly packed:

```text
row_bytes = width × 2
frame_bytes = row_bytes × height
```

Nếu có stride:

```text
frame_lưu trữ = stride × height
```

Trong đó:

```text
stride >= width × 2
```

Ví dụ 640 × 480:

```text
row_bytes = 640 × 2
          = 1280 bytes

frame_bytes = 1280 × 480
            = 614400 bytes
            = 600 KiB
```

Ví dụ C++ kiểm tra layout và tránh overflow:

```cpp
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

struct Yuy2Layout {
    std::size_t minimum_row_bytes = 0U;
    std::size_t storage_bytes = 0U;
};

std::optional<Yuy2Layout> calculate_yuy2_layout(
    std::uint32_t width,
    std::uint32_t height,
    std::size_t stride) {
    if (width == 0U || height == 0U) {
        return std::nullopt;
    }

    constexpr std::size_t bytes_per_pixel = 2U;

    if (static_cast<std::size_t>(width) >
        std::numeric_limits<std::size_t>::max() /
            bytes_per_pixel) {
        return std::nullopt;
    }

    const std::size_t minimum_row_bytes =
        static_cast<std::size_t>(width) *
        bytes_per_pixel;

    if (stride < minimum_row_bytes) {
        return std::nullopt;
    }

    if (static_cast<std::size_t>(height) >
        std::numeric_limits<std::size_t>::max() /
            stride) {
        return std::nullopt;
    }

    return Yuy2Layout{
        minimum_row_bytes,
        stride *
            static_cast<std::size_t>(height),
    };
}
```

Hàm nhận stride thực tế thay vì tự giả định tightly packed.

Ví dụ sử dụng:

```cpp
const auto layout =
    calculate_yuy2_layout(
        640U,
        480U,
        1280U);

if (!layout.has_value()) {
    // Format hoặc kích thước không hợp lệ.
}
```

Nếu ứng dụng chỉ nhận `GstBuffer`, cần lấy layout từ caps cùng `GstVideoInfo` hoặc `GstVideoMeta`, không tự gán stride bằng `width × 2`.

## 5.4 Nominal raw payload rate

Với YUY2 tightly packed:

```text
payload_rate =
    width × height × 2 × frames_per_second
```

640 × 480 tại 30 fps:

```text
614400 bytes/frame × 30
    = 18432000 bytes/second
    = 18.432 MB/s
    ≈ 17.58 MiB/s
```

320 × 240 tại 30 fps:

```text
153600 bytes/frame × 30
    = 4608000 bytes/second
    = 4.608 MB/s
    ≈ 4.39 MiB/s
```

Đây chỉ là nominal image payload.

USB bus load có thể khác do:

- packet headers;
- transfer scheduling;
- protocol overhead;
- retries;
- alternate setting;
- padding;
- thiết bị không giao đủ frame;
- driver hoặc host-controller behavior.

Memory bandwidth trong hệ thống cũng có thể cao hơn raw payload rate nếu frame bị:

- copy nhiều lần;
- decode;
- convert;
- đọc và ghi bởi nhiều element.

---

# 6. MJPEG và đường giải mã

## 6.1 Khái niệm MJPEG

> **MJPEG là chuỗi các frame mà mỗi frame được mã hóa độc lập bằng JPEG.**

Mô hình:

```text
JPEG frame 0
JPEG frame 1
JPEG frame 2
...
```

Khác với codec inter-frame như H.264:

```text
MJPEG
    mỗi frame thường tự giải mã độc lập

H.264/H.265
    nhiều frame có thể phụ thuộc frame khác
```

Ưu điểm của MJPEG:

- truy cập độc lập theo từng frame đơn giản;
- lỗi một frame ít ảnh hưởng frame sau;
- camera USB thường hỗ trợ trực tiếp;
- payload qua USB thường nhỏ hơn YUY2 cùng độ phân giải.

Nhược điểm:

- phải giải mã JPEG;
- frame size thay đổi;
- hiệu quả nén thường thấp hơn codec inter-frame;
- sau decode, raw payload và memory bandwidth tăng trở lại.

## 6.2 JPEG codestream và payload frame

Một JPEG codestream chứa các marker và compressed scan data cần thiết để decoder tái tạo hình ảnh.

Không nên giả định mọi `image/jpeg` buffer luôn là một file JPEG theo đúng một wrapper cụ thể như JFIF hoặc Exif. GStreamer media type mô tả JPEG image data; metadata wrapper cụ thể phụ thuộc nguồn.

MJPEG frame size thay đổi theo:

- nội dung hình ảnh;
- mức chi tiết;
- noise;
- JPEG quality;
- phần triển khai encoder;
- restart interval hoặc header data.

Vì vậy:

```text
sizeimage
    = capacity

bytesused
    = payload thực của frame hiện tại
```

Dùng `sizeimage` để ghi toàn bộ buffer ra file có thể chèn dữ liệu thừa sau codestream.

## 6.3 `jpegdec`, `jpegparse` và `videoconvert`

**`jpegdec`**

> **`jpegdec` là GStreamer decoder nhận `image/jpeg` và tạo `video/x-raw`.**

Graph:

```text
image/jpeg
    ↓
jpegdec
    ↓
video/x-raw
```

Output format không phải lúc nào cũng cố định là I420. Pad template chính thức của `jpegdec` hỗ trợ nhiều raw format như:

```text
I420
RGB
BGR
RGBx
xRGB
BGRx
xBGR
GRAY8
```

Format thực tế do caps negotiation quyết định.

Trong validation hiện tại của CamStream, `jpegdec` đã thương lượng output I420. Đây là project evidence của lần chạy đó, không phải quy tắc phổ quát của `jpegdec`.

**`jpegparse`**

`jpegparse` có thể được dùng để:

- xác định ranh giới JPEG frame;
- chuẩn hóa hoặc phân tích JPEG stream;
- bổ sung thông tin cần thiết cho downstream.

Camera path hiện tại:

```text
v4l2src
→ image/jpeg
→ jpegdec
```

đã hoạt động mà không cần `jpegparse`. Không nên thêm parser khi chưa có yêu cầu framing hoặc compatibility cụ thể.

**`videoconvert`**

> **`videoconvert` chuyển đổi giữa nhiều raw video format và color representation.**

Graph:

```text
video/x-raw,format=A
    ↓
videoconvert
    ↓
video/x-raw,format=B
```

`videoconvert` không giải mã JPEG.

```text
jpegdec
    = compressed JPEG → raw video

videoconvert
    = raw video format A → raw video format B
```

Nếu element phía trước và element phía sau thống nhất cùng raw format, `videoconvert` có thể thương lượng đường passthrough mà không cần đổi pixel format.

Do đó, chỉ thấy element `videoconvert` trong graph không chứng minh frame luôn bị chuyển đổi hoặc sao chép.

## 6.4 Đường MJPEG của CamStream

Graph hiện tại:

```text
v4l2src
    ↓ image/jpeg
capsfilter
    ↓ image/jpeg
jpegdec
    ↓ video/x-raw
videoconvert
    ↓ video/x-raw
fakesink
```

Vai trò:

- `v4l2src` lấy MJPEG frame từ V4L2;
- `capsfilter` ràng buộc width, height và frame rate đầu vào;
- `jpegdec` giải mã JPEG;
- `videoconvert` mở rộng khả năng tương thích raw format;
- `fakesink` tiêu thụ dữ liệu để kiểm tra luồng.

Sau decode, payload thô có thể lớn hơn đáng kể so với JPEG input.

Ví dụ nếu decoder tạo I420 tightly packed tại 640 × 480:

```text
Y plane  = 640 × 480
U plane  = 320 × 240
V plane  = 320 × 240

total = 460800 bytes
```

Công thức `width × height × 1.5` chỉ đúng cho:

- I420 8-bit;
- width và height phù hợp với 4:2:0;
- layout tightly packed;
- không có stride padding bổ sung.

Không dùng công thức đó cho mọi `video/x-raw`.

---

# 7. Lựa chọn format và bằng chứng CamStream

## 7.1 Hai graph hiện tại

**YUY2 path**

```text
v4l2src
→ video/x-raw,format=YUY2
→ fakesink
```

Trong implementation C++:

```text
v4l2src
→ capsfilter
→ fakesink
```

**MJPEG path**

```text
v4l2src
→ image/jpeg
→ jpegdec
→ videoconvert
→ fakesink
```

Trong implementation C++:

```text
v4l2src
→ capsfilter
→ jpegdec
→ videoconvert
→ fakesink
```

Hai path dùng chung:

- device selection;
- width, height và fps configuration;
- finite hoặc continuous run;
- Bus và lifecycle management.

Chúng khác nhau ở cách diễn giải payload và các element xử lý cần thiết.

## 7.2 Bằng chứng hiện tại của dự án

**Driver tổng hợp**

Driver `camstream-video` cung cấp cố định:

```text
YUYV 640 × 480 @ 30 fps
```

GStreamer mapping:

```text
video/x-raw,
format=YUY2,
width=640,
height=480,
framerate=30/1
```

Ba lần chạy 300 buffer đã đạt EOS và hoàn tất khoảng 10.11 giây trong lần đo được ghi nhận.

Kết quả này chứng minh:

- V4L2 node được `v4l2src` mở;
- YUY2 caps được thương lượng;
- buffer đi qua graph;
- finite lifecycle hoàn tất.

Nó không chứng minh zero-copy. Validation ghi nhận GStreamer đã bật V4L2 copy threshold vì chưa đủ chắc chắn về buffer count và zero-copy path.

**Logitech C270 raw path**

Path được chấp nhận:

```text
YUY2 320 × 240 @ 30/1
```

Một lần chạy trực tiếp đạt khoảng 29.1 buffer/s, nhưng các phép đo V4L2 trực tiếp sau đó cho thấy throughput không ổn định và thường khoảng 19–20 buffer/s.

Do đó project không tuyên bố stable delivered 30 fps.

**Logitech C270 MJPEG path**

Path được chấp nhận:

```text
MJPEG 640 × 480 @ 30/1
```

`jpegdec` đã tạo:

```text
video/x-raw,format=I420
```

trong validation được ghi nhận.

300 buffer thường hoàn tất khoảng 15.3–15.6 giây, tương đương khoảng 19–20 buffer/s.

**MJPEG passthrough isolation**

Graph:

```text
v4l2src
→ image/jpeg
→ fakesink
```

không dùng `jpegdec` hoặc `videoconvert`, nhưng throughput vẫn khoảng 19.5 buffer/s.

Kết quả này cho thấy software JPEG decode chưa được chứng minh là điểm nghẽn chính.

Native V4L2 capture cũng tái hiện khoảng 19–20 fps. Root cause chính xác chưa được xác định.

## 7.3 Requested, negotiated và delivered frame rate

Ba giá trị phải được phân biệt.

**Requested**

Ứng dụng yêu cầu:

```text
framerate=30/1
```

**Negotiated hoặc configured**

Pipeline và driver chấp nhận contract:

```text
video/x-raw,framerate=30/1
```

hoặc:

```text
image/jpeg,framerate=30/1
```

**Delivered hoặc measured**

Số buffer thực sự nhận được trong một khoảng thời gian:

```text
delivered_rate =
    completed_buffers / elapsed_time
```

Ví dụ:

```text
300 buffers / 15.39 seconds
    ≈ 19.49 buffers/s
```

Do đó:

```text
negotiated 30/1
    ≠
measured 30 buffers/s
```

EOS chỉ cho biết source hữu hạn đã hoàn tất. Muốn đo throughput phải có elapsed time và xác định rõ đang đếm:

- input buffers;
- decoded frames;
- output buffers;
- dropped buffers.

## 7.4 `sync=false` và giới hạn kết luận

`fakesink sync=false` làm sink không chờ pipeline clock trước khi tiêu thụ buffer.

Nó hữu ích để:

- giảm ảnh hưởng của cơ chế lập lịch của sink;
- đo dataflow mà không chờ presentation time;
- cô lập một nhóm nguyên nhân về clock synchronization.

Nó không:

- làm camera giao frame nhanh hơn;
- bỏ qua driver hoặc USB timing;
- loại bỏ decoder cost;
- chứng minh source đạt requested FPS;
- chứng minh zero-copy.

Trong Stage 7, passthrough, `sync=false` và native V4L2 đều cho throughput tương tự. Vì vậy sink synchronization và JPEG decoder không được chứng minh là nguyên nhân chính, nhưng root cause vẫn chưa được xác định.

## 7.5 Chọn YUY2 hay MJPEG

| Tiêu chí | YUY2 | MJPEG |
|---|---|---|
| USB payload | Cao | Thường thấp hơn |
| Kích thước frame | Gần cố định theo stride | Thay đổi theo nội dung |
| Decoder | Không cần | Cần JPEG decoder |
| CPU decode | Thấp ở bước này | Có chi phí |
| Truy cập pixel | Trực tiếp sau khi hiểu layout | Phải decode |
| Mất mát do codec | Không có ở bước truyền YUY2 | JPEG thường có mất mát |
| Debug | Stride và byte order | Codestream và decoder |
| Phù hợp camera USB độ phân giải cao | Có thể bị giới hạn bus | Thường phù hợp hơn |

YUY2 phù hợp khi:

- độ phân giải và frame rate vừa với bus;
- cần raw frame trực tiếp;
- CPU hạn chế và không có decoder phù hợp;
- yêu cầu tránh mất mát JPEG.

MJPEG phù hợp khi:

- USB bandwidth là giới hạn;
- camera có encoder JPEG;
- hệ thống có đủ tài nguyên decode;
- chất lượng JPEG đáp ứng yêu cầu.

Không nên luôn chọn một format chỉ từ một tiêu chí.

Lựa chọn lựa chọn ở mức sản phẩm cần đo:

- delivered FPS;
- CPU load;
- end-to-end latency;
- memory bandwidth;
- frame drop;
- chất lượng hình ảnh;
- độ ổn định dài hạn.

---

# 8. Kiểm tra, gỡ lỗi và tổng kết

## 8.1 Quy trình kiểm tra theo tầng

**Bước 1 — Xác định đúng device node**

Không gán cố định `/dev/video0`.

Kiểm tra:

```bash
v4l2-ctl --list-devices
```

Chọn node có capability image capture phù hợp, không chọn metadata node.

**Bước 2 — Liệt kê format và mode**

```bash
v4l2-ctl \
    --device=/dev/videoX \
    --list-formats-ext
```

Xác nhận:

- FourCC;
- width/height;
- khoảng thời gian giữa các frame;
- raw hoặc compressed.

**Bước 3 — Kiểm tra format thực tế**

```bash
v4l2-ctl \
    --device=/dev/videoX \
    --get-fmt-video \
    --get-parm
```

Quan sát:

- width;
- height;
- pixel format;
- field;
- bytes per line;
- size image;
- time per frame.

**Bước 4 — Kiểm tra raw path**

```bash
gst-launch-1.0 -v \
    v4l2src device=/dev/videoX num-buffers=300 ! \
    "video/x-raw,format=YUY2,width=320,height=240,framerate=30/1" ! \
    fakesink sync=false
```

**Bước 5 — Kiểm tra MJPEG passthrough**

```bash
gst-launch-1.0 -v \
    v4l2src device=/dev/videoX num-buffers=300 ! \
    "image/jpeg,width=640,height=480,framerate=30/1" ! \
    fakesink sync=false
```

**Bước 6 — Thêm decoder**

```bash
gst-launch-1.0 -v \
    v4l2src device=/dev/videoX num-buffers=300 ! \
    "image/jpeg,width=640,height=480,framerate=30/1" ! \
    jpegdec ! \
    videoconvert ! \
    fakesink sync=false
```

**Bước 7 — So sánh native V4L2**

```bash
v4l2-ctl \
    --device=/dev/videoX \
    --set-fmt-video=width=640,height=480,pixelformat=MJPG \
    --set-parm=30 \
    --stream-mmap \
    --stream-count=300 \
    --stream-to=/dev/null
```

So sánh từng tầng giúp tránh kết luận vội:

```text
camera/driver
→ V4L2
→ v4l2src
→ decoder
→ converter
→ sink
```

## 8.2 Lỗi thường gặp

**Hình bị lệch hoặc chéo**

Kiểm tra:

- stride;
- `bytesperline`;
- padding;
- width thực;
- kích thước buffer;
- offset từng plane.

Không nhảy sang dòng kế tiếp bằng `width × bytes_per_pixel` nếu stride lớn hơn.

**Hình có màu xanh hoặc tím**

Kiểm tra:

- YUY2 hay UYVY;
- Cb và Cr có bị đảo;
- colorimetry;
- color range;
- chroma site.

**Raw frame bị cắt**

Kiểm tra:

```text
buffer_size >= stride × height
```

và không dùng `width × height × 2` khi có padding.

**MJPEG decoder báo lỗi**

Kiểm tra:

- `bytesused`;
- frame boundaries;
- payload có bị ghi thêm padding;
- caps có phải `image/jpeg`;
- camera có giao frame lỗi;
- có cần `jpegparse` để chuẩn hóa framing hay không.

**Payload throughput thấp**

Cô lập lần lượt:

1. sink synchronization;
2. decoder và converter;
3. GStreamer bridge;
4. native V4L2;
5. USB topology và reset;
6. CPU load;
7. frame drop.

Không quy lỗi cho decoder chỉ vì graph có `jpegdec`.

**Dùng `videoconvert` ở mọi nơi**

`videoconvert` hữu ích khi raw format chưa chắc tương thích. Nhưng thêm element không cần thiết có thể làm graph phức tạp và có khả năng phát sinh chuyển đổi.

Chỉ thêm khi:

- downstream cần raw format khác;
- format chưa xác định trước;
- muốn tạo compatibility boundary rõ ràng.

**Tuyên bố zero-copy từ raw path**

Raw YUY2 không có bước JPEG decode nhưng vẫn có thể copy tại:

- V4L2 source;
- allocator;
- buffer pool;
- mapping;
- sink;
- memory compatibility boundary.

Raw không đồng nghĩa zero-copy.

## 8.3 Kiến thức cần nhớ và nguồn tham khảo

Mô hình tổng thể:

```text
GstBuffer payload
    ↓ được diễn giải bởi
Media type + format + layout
    ↓
Raw video
    ├── YUY2
    ├── I420
    └── NV12

Compressed video
    └── image/jpeg
          ↓ jpegdec
       video/x-raw
```

Các ý trọng tâm:

1. Độ phân giải không đủ để xác định kích thước frame.
2. Raw pixel format mô tả layout mẫu chưa nén; compression format mô tả codestream nén.
3. `video/x-raw` không đồng nghĩa sensor RAW.
4. YUY2 là packed 4:2:2 với byte order Y0, Cb0, Y1, Cr0.
5. V4L2 `YUYV` và GStreamer `YUY2` mô tả cùng byte order.
6. Hai pixel YUY2 dùng bốn byte và chia sẻ chroma.
7. Packed, planar và semi-planar không đồng nghĩa với số `GstMemory`.
8. Video plane khác V4L2 multi-planar API.
9. FourCC chưa mô tả đầy đủ colorimetry, chroma site, pixel aspect ratio và interlace.
10. Stride có thể lớn hơn số byte pixel hữu ích của một dòng.
11. `bytesperline` phải dùng theo giá trị driver trả về.
12. `sizeimage` là dung lượng cần cấp; `bytesused` là payload thực của buffer hiện tại.
13. Với V4L2 multi-planar, payload bắt đầu tại `data_offset`.
14. MJPEG frame size thay đổi và không dùng công thức raw frame size.
15. `jpegdec` chuyển `image/jpeg` thành `video/x-raw`; output format do negotiation quyết định.
16. `videoconvert` chuyển raw format và có thể passthrough khi không cần đổi.
17. Decoder output I420 là bằng chứng của CamStream validation, không phải quy tắc phổ quát.
18. Negotiated frame rate không chứng minh delivered frame rate.
19. `sync=false` chỉ loại bỏ việc sink chờ pipeline clock.
20. YUY2 và MJPEG phải được chọn dựa trên bus, CPU, latency và chất lượng.
21. Pipeline chạy đúng chức năng không phải bằng chứng zero-copy hoặc độ tin cậy ở mức sản phẩm.

**Tài liệu GStreamer chính thức**

- [G1 — Raw Video Media Types](https://gstreamer.freedesktop.org/documentation/additional/design/mediatype-video-raw.html)
- [G2 — `GstVideoFormat`](https://gstreamer.freedesktop.org/documentation/video/video-format.html)
- [G3 — Media formats and Pad Capabilities](https://gstreamer.freedesktop.org/documentation/tutorials/basic/media-formats-and-pad-capabilities.html)
- [G4 — `GstVideoInfo`](https://gstreamer.freedesktop.org/documentation/video/video-info.html)
- [G5 — `GstVideoMeta`](https://gstreamer.freedesktop.org/documentation/video/gstvideometa.html)
- [G6 — `jpegdec`](https://gstreamer.freedesktop.org/documentation/jpeg/jpegdec.html)
- [G7 — `jpegparse`](https://gstreamer.freedesktop.org/documentation/jpegformat/jpegparse.html)
- [G8 — `videoconvert`](https://gstreamer.freedesktop.org/documentation/videoconvertscale/videoconvert.html)
- [G9 — `v4l2src`](https://gstreamer.freedesktop.org/documentation/video4linux2/v4l2src.html)

**Tài liệu Linux/V4L2 chính thức**

- [K1 — Image Formats](https://docs.kernel.org/userspace-api/media/v4l/pixfmt.html)
- [K2 — Packed YUV Formats](https://docs.kernel.org/userspace-api/media/v4l/pixfmt-packed-yuv.html)
- [K3 — Single-planar Format Structure](https://docs.kernel.org/userspace-api/media/v4l/pixfmt-v4l2.html)
- [K4 — Multi-planar Format Structures](https://docs.kernel.org/userspace-api/media/v4l/pixfmt-v4l2-mplane.html)
- [K5 — Single- and Multi-planar APIs](https://docs.kernel.org/userspace-api/media/v4l/planar-apis.html)
- [K6 — V4L2 Buffers](https://docs.kernel.org/userspace-api/media/v4l/buffer.html)
- [K7 — Compressed Formats](https://docs.kernel.org/userspace-api/media/v4l/pixfmt-compressed.html)

**CamStream Control**

- [P1 — `GstreamerPipeline` implementation](https://github.com/tungnhs2014/camstream-control/blob/74a6a2045198ecc7c91fe32d48dbe42b52e87faa/libs/camstream-gstreamer/src/gstreamer_pipeline.cpp)
- [P2 — Stage 7 GStreamer integration validation](https://github.com/tungnhs2014/camstream-control/blob/74a6a2045198ecc7c91fe32d48dbe42b52e87faa/docs/validation/stage-07-gstreamer-integration.md)
- [P3 — System overview](https://github.com/tungnhs2014/camstream-control/blob/74a6a2045198ecc7c91fe32d48dbe42b52e87faa/docs/architecture/system-overview.md)
