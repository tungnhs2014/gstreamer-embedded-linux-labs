# Bài 6 — V4L2, `v4l2src` và quy trình camera capture

> **Lộ trình:** [Bảng đối chiếu lý thuyết ↔ lab](README.md) · [Lab 06](../practice/lesson-06/README.md). Phần CamStream Control là case study của dự án ngoài repo lab; chỉ tiêu chí PASS của lab mới là bằng chứng kiểm chứng tại repo này.

> **Dự án:** CamStream Control
> **Đối tượng:** Người mới học Embedded Linux và GStreamer
> **Phạm vi:** Bài này giải thích cách `v4l2src` sử dụng Linux V4L2 API để cấu hình camera, nhận frame và đưa dữ liệu vào GStreamer graph dưới dạng `GstBuffer`.

---

# 1. Vị trí của V4L2 và `v4l2src`

## 1.1 Mục tiêu và phạm vi bài học

Bài 4 đã giải thích payload YUY2 và MJPEG. Bài 5 đã giải thích cách GStreamer dùng caps để mô tả và thương lượng format. Bài 6 nối format contract đó với camera thật trong Linux:

```text
GStreamer caps
    ↓
v4l2src
    ↓
V4L2 device contract
    ↓
kernel camera driver
    ↓
camera hardware
```

Sau bài này, người học cần:

- hiểu vị trí của V4L2 trong Linux camera stack;
- hiểu vai trò của `/dev/videoN`;
- hiểu `v4l2src` là GStreamer source element sử dụng V4L2 API;
- phân biệt kernel driver, V4L2 device node và GStreamer element;
- chọn đúng image-capture node dựa trên capability;
- phân biệt `capabilities` với `device_caps`;
- hiểu cách V4L2 liệt kê format, frame size và frame interval;
- hiểu vai trò của `VIDIOC_TRY_FMT`, `VIDIOC_S_FMT` và `VIDIOC_G_FMT`;
- phân biệt requested, configured và delivered frame rate;
- hiểu các V4L2 I/O method chính;
- hiểu property `io-mode` của `v4l2src`;
- hiểu mental model của V4L2 streaming queue;
- hiểu trạng thái ownership của queued và dequeued buffer;
- hiểu V4L2 buffer khác `GstBuffer`;
- hiểu các đường copy, export và import ở mức tổng quan;
- không suy luận zero-copy chỉ từ `io-mode=dmabuf`;
- hiểu `device`, `device-fd`, `device-name`, `flags`, `num-buffers` và signal `prepare-format`;
- hiểu cách CamStream cấu hình `v4l2src`;
- debug camera pipeline theo từng tầng.

Bài này không đi sâu vào:

| Nội dung | Bài mở rộng |
|---|---:|
| Byte layout YUY2, stride, `sizeimage`, `bytesused` | Bài 4 |
| Caps intersection, fixation và CAPS event | Bài 5 |
| Chi tiết state machine, live source và `NO_PREROLL` | Bài 7 |
| EOS, ERROR và Bus message handling | Bài 8 |
| Camera controls, crop và selection API | Bài chuyên sâu V4L2 |
| End-to-end zero-copy và platform-specific optimization | Bài tối ưu buffer |

Bài 6 không phải giáo trình native V4L2 application hoàn chỉnh. Các ioctl được trình bày đủ để hiểu cách `v4l2src` nối camera Linux vào GStreamer.

## 1.2 Camera stack trong Embedded Linux

Mô hình tổng quát:

```text
Camera hardware
    ↓
USB / CSI / parallel bus
    ↓
Kernel camera driver
    ↓
V4L2 subsystem
    ↓
Character device node
/dev/videoN
    ↓
Userspace V4L2 API
    ↓
v4l2src
    ↓
GstBuffer
    ↓
GStreamer graph
```

Mỗi tầng có trách nhiệm khác nhau:

| Tầng | Trách nhiệm |
|---|---|
| Camera hardware | Tạo dữ liệu hình ảnh |
| Kernel driver | Điều khiển thiết bị và DMA |
| V4L2 subsystem | Chuẩn hóa userspace API |
| `/dev/videoN` | Điểm truy cập character device |
| `v4l2src` | Ánh xạ V4L2 capture thành GStreamer dataflow |
| GStreamer graph | Decode, convert, hiển thị, ghi hoặc truyền |

Application GStreamer không trực tiếp gọi mọi ioctl V4L2 khi dùng `v4l2src`. Element này đóng gói phần lớn workflow đó.

Tuy nhiên, để cấu hình và debug đúng, application developer vẫn phải hiểu:

- node nào đang được mở;
- node hỗ trợ capability nào;
- mode nào được thiết bị hỗ trợ;
- I/O method nào đang được dùng;
- buffer được giữ và trả lại như thế nào.

## 1.3 Khái niệm V4L2 và `v4l2src`

> **Video4Linux2, viết tắt là V4L2, là userspace API của Linux dùng để làm việc với các thiết bị media như camera capture, video output, codec, tuner và metadata device.**

V4L2 cung cấp:

- character device interface;
- ioctl để query capability;
- ioctl để cấu hình format;
- streaming parameter;
- buffer queue;
- controls;
- event;
- selection và crop API;
- nhiều I/O method.

> **`v4l2src` là GStreamer source element dùng để capture video từ một thiết bị V4L2 và đưa dữ liệu vào GStreamer graph qua source pad.**

Class hierarchy:

```text
GstElement
    ↓
GstBaseSrc
    ↓
GstPushSrc
    ↓
v4l2src
```

Factory và plugin:

```text
Factory:
v4l2src

Plugin:
video4linux2

Classification:
Source/Video
```

Ranh giới cần nhớ:

```text
Kernel driver
    = chạy trong kernel, điều khiển hardware

V4L2
    = API contract giữa kernel và userspace

v4l2src
    = userspace GStreamer element dùng V4L2 API
```

Trong CamStream:

```text
camstream-video
    = synthetic kernel V4L2 driver

v4l2src
    = GStreamer element có sẵn

GstreamerPipeline
    = C++ wrapper xây và điều khiển graph
```

CamStream hiện không triển khai custom GStreamer source element.

---

# 2. Device node và capability

## 2.1 `/dev/videoN` là gì?

V4L2 video device thường được truy cập qua character device node:

