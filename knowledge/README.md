# Lộ trình kiến thức GStreamer và Embedded Linux camera

Tài liệu trong thư mục này giải thích **contract của GStreamer và V4L2**. Các lab trong `practice/` cho thấy cách quan sát một phần contract đó. Một ví dụ chạy thành công chỉ chứng minh điều ghi trong tiêu chí PASS của ví dụ; nó không tự chứng minh hiệu năng, zero-copy hay tính tương thích với mọi camera.

Đọc [knowledge map](gstreamer-knowledge-map-and-workflows-fixed.md) để xem quan hệ giữa các khái niệm. Đọc từng bài theo thứ tự dưới đây. Mỗi bài có ba lớp nội dung: kiến thức chung, ví dụ minh họa và case study CamStream Control. Kết quả của CamStream Control thuộc dự án đó, không phải kết quả kiểm thử của repo lab này. Các liên kết nguồn CamStream được cố định tại commit `74a6a2045198ecc7c91fe32d48dbe42b52e87faa`; những thay đổi sau commit đó cần được kiểm chứng lại trước khi áp dụng kết luận.

| Thứ tự | Lý thuyết | Thực hành | Điều lab thực sự chứng minh | Phần vẫn là lý thuyết / cần môi trường khác |
| --- | --- | --- | --- | --- |
| 01 | [Tổng quan graph và camera stack](01-gstreamer-overview-camera-embedded-linux-updated.md) | [Lab 01](../practice/lesson-01/README.md) | CLI, element inspection, pipeline hữu hạn, caps quan sát qua `-v` | Ví dụ C++ API trong bài là phần đọc trước cho Lab 02; camera thật chưa được dùng |
| 02 | [Plugin, element, pad, bin, pipeline](02-gstreamer-object-model-plugin-element-pad-bin-pipeline.md) | [Lab 02](../practice/lesson-02/README.md) | 7 ví dụ: factory, template, ownership, static/request/ghost/dynamic pad | Graph CamStream là case study riêng |
| 03 | [Buffer, memory và ownership](03-gstreamer-gstbuffer-gstmemory-data-lifecycle-updated.md) | [Lab 03](../practice/lesson-03/README.md) | 7 ví dụ: map, copy, metadata, pool, `gst_pad_push()`, probe | Pool V4L2/DMABUF và zero-copy trên thiết bị thật |
| 04 | [Raw video, YUY2 và MJPEG](04-gstreamer-raw-video-yuyv-yuy2-mjpeg-formats-final.md) | [Lab 04](../practice/lesson-04/README.md) | 6 ví dụ: byte layout, raw conversion, JPEG encode/decode | `bytesperline`, `sizeimage`, `bytesused` thuộc V4L2; quan sát trực tiếp ở Lab 06 mới chỉ dừng ở enumeration |
| 05 | [Caps và negotiation](05-gstreamer-gstcaps-caps-negotiation-format-contract-final.md) | [Lab 05](../practice/lesson-05/README.md) | 5 ví dụ: tập caps, giao, fixation, template/query/current, runtime failure | Caps features và renegotiation event chưa có lab riêng |
| 06 | [V4L2 và `v4l2src`](06-gstreamer-v4l2-v4l2src-camera-capture-workflow-final.md) | [Lab 06](../practice/lesson-06/README.md) | Device monitor, lỗi thiếu device; có công cụ enumeration/capture cho máy có camera | Capture YUY2/MJPEG trong repo này chưa chạy trên camera thật; `TRY_FMT`/`S_FMT` và queue API được giải thích nhưng chưa lập trình trực tiếp |
| 07 | [State và live source](07-gstreamer-state-machine-live-source-lifecycle-final.md) | [Lab 07](../practice/lesson-07/README.md) | 5 ví dụ: state, preroll, `NO_PREROLL`, restart pipeline tổng hợp | Clock/base-time và policy restart CamStream chưa có phép đo riêng |
| 08 | [Bus, message và lỗi runtime](08-gstreamer-gstbus-gstmessage-runtime-error-handling-final.md) | [Lab 08](../practice/lesson-08/README.md) | 5 ví dụ: polling, EOS, ERROR, STATE_CHANGED, watch | Poll-fd service và sync handler chỉ được giải thích trong bài |
| 09 | [Runtime integration và phỏng vấn](09-gstreamer-runtime-integration-and-interview-guide.md) | [Lab 09](../practice/lesson-09/README.md) | 4 ví dụ tổng hợp: timestamp, hai nhánh `tee`/`queue`, `appsrc` → `appsink`, nhánh queue có drop | Sticky event/seek, QoS, hiệu năng camera thật, latency, drop policy cho sản phẩm và networking vẫn cần lab riêng |

## Kiến thức thiết yếu sau Bài 08

Tám bài đầu là nền móng, chưa phải toàn bộ GStreamer. Trước khi xây hệ thống camera/video cho sản phẩm hoặc phỏng vấn vị trí Embedded Linux multimedia, đọc [runtime integration và câu hỏi thực chiến](09-gstreamer-runtime-integration-and-interview-guide.md), rồi chạy Lab 09. Bài này bao gồm clock/timestamp, stream time và running time, sticky event/seek, `tee`/`queue`, backpressure, QoS, `appsink`/`appsrc`, latency và quy trình debug. Bốn ví dụ tổng hợp của Lab 09 không kiểm chứng QoS, hiệu năng hoặc drop policy cho camera thật.

## Cách kiểm chứng một phát biểu kỹ thuật

1. Tra API/element trong [GStreamer documentation](https://gstreamer.freedesktop.org/documentation/) hoặc V4L2 trong [Linux media userspace API](https://docs.kernel.org/userspace-api/media/v4l/).
2. Kiểm tra factory/property/pad trên bản GStreamer đang dùng bằng `gst-inspect-1.0`.
3. Chạy lab và đọc **current caps, Bus message, state, buffer count** tương ứng; đối với camera, kiểm tra mode bằng `v4l2-ctl` và thử trên đúng thiết bị.
4. Ghi rõ phiên bản, thiết bị, lệnh chạy, kết quả và giới hạn trước khi kết luận.