```text
/dev/video0
/dev/video1
/dev/video2
...
```

Node number không phải stable hardware identity.

Nó có thể thay đổi khi:

- thứ tự probe driver thay đổi;
- cắm hoặc rút USB camera;
- thêm media device khác;
- module được load theo thứ tự khác;
- hệ thống boot với topology khác.

Một camera có thể tạo nhiều node:

```text
Camera
    ├── image capture node
    ├── metadata capture node
    ├── compressed stream node
    └── auxiliary node
```

Do đó:

```text
node tồn tại
    ≠
node là image capture node phù hợp
```

Property mặc định của `v4l2src` là:

```text
device=/dev/video0
```

Application production không nên phụ thuộc vào mặc định này.

Khi hệ thống dùng udev, có thể tồn tại các symlink ổn định hơn:

```text
/dev/v4l/by-id/
/dev/v4l/by-path/
```

Nhưng chúng không phải lúc nào cũng có trên mọi root filesystem. Buildroot image phải cài đúng udev/mdev rule nếu muốn sử dụng.

## 2.2 `VIDIOC_QUERYCAP`, `capabilities` và `device_caps`

Mọi V4L2 device phải hỗ trợ:

```c
VIDIOC_QUERYCAP
```

Ioctl này điền:

```c
struct v4l2_capability
```

Các field quan trọng:

```text
driver
card
bus_info
version
capabilities
device_caps
```

Quy tắc chọn capability mask:

```cpp
std::uint32_t effective_capabilities =
    (capability.capabilities &
     V4L2_CAP_DEVICE_CAPS) != 0U
        ? capability.device_caps
        : capability.capabilities;
```

Ý nghĩa:

- `capabilities` có thể mô tả capability tổng quát của physical device;
- nếu có `V4L2_CAP_DEVICE_CAPS`, `device_caps` mô tả capability của node đang mở;
- `device_caps` không chứa lại bit `V4L2_CAP_DEVICE_CAPS`.

Đây là điểm quan trọng khi một physical device tạo nhiều `/dev/videoN`.

Các capability thường cần cho camera capture:

| Capability | Ý nghĩa |
|---|---|
| `V4L2_CAP_VIDEO_CAPTURE` | Single-planar video capture |
| `V4L2_CAP_VIDEO_CAPTURE_MPLANE` | Multi-planar video capture |
| `V4L2_CAP_STREAMING` | Streaming I/O được hỗ trợ |
| `V4L2_CAP_READWRITE` | `read()`/`write()` I/O được hỗ trợ |
| `V4L2_CAP_META_CAPTURE` | Metadata capture, không phải image frame |

Một image-capture node thường cần ít nhất:

```text
VIDEO_CAPTURE hoặc VIDEO_CAPTURE_MPLANE
```

và một I/O capability phù hợp:

```text
STREAMING hoặc READWRITE
```

## 2.3 Chọn đúng capture node

Ví dụ helper kiểm tra capability:

```cpp
#include <linux/videodev2.h>

#include <cstdint>

bool is_image_capture_device(
    const v4l2_capability& capability) {
    const std::uint32_t effective =
        (capability.capabilities &
         V4L2_CAP_DEVICE_CAPS) != 0U
            ? capability.device_caps
            : capability.capabilities;

    const bool captures_images =
        (effective &
         V4L2_CAP_VIDEO_CAPTURE) != 0U ||
        (effective &
         V4L2_CAP_VIDEO_CAPTURE_MPLANE) != 0U;

    const bool supports_io =
        (effective &
         V4L2_CAP_STREAMING) != 0U ||
        (effective &
         V4L2_CAP_READWRITE) != 0U;

    return captures_images && supports_io;
}
```

Node metadata có thể có:

```text
V4L2_CAP_META_CAPTURE
```

nhưng không có:

```text
V4L2_CAP_VIDEO_CAPTURE
V4L2_CAP_VIDEO_CAPTURE_MPLANE
```

Nếu application mở metadata node như image source, có thể gặp:

- không có raw/JPEG video format;
- `VIDIOC_S_FMT` dùng buffer type không phù hợp;
- `v4l2src` báo device không hỗ trợ capture;
- pipeline không negotiation được.

## 2.4 Device discovery trong workflow CamStream

Các lệnh hữu ích:

```bash
v4l2-ctl --list-devices
```

```bash
v4l2-ctl \
    --device=/dev/videoX \
    --all
```

```bash
gst-device-monitor-1.0 Video/Source
```

`gst-device-monitor-1.0` giúp khám phá GStreamer source device và caps liên quan. `v4l2-ctl` giúp kiểm tra V4L2 capability và format trực tiếp.

Trong CamStream hiện tại:

```text
validation workflow
    = khám phá node động và chọn node phù hợp

GstreamerPipeline library
    = nhận device_path đã được cấu hình
```

Library không tự quét tất cả `/dev/videoN`. Nó đặt giá trị đã nhận vào property:

```text
v4l2src.device
```

Vì vậy tầng orchestration hoặc service vẫn chịu trách nhiệm cung cấp đúng device path.

---

# 3. Từ `GstCaps` đến V4L2 format

## 3.1 Hai format contract

Bài 5 đã trình bày GStreamer caps:

```text
video/x-raw,
format=YUY2,
width=640,
height=480,
framerate=30/1
```

Ở V4L2, raw mode tương ứng dùng:

```c
V4L2_PIX_FMT_YUYV
```

Mapping:

```text
GStreamer YUY2
    ↔
V4L2 YUYV
```

Cả hai mô tả byte order:

```text
Y0 Cb0 Y1 Cr0
```

Đối với MJPEG:

```text
GStreamer:
image/jpeg

V4L2:
V4L2_PIX_FMT_MJPEG
```

Mô hình:

```text
Requested GstCaps
    ↓
v4l2src
    ↓
V4L2 buffer type + FourCC + size + interval
```

`v4l2src` chịu trách nhiệm ánh xạ giữa hai hệ thống tên và cấu trúc.

## 3.2 Liệt kê format, frame size và frame interval

Native V4L2 application có thể dùng:

```text
VIDIOC_ENUM_FMT
    ↓
VIDIOC_ENUM_FRAMESIZES
    ↓
VIDIOC_ENUM_FRAMEINTERVALS
```

**`VIDIOC_ENUM_FMT`**

Liệt kê pixel/compression format theo buffer type:

```text
V4L2_BUF_TYPE_VIDEO_CAPTURE
```

hoặc:

```text
V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
```

**`VIDIOC_ENUM_FRAMESIZES`**

Liệt kê width và height được hỗ trợ cho một FourCC.

Thiết bị có thể báo:

- discrete sizes;
- continuous range;
- stepwise range.

**`VIDIOC_ENUM_FRAMEINTERVALS`**

Liệt kê interval được hỗ trợ cho:

```text
FourCC + width + height
```

Frame interval là nghịch đảo của frame rate:

```text
timeperframe = 1/30 second
    ↔
framerate = 30/1
```

Không phải driver nào cũng cung cấp đầy đủ enumeration. Khi ioctl trả `EINVAL`, application phải xử lý theo contract của thiết bị thay vì giả định danh sách luôn tồn tại.

Dùng công cụ:

```bash
v4l2-ctl \
    --device=/dev/videoX \
    --list-formats-ext
```

## 3.3 Pad template khác capability của camera cụ thể

`gst-inspect-1.0 v4l2src` hiển thị source-pad template caps rất rộng.

Ví dụ template có thể chứa:

```text
video/x-raw
image/jpeg
video/x-bayer
video/mpeg
...
```

Điều đó chỉ có nghĩa plugin biết cách biểu diễn những loại V4L2 format đó trong GStreamer.

Nó không chứng minh camera hiện tại hỗ trợ tất cả.

```text
v4l2src pad template
    = khả năng tổng quát của plugin

queried caps sau khi chọn device
    = khả năng runtime của node

V4L2 enumeration
    = capability trực tiếp của device
```

Debug phải kiểm tra cả ba tầng.

## 3.4 `VIDIOC_TRY_FMT`, `VIDIOC_S_FMT` và `VIDIOC_G_FMT`

Ba ioctl có trách nhiệm khác nhau.

**`VIDIOC_TRY_FMT`**

```text
Thử format
Không thay đổi driver state
```

Driver có thể sửa:

- width;
- height;
- FourCC;
- field;
- stride;
- `sizeimage`;
- plane count.

`VIDIOC_TRY_FMT` được khuyến nghị mạnh nhưng driver không bắt buộc phải triển khai.

**`VIDIOC_S_FMT`**

```text
Yêu cầu driver đặt format
Driver kiểm tra và điều chỉnh theo hardware
```

Driver có thể normalize yêu cầu thay vì trả lỗi.

Ví dụ application yêu cầu:

```text
641 × 479 YUYV
```

driver có thể trả:

```text
640 × 480 YUYV
```

**`VIDIOC_G_FMT`**

```text
Đọc format hiện tại
```

Sau `VIDIOC_S_FMT`, application phải dùng giá trị được driver trả về hoặc gọi `VIDIOC_G_FMT`.

Không được tiếp tục dùng mù quáng request ban đầu.

Native workflow:

```text
G_FMT
    ↓ lấy baseline
sửa field cần thiết
    ↓
TRY_FMT nếu được hỗ trợ
    ↓
S_FMT
    ↓
đọc giá trị driver trả về
```

Bài 4 đã giải thích:

- `bytesperline`;
- `sizeimage`;
- stride;
- plane layout.

Bài 6 chỉ nhấn mạnh rằng các giá trị thực phải lấy từ V4L2 result.

## 3.5 Frame interval và delivered frame rate

Streaming parameter dùng:

```text
VIDIOC_G_PARM
VIDIOC_S_PARM
```

Field thường quan tâm với capture:

```text
capture.timeperframe
```

Ví dụ:

```text
numerator   = 1
denominator = 30
```

tương đương:

```text
30 fps
```

Các ioctl này là optional. Driver không hỗ trợ có thể trả:

```text
EINVAL
```

Nếu driver hỗ trợ thay đổi interval, nó có thể normalize request và trả interval thực.

Ba tầng cần phân biệt:

```text
Requested:
GStreamer caps framerate=30/1

Configured:
V4L2 timeperframe driver chấp nhận

Delivered:
số completed buffers đo được theo elapsed time
```

Do đó:

```text
caps negotiation 30/1
    ≠
driver chắc chắn cấu hình 1/30
    ≠
camera chắc chắn giao ổn định 30 fps
```

---

# 4. Thuộc tính và trách nhiệm của `v4l2src`

## 4.1 Workflow tổng quát

Ở mức khái niệm, `v4l2src` thực hiện chuỗi công việc:

```text
Nhận device property
    ↓
mở V4L2 node
    ↓
query capability
    ↓
khám phá runtime caps
    ↓
tham gia caps negotiation
    ↓
cấu hình V4L2 format
    ↓
chọn I/O method
    ↓
chuẩn bị capture buffers
    ↓
bắt đầu streaming
    ↓
nhận completed V4L2 buffer
    ↓
tạo hoặc gắn memory vào GstBuffer
    ↓
push GstBuffer qua source pad
    ↓
dừng streaming và giải phóng resource
```

Application không gọi trực tiếp chuỗi ioctl này khi dùng `v4l2src`, nhưng property và caps của application ảnh hưởng đến quyết định bên trong.

## 4.2 Các property và signal trọng tâm

| Tên | Quyền | Vai trò |
|---|---|---|
| `device` | Read/Write | Đường dẫn V4L2 node |
| `device-fd` | Read | File descriptor của node đang mở |
| `device-name` | Read | Tên thiết bị |
| `flags` | Read | Loại capability mà element nhận diện |
| `io-mode` | Read/Write | I/O method |
| `num-buffers` | Read/Write, inherited | Số buffer tạo trước EOS |
| `prepare-format` | Signal | Gọi trước `VIDIOC_S_FMT` |

**`device`**

```cpp
g_object_set(
    G_OBJECT(source),
    "device",
    "/dev/video2",
    nullptr);
```

Không gán cố định một node number nếu platform có thể thay đổi topology.

**`device-fd`**

Property read-only. Nó cho biết file descriptor hiện tại sau khi device được mở.

Không dùng như property để truyền fd do application mở.

**`device-name` và `flags`**

Read-only runtime information.

Chúng chỉ có ý nghĩa đầy đủ sau khi element đã mở/probe device.

**`prepare-format`**

Signal được phát trước khi `v4l2src` gọi:

```text
VIDIOC_S_FMT
```

Nó dành cho thiết bị cần custom preparation trước khi đặt format, ví dụ một số UVC H.264 camera.

CamStream YUY2/MJPEG path hiện không dùng signal này.

## 4.3 `num-buffers`: hữu hạn và không giới hạn

`num-buffers` được kế thừa từ `GstBaseSrc`.

Contract:

```text
num-buffers = N, N > 0
    → tạo N buffer rồi gửi EOS

num-buffers = -1
    → không giới hạn
```

Giá trị mặc định là:

```text
-1
```

Điểm quan trọng:

```text
num-buffers
    ≠
số buffer trong V4L2 capture pool
```

Trong CamStream:

```text
config.buffer_count > 0
    → đặt v4l2src num-buffers

config.buffer_count == 0
    → không đặt property
    → giữ mặc định -1
```

Không được diễn giải:

```text
num-buffers=0 là continuous mode
```

Continuous mode của CamStream là policy của `config.buffer_count == 0`, được thực hiện bằng cách không thay đổi default unlimited của source.

EOS và cách ứng dụng chờ EOS thuộc Bài 8.

## 4.4 Source pad và caps runtime

`v4l2src` có source pad:

```text
src
```

Pad template chứa nhiều media type. Sau khi chọn device và negotiation:

```text
current caps
    = format đang được dùng cho dataflow
```

Ví dụ:

```text
video/x-raw,
format=YUY2,
width=640,
height=480,
framerate=30/1
```

hoặc:

```text
image/jpeg,
width=640,
height=480,
framerate=30/1
```

Current caps mô tả GStreamer dataflow.

Các V4L2 field thấp hơn như:

- `bytesperline`;
- `sizeimage`;
- driver-specific alignment;

có thể không xuất hiện đầy đủ trong caps. Chúng được quản lý qua video info, metadata, allocator và internal V4L2 state.

---

# 5. Các phương thức V4L2 I/O

## 5.1 Tổng quan

V4L2 định nghĩa nhiều cách trao đổi dữ liệu giữa driver và userspace.

| Phương thức | Bản chất |
|---|---|
| Read/write | Application gọi `read()` hoặc `write()` |
| MMAP | Driver cấp buffer; application map buffer |
| USERPTR | Application cấp memory và truyền pointer |
| DMABUF import | Driver nhận fd của DMA-BUF do nơi khác cấp |
| DMABUF export | Buffer của driver được export thành fd để chia sẻ |

Read/write được chọn tự động sau khi mở fd nếu driver hỗ trợ.

Các streaming method thường dùng:

```text
VIDIOC_REQBUFS
QBUF / DQBUF
STREAMON / STREAMOFF
```

Mỗi file descriptor thường dùng một I/O method tại một thời điểm.

## 5.2 `rw`

`rw` dùng classic I/O:

```text
open()
    ↓
read()
    ↓
read()
    ↓
close()
```

Ưu điểm:

- đơn giản;
- ít bước queue setup.

Giới hạn:

- driver có thể không hỗ trợ;
- application ít kiểm soát buffer lifecycle hơn;
- có thể phát sinh copy;
- không phải lựa chọn phù hợp nhất cho mọi camera.

Capability cần kiểm tra:

```text
V4L2_CAP_READWRITE
```

## 5.3 `mmap`

MMAP streaming:

```text
driver cấp capture buffers
    ↓
application mmap vào address space
    ↓
QBUF
    ↓
driver DMA/capture
    ↓
DQBUF
```

Capability tổng quát:

```text
V4L2_CAP_STREAMING
```

Support thực tế được kiểm tra bằng:

```text
VIDIOC_REQBUFS
memory=V4L2_MEMORY_MMAP
```

MMAP tránh copy giữa kernel buffer và một userspace buffer chỉ để application nhìn thấy dữ liệu.

Điều đó không đồng nghĩa toàn GStreamer pipeline zero-copy.

Sau `v4l2src`, dữ liệu vẫn có thể bị copy tại:

- pool compatibility boundary;
- decoder;
- converter;
- mapping;
- sink;
- writable transform.

## 5.4 `userptr`

USERPTR:

```text
application cấp memory
    ↓
QBUF với user pointer
    ↓
driver dùng memory đó
    ↓
DQBUF trả quyền truy cập
```

Support được kiểm tra bằng:

```text
VIDIOC_REQBUFS
memory=V4L2_MEMORY_USERPTR
```

Với USERPTR:

- application chịu trách nhiệm cấp vùng memory phù hợp;
- pages có thể bị lock khi buffer queued;
- pointer và length được truyền trong `v4l2_buffer` hoặc `v4l2_plane`;
- memory không được truy cập khi driver đang giữ buffer.

USERPTR không tự động hiệu quả hơn MMAP. Driver và IOMMU/DMA requirement quyết định chi phí thực tế.

## 5.5 `dmabuf` và `dmabuf-import`

DMA-BUF là cơ chế Linux chia sẻ buffer qua file descriptor giữa các device/driver.

Hai vai trò:

```text
Exporter
    = tạo buffer và export fd

Importer
    = nhận fd và sử dụng buffer
```

Trong `v4l2src`:

```text
io-mode=dmabuf
    = export-oriented capture mode

io-mode=dmabuf-import
    = import buffer từ allocation downstream khi có thể
```

DMABUF import ở V4L2 dùng:

```text
V4L2_MEMORY_DMABUF
```

và truyền fd trong:

```text
v4l2_buffer.m.fd
```

hoặc:

```text
v4l2_plane.m.fd
```

End-to-end zero-copy chỉ khả thi khi:

- capture driver hỗ trợ;
- downstream hỗ trợ cùng memory type;
- caps features tương thích;
- stride, alignment và modifier tương thích;
- allocator/pool negotiation thành công;
- decoder/converter không buộc copy;
- sink import được buffer.

Vì vậy:

```text
io-mode=dmabuf
    ≠
end-to-end zero-copy
```

## 5.6 `io-mode=auto` trong CamStream

Property:

```text
v4l2src.io-mode
```

có các giá trị:

```text
auto
rw
mmap
userptr
dmabuf
dmabuf-import
```

Mặc định:

```text
auto
```

Trong CamStream source code hiện tại không đặt `io-mode`.

Do đó element dùng:

```text
auto
```

Đây là baseline phù hợp cho functional validation vì:

- không ép mode thiết bị không hỗ trợ;
- cho plugin chọn đường phù hợp với driver và allocation;
- giảm platform assumption;
- giữ graph portable hơn.

Khi tối ưu performance, có thể test từng mode. Nhưng phải đo:

- copy count;
- CPU;
- latency;
- dropped frame;
- allocator compatibility;
- stability.

Không đổi `io-mode` chỉ vì tên DMABUF nghe “nhanh hơn”.

---

# 6. Buffer queue và cầu nối sang `GstBuffer`

## 6.1 Mental model của MMAP streaming queue

Workflow native V4L2 MMAP:

```text
open device
    ↓
QUERYCAP
    ↓
S_FMT
    ↓
REQBUFS
    ↓
QUERYBUF cho từng index
    ↓
mmap từng buffer/plane
    ↓
QBUF tất cả buffer
    ↓
STREAMON
    ↓
poll/select
    ↓
DQBUF completed buffer
    ↓
đọc metadata và payload
    ↓
QBUF lại
    ↓
lặp
    ↓
STREAMOFF
    ↓
munmap / close
```

Không phải mọi workflow dùng đúng MMAP. USERPTR và DMABUF có cách chuẩn bị memory khác, nhưng queue state vẫn có cùng ý tưởng:

```text
đưa buffer cho driver
    ↓
driver hoàn thành
    ↓
lấy buffer về userspace
    ↓
trả lại driver
```

## 6.2 `REQBUFS`, `QUERYBUF` và mapping

**`VIDIOC_REQBUFS`**

Yêu cầu một tập buffer cho:

```text
buffer type
+
memory type
```

Ví dụ:

```text
type   = V4L2_BUF_TYPE_VIDEO_CAPTURE
memory = V4L2_MEMORY_MMAP
count  = 4
```

Driver có thể trả số buffer khác request.

Application phải kiểm tra:

```text
returned count
```

Không được giả định luôn nhận đúng count yêu cầu.

`count=0` có thể được dùng để giải phóng hoặc orphan buffer theo capability của driver và đồng thời dừng DMA liên quan.

**`VIDIOC_QUERYBUF`**

Với MMAP, query từng buffer index để lấy:

- offset;
- length;
- plane information;
- flags.

Sau đó application gọi:

```c
mmap()
```

Với multi-planar API, mỗi plane có offset và length riêng và thường cần map riêng.

## 6.3 `QBUF`, `DQBUF`, `STREAMON` và `STREAMOFF`

**`VIDIOC_QBUF`**

Đưa buffer vào incoming queue để driver sử dụng.

Sau khi queue:

```text
buffer thuộc quyền driver
```

Application không được đọc hoặc ghi memory đang locked/queued. Kết quả truy cập có thể undefined.

**`VIDIOC_STREAMON`**

Bắt đầu capture cho buffer type đã cấu hình.

Hardware không bắt đầu điền capture buffer trước khi stream được bật.

**Readiness**

Application có thể dùng:

```text
poll()
select()
```

để chờ completed buffer.

**`VIDIOC_DQBUF`**

Lấy completed buffer khỏi outgoing queue.

Với non-blocking fd:

```text
không có buffer
    → EAGAIN
```

Driver có thể đặt:

```text
V4L2_BUF_FLAG_ERROR
```

để báo lỗi recoverable trên buffer. Payload có thể hỏng nhưng queue vẫn có thể tiếp tục.

**`VIDIOC_STREAMOFF`**

Dừng capture và:

- dừng hoặc hoàn tất DMA theo driver;
- bỏ buffer khỏi queue;
- mở khóa USERPTR/DMABUF;
- đưa queue về trạng thái có thể khởi động lại;
- làm mất completed frame chưa dequeue.

## 6.4 Ownership state của V4L2 buffer

Mental model:

```text
Dequeued / application-owned
    = userspace được phép đọc hoặc chuẩn bị buffer

Queued / driver-owned
    = driver và hardware được phép truy cập buffer
```

State transition:

```text
Application-owned
    ↓ QBUF
Driver-owned
    ↓ capture complete
Done queue
    ↓ DQBUF
Application-owned
```

Không giữ một queued buffer pointer rồi đọc payload trong lúc driver đang dùng nó.

Không requeue buffer trước khi application hoặc downstream hoàn tất việc dùng capture-backed resource.

## 6.5 Native workflow pseudocode

```cpp
open_device();

query_capabilities();
choose_capture_type();

set_format();
set_frame_interval_if_supported();

request_buffers();

for (each_buffer) {
    query_buffer();
    map_buffer();
    queue_buffer();
}

stream_on();

while (running) {
    wait_for_readiness();

    v4l2_buffer buffer =
        dequeue_buffer();

    if ((buffer.flags &
         V4L2_BUF_FLAG_ERROR) != 0U) {
        handle_corrupted_frame();
    } else {
        process_payload(
            buffer.index,
            buffer.bytesused,
            buffer.timestamp);
    }

    queue_buffer(buffer.index);
}

stream_off();
unmap_buffers();
close_device();
```

Đây là mental-model pseudocode, không phải full production implementation.

Production code còn cần:

- `EINTR`;
- `EAGAIN`;
- multi-planar arrays;
- timeout;
- hot unplug;
- source-change event;
- rollback;
- cleanup từng phần;
- thread cancellation;
- timestamp conversion;
- error policy.

## 6.6 Từ V4L2 buffer thành `GstBuffer`

V4L2 buffer và `GstBuffer` là hai object khác nhau.

```text
V4L2 buffer
    = descriptor và queue state giữa driver/userspace

GstBuffer
    = media object trong GStreamer dataflow
```

`v4l2src` làm cầu nối:

```text
Completed V4L2 buffer
    ↓
v4l2src buffer pool
    ↓
GstBuffer
    ├── GstMemory
    ├── timestamp
    ├── flags
    └── metadata
    ↓
source pad
```

Có thể tồn tại nhiều đường.

**Copy path**

```text
V4L2 capture memory
    ↓ copy
GStreamer-owned memory
```

Ưu điểm:

- dễ tương thích;
- capture buffer có thể requeue sớm.

Chi phí:

- memory copy;
- CPU/memory bandwidth.

**Capture-backed path**

```text
GstBuffer
    ↓ giữ reference
V4L2-backed memory
```

Khi downstream còn giữ `GstBuffer`:

```text
capture resource có thể chưa requeue được
```

Ưu điểm:

- có thể giảm copy.

Rủi ro:

- downstream giữ lâu làm cạn capture pool;
- alignment không tương thích;
- một element buộc copy fallback.

**Import path**

Downstream đề xuất pool/memory qua ALLOCATION query. `v4l2src` thử cấu hình driver để capture vào buffer đó. Nếu không tương thích, element có thể fallback sang buffer riêng và downstream phải copy.

Không được giả định một đường cố định cho mọi driver và pipeline.

## 6.7 Buffer pool, backpressure và copy threshold

Bài 3 đã giải thích `GstBufferPool`.

Trong camera path:

```text
capture pool hữu hạn
    ↓
downstream giữ nhiều GstBuffer
    ↓
không đủ buffer để queue
    ↓
source chờ hoặc fallback
```

Hệ quả:

- backpressure;
- tăng latency;
- frame drop;
- buffer starvation;
- copy fallback.

Stage 7 validation ghi nhận GStreamer bật V4L2 copy threshold vì độ chắc chắn về buffer count và zero-copy chưa đủ.

Đây là:

```text
performance observation
```

không phải:

```text
functional failure
```

và cũng không chứng minh:

```text
zero-copy support
```

---

# 7. Quan hệ với lifecycle GStreamer

## 7.1 Resource lifecycle ở mức tổng quan

Khi application gọi:

```cpp
gst_element_set_state(
    pipeline,
    GST_STATE_PLAYING);
```

GStreamer đi qua các intermediate state cần thiết.

Ở mức tổng quát:

```text
NULL
    ↓
READY
    thiết bị có thể được mở/probe
    ↓
PAUSED
    negotiation và resource preparation
    ↓
PLAYING
    live capture tạo buffer
```

Khi dừng:

```text
PLAYING
    ↓
PAUSED
    ↓
READY
    streaming resource được dừng
    ↓
NULL
    device được đóng và release
```

Thời điểm chi tiết từng resource được mở hoặc cấp phát phụ thuộc implementation của element.

Bài 7 sẽ giải thích chính xác:

- state transition;
- live source;
- `NO_PREROLL`;
- asynchronous state change;
- pipeline clock;
- restart contract.

## 7.2 Live capture

Camera là live source:

```text
frame tồn tại theo thời gian thực
```

Không giống file source:

```text
application không thể pause camera vô thời hạn
rồi quay lại lấy chính xác frame đã bỏ lỡ
```

`v4l2src` chỉ tạo dataflow live khi pipeline ở state phù hợp.

Chi tiết:

- `PAUSED`;
- preroll;
- `GST_STATE_CHANGE_NO_PREROLL`;
- timestamp và running time;

thuộc Bài 7.

## 7.3 Hữu hạn và liên tục trong CamStream

**Finite run**

CamStream:

```text
config.buffer_count > 0
```

đặt:

```text
v4l2src.num-buffers = buffer_count
```

Sau khi source tạo đủ buffer:

```text
EOS event đi downstream
    ↓
EOS message xuất hiện trên Bus
```

**Continuous run**

CamStream:

```text
config.buffer_count == 0
```

không đặt `num-buffers`, giữ default:

```text
-1
```

Source tiếp tục cho đến khi:

- application dừng pipeline;
- thiết bị lỗi;
- downstream lỗi;
- một EOS khác được đưa vào graph.

Bài 8 sẽ giải thích:

- EOS event;
- EOS message;
- ERROR;
- Bus consumer;
- clean shutdown.

---

# 8. Áp dụng vào CamStream Control

## 8.1 Cách CamStream cấu hình `v4l2src`

CamStream tạo source:

```cpp
GstElement* source =
    create_and_add_element(
        "v4l2src",
        "camera-source");
```

Sau đó:

1. kiểm tra property `device`;
2. xác định finite hay continuous run;
3. nếu finite, kiểm tra property `num-buffers`;
4. đặt `device`;
5. nếu finite, đặt `num-buffers`;
6. đọc lại property để xác nhận.

Finite configuration:

```cpp
g_object_set(
    G_OBJECT(source),
    "device",
    config.device_path.c_str(),
    "num-buffers",
    requested_buffers,
    nullptr);
```

Continuous configuration:

```cpp
g_object_set(
    G_OBJECT(source),
    "device",
    config.device_path.c_str(),
    nullptr);
```

Không có cấu hình:

```text
io-mode
```

nên `v4l2src` giữ default:

```text
auto
```

## 8.2 Synthetic V4L2 path

Mô hình:

```text
camstream-video kernel module
    ↓
synthetic V4L2 capture node
    ↓
v4l2src
    ↓
video/x-raw,format=YUY2
    ↓
capsfilter
    ↓
fakesink
```

Validation ghi nhận:

```text
YUY2
640 × 480
30/1
```

Node được đăng ký động là:

```text
video2
```

trong một session cụ thể.

Không được biến kết quả đó thành ABI:

```text
synthetic driver luôn là /dev/video2
```

Ba lần chạy 300 buffer đã:

- đạt EOS;
- trả exit status 0;
- về `NULL`;
- giải phóng pipeline;
- unload module sạch trong phạm vi quan sát.

Validation cũng ghi nhận copy threshold và không tuyên bố zero-copy.

## 8.3 Logitech C270 YUY2 path

Graph:

```text
Logitech C270
    ↓ V4L2 YUYV
v4l2src
    ↓ GStreamer YUY2
capsfilter
    ↓
fakesink
```

Accepted mode:

```text
video/x-raw,
format=YUY2,
width=320,
height=240,
framerate=30/1
```

Một direct-USB GStreamer run đạt khoảng:

```text
29.1 buffers/s
```

nhưng các native V4L2 test sau đó thường khoảng:

```text
19–20 buffers/s
```

Do đó project không tuyên bố stable delivered 30 fps.

## 8.4 Logitech C270 MJPEG path

Graph:

```text
Logitech C270
    ↓ V4L2 MJPEG
v4l2src
    ↓ image/jpeg
capsfilter
    ↓
jpegdec
    ↓ video/x-raw
videoconvert
    ↓
fakesink
```

Accepted source caps:

```text
image/jpeg,
width=640,
height=480,
framerate=30/1
```

Validation ghi nhận `jpegdec` tạo:

```text
video/x-raw,
format=I420
```

trong các lần chạy đó.

300 buffer hoàn tất khoảng:

```text
15.3–15.6 seconds
```

tương đương:

```text
19–20 buffers/s
```

MJPEG passthrough:

```text
v4l2src
→ image/jpeg
→ fakesink
```

cho kết quả tương tự. Native V4L2 capture cũng tái hiện tốc độ thấp hơn.

Kết luận có bằng chứng:

```text
software JPEG decoder chưa được chứng minh
là điểm nghẽn chính
```

Root cause chính xác chưa được xác định.

## 8.5 Phạm vi bằng chứng

**Đã chứng minh**

- synthetic node được `v4l2src` mở;
- YUY2 synthetic path hoạt động;
- C270 YUY2 path hoạt động;
- C270 MJPEG decode path hoạt động;
- caps negotiation thành công cho các mode đã ghi nhận;
- finite runs đạt EOS;
- pipeline quay về `NULL`;
- native V4L2 và GStreamer isolation đã được so sánh.

**Chưa chứng minh**

- end-to-end zero-copy;
- stable delivered 30 fps trên C270;
- long-duration USB reliability;
- production performance;
- mọi `io-mode`;
- DMABUF compatibility;
- crop support của synthetic driver;
- root cause chính xác của throughput 19–20 fps.

---

# 9. Debug theo từng tầng và tổng kết

## 9.1 Quy trình debug

Không debug bằng cách sửa ngẫu nhiên GStreamer graph.

Đi theo tầng:

```text
1. Hardware và kết nối
    ↓
2. Kernel driver
    ↓
3. Device node
    ↓
4. Capability
    ↓
5. Format và frame interval
    ↓
6. Native V4L2 capture
    ↓
7. v4l2src
    ↓
8. Exact GstCaps
    ↓
9. Decoder / converter
    ↓
10. Sink và application
```

**Tầng 1 — Hardware**

Kiểm tra:

- camera có được enumerate;
- USB reset;
- nguồn điện;
- cable;
- hub topology;
- CSI connector.

**Tầng 2 — Driver**

```bash
dmesg
```

```bash
lsmod
```

**Tầng 3 — Device discovery**

```bash
v4l2-ctl --list-devices
```

```bash
ls -l /dev/video*
```

**Tầng 4 — Capability**

```bash
v4l2-ctl \
    --device=/dev/videoX \
    --all
```

**Tầng 5 — Format**

```bash
v4l2-ctl \
    --device=/dev/videoX \
    --list-formats-ext
```

**Tầng 6 — Native capture**

```bash
v4l2-ctl \
    --device=/dev/videoX \
    --set-fmt-video=width=640,height=480,pixelformat=MJPG \
    --set-parm=30 \
    --stream-mmap \
    --stream-count=300 \
    --stream-to=/dev/null
```

**Tầng 7–10 — GStreamer**

```bash
gst-inspect-1.0 v4l2src
```

```bash
gst-launch-1.0 -v \
    v4l2src device=/dev/videoX num-buffers=300 ! \
    "image/jpeg,width=640,height=480,framerate=30/1" ! \
    jpegdec ! \
    videoconvert ! \
    fakesink sync=false
```

## 9.2 Lỗi thường gặp

**Gán cố định `/dev/video0`**

Node number thay đổi. Dùng discovery hoặc symlink ổn định nếu platform cung cấp.

**Chọn metadata node**

Kiểm tra capability thực của node bằng `device_caps`.

**Nhìn `gst-inspect` rồi kết luận camera hỗ trợ mode**

Pad template là capability của plugin, không phải camera cụ thể.

**Không đọc lại kết quả `S_FMT`**

Driver có thể normalize width, height, stride và format.

**Nhầm requested FPS với delivered FPS**

Cần đo elapsed time và completed buffer count.

**Nhầm `num-buffers` với pool size**

`num-buffers` chỉ giới hạn số buffer source tạo trước EOS.

**Đặt `num-buffers=0` để chạy liên tục**

Contract unlimited là `-1`. CamStream dùng config value 0 để quyết định không đặt property.

**Truy cập queued V4L2 buffer**

Khi queued, driver sở hữu quyền truy cập.

**Không requeue dequeued buffer**

Capture queue dần cạn và streaming dừng hoặc chờ.

**Giữ capture-backed `GstBuffer` quá lâu**

Có thể gây buffer starvation hoặc copy fallback.

**Ép `io-mode=dmabuf` và tuyên bố zero-copy**

Cần chứng minh toàn bộ memory path.

**Quy mọi “Internal data stream error” cho USB**

Lỗi có thể đến từ:

- format;
- negotiation;
- driver;
- device removal;
- queue;
- decoder;
- downstream flow return.

Phải đọc debug log và native V4L2 evidence.

## 9.3 Kiến thức cần nhớ

Mô hình:

```text
GstCaps
    ↓
v4l2src
    ↓
V4L2 format + I/O method
    ↓
V4L2 capture queue
    ↓
GstBuffer
    ↓
GStreamer graph
```

Các ý trọng tâm:

1. V4L2 là Linux userspace API cho media device.
2. `/dev/videoN` là device node, không phải stable camera identity.
3. Một camera có thể tạo nhiều node với capability khác nhau.
4. Khi có `V4L2_CAP_DEVICE_CAPS`, phải dùng `device_caps` cho node đang mở.
5. Image capture cần `VIDEO_CAPTURE` hoặc `VIDEO_CAPTURE_MPLANE`.
6. Metadata node không mang image frame thông thường.
7. `v4l2src` là userspace GStreamer source element, không phải kernel driver.
8. Pad template của `v4l2src` không chứng minh camera cụ thể hỗ trợ mọi format.
9. V4L2 `YUYV` ánh xạ sang GStreamer `YUY2`.
10. V4L2 `MJPEG` ánh xạ sang `image/jpeg`.
11. `ENUM_FMT`, `ENUM_FRAMESIZES` và `ENUM_FRAMEINTERVALS` khám phá mode khi driver hỗ trợ.
12. `TRY_FMT` thử format mà không thay đổi state.
13. `S_FMT` có thể normalize request và trả format thực.
14. `G_FMT` đọc format hiện tại.
15. `G_PARM`/`S_PARM` là optional và không chứng minh delivered FPS.
16. `rw`, `mmap`, `userptr`, `dmabuf` và `dmabuf-import` có ownership khác nhau.
17. `io-mode=auto` để `v4l2src` chọn mode phù hợp.
18. `io-mode=dmabuf` không chứng minh end-to-end zero-copy.
19. MMAP workflow gồm `REQBUFS`, `QUERYBUF`, mapping, `QBUF`, `STREAMON`, `DQBUF`, requeue và `STREAMOFF`.
20. Queued buffer thuộc quyền driver; dequeued buffer thuộc quyền userspace.
21. V4L2 buffer khác `GstBuffer`.
22. `v4l2src` có thể dùng copy, capture-backed hoặc import path tùy negotiation.
23. Downstream giữ buffer lâu có thể làm cạn capture pool.
24. `num-buffers=-1` là unlimited.
25. Trong CamStream, `buffer_count==0` nghĩa là giữ default unlimited, không phải đặt `num-buffers=0`.
26. CamStream hiện giữ `io-mode=auto`.
27. Functional capture không phải bằng chứng zero-copy hoặc stable FPS.
28. Debug phải cô lập native V4L2 trước khi kết luận lỗi thuộc GStreamer.

## 9.4 Nguồn tham khảo

**Tài liệu GStreamer chính thức**

- [G1 — `v4l2src`](https://gstreamer.freedesktop.org/documentation/video4linux2/v4l2src.html)
- [G2 — Video4Linux2 plugin](https://gstreamer.freedesktop.org/documentation/video4linux2/)
- [G3 — `GstBaseSrc`](https://gstreamer.freedesktop.org/documentation/base/gstbasesrc.html)
- [G4 — Source elements](https://gstreamer.freedesktop.org/documentation/additional/design/element-source.html)
- [G5 — Memory allocation](https://gstreamer.freedesktop.org/documentation/plugin-development/advanced/allocation.html)
- [G6 — DMA buffers](https://gstreamer.freedesktop.org/documentation/additional/design/dmabuf.html)
- [G7 — Live sources](https://gstreamer.freedesktop.org/documentation/additional/design/live-source.html)
- [G8 — `gst-device-monitor-1.0`](https://gstreamer.freedesktop.org/documentation/tools/gst-device-monitor.html)
- [G9 — `gst-launch-1.0`](https://gstreamer.freedesktop.org/documentation/tools/gst-launch.html)

**Tài liệu Linux/V4L2 chính thức**

- [K1 — V4L2 Userspace API](https://docs.kernel.org/userspace-api/media/v4l/v4l2.html)
- [K2 — Opening and closing devices](https://docs.kernel.org/userspace-api/media/v4l/open.html)
- [K3 — `VIDIOC_QUERYCAP`](https://docs.kernel.org/userspace-api/media/v4l/vidioc-querycap.html)
- [K4 — Data formats](https://docs.kernel.org/userspace-api/media/v4l/format.html)
- [K5 — `VIDIOC_ENUM_FMT`](https://docs.kernel.org/userspace-api/media/v4l/vidioc-enum-fmt.html)
- [K6 — `VIDIOC_ENUM_FRAMESIZES`](https://docs.kernel.org/userspace-api/media/v4l/vidioc-enum-framesizes.html)
- [K7 — `VIDIOC_ENUM_FRAMEINTERVALS`](https://docs.kernel.org/userspace-api/media/v4l/vidioc-enum-frameintervals.html)
- [K8 — `VIDIOC_G_FMT`, `VIDIOC_S_FMT`, `VIDIOC_TRY_FMT`](https://docs.kernel.org/userspace-api/media/v4l/vidioc-g-fmt.html)
- [K9 — Streaming parameters](https://docs.kernel.org/userspace-api/media/v4l/streaming-par.html)
- [K10 — Input/Output methods](https://docs.kernel.org/userspace-api/media/v4l/io.html)
- [K11 — Memory-mapped streaming](https://docs.kernel.org/userspace-api/media/v4l/mmap.html)
- [K12 — USERPTR streaming](https://docs.kernel.org/userspace-api/media/v4l/userp.html)
- [K13 — DMA-BUF importing](https://docs.kernel.org/userspace-api/media/v4l/dmabuf.html)
- [K14 — V4L2 buffers](https://docs.kernel.org/userspace-api/media/v4l/buffer.html)
- [K15 — `VIDIOC_REQBUFS`](https://docs.kernel.org/userspace-api/media/v4l/vidioc-reqbufs.html)
- [K16 — `VIDIOC_QUERYBUF`](https://docs.kernel.org/userspace-api/media/v4l/vidioc-querybuf.html)
- [K17 — `VIDIOC_QBUF`, `VIDIOC_DQBUF`](https://docs.kernel.org/userspace-api/media/v4l/vidioc-qbuf.html)
- [K18 — `VIDIOC_STREAMON`, `VIDIOC_STREAMOFF`](https://docs.kernel.org/userspace-api/media/v4l/vidioc-streamon.html)
- [K19 — Official V4L2 capture example](https://docs.kernel.org/userspace-api/media/v4l/capture.c.html)

**CamStream Control**

- [P1 — `GstreamerPipeline` implementation](https://github.com/tungnhs2014/camstream-control/blob/74a6a2045198ecc7c91fe32d48dbe42b52e87faa/libs/camstream-gstreamer/src/gstreamer_pipeline.cpp)
- [P2 — `GstreamerPipeline` interface](https://github.com/tungnhs2014/camstream-control/blob/74a6a2045198ecc7c91fe32d48dbe42b52e87faa/libs/camstream-gstreamer/include/camstream/gstreamer_pipeline.hpp)
- [P3 — Stage 7 GStreamer integration validation](https://github.com/tungnhs2014/camstream-control/blob/74a6a2045198ecc7c91fe32d48dbe42b52e87faa/docs/validation/stage-07-gstreamer-integration.md)
- [P4 — Stage 8.2 service integration validation](https://github.com/tungnhs2014/camstream-control/blob/74a6a2045198ecc7c91fe32d48dbe42b52e87faa/docs/validation/stage-08.2-gstreamer-service-integration.md)
