# Bài 09 — Runtime integration và câu hỏi GStreamer thường gặp khi làm việc

> **Vị trí:** Đọc sau [Bài 08](08-gstreamer-gstbus-gstmessage-runtime-error-handling-final.md). [Lab 09](../practice/lesson-09/README.md) kiểm chứng bốn hành vi cơ bản bằng nguồn tổng hợp. Các lệnh và ví dụ dưới đây không phải bằng chứng về hiệu năng trên camera thật.

## 1. Clock, timestamp và đồng bộ

`GstBuffer` có thể mang PTS, DTS và duration ([Bài 03](03-gstreamer-gstbuffer-gstmemory-data-lifecycle-updated.md)). PTS không tự nó là thời điểm trên đồng hồ hệ thống. `SEGMENT` đứng trước buffer xác định cách đổi timestamp sang **running time**; khi pipeline đang `PLAYING`, running time của pipeline bằng `clock time − base time`. Sink đồng bộ so sánh running time của buffer với running time của clock để quyết định thời điểm render. `sync=false` bỏ bước chờ đồng bộ tại sink đó, nhưng không xóa state machine, timestamp hay mọi nguồn trễ.

Phân biệt ba mốc: **buffer timestamp** thuộc timeline của dữ liệu; **stream time** là vị trí trong nội dung, thường dùng cho position/seek; **running time** dùng để đồng bộ với clock. Sau seek hoặc đổi tốc độ phát, không được coi ba giá trị này là một. Với `GstSegment` định dạng `GST_FORMAT_TIME`, dùng `gst_segment_to_running_time()` khi cần chuyển timestamp; kiểm tra kết quả hợp lệ trước khi dùng. [Tài liệu đồng bộ chính thức](https://gstreamer.freedesktop.org/documentation/additional/design/synchronisation.html) trình bày cả base time, segment offset và playback rate.

Khi hỏi “caps ghi 30/1 có nghĩa camera giao đúng 30 fps không?”, câu trả lời là **không**. Caps mô tả format đã thỏa thuận. Muốn biết tốc độ giao thực tế phải đo thời điểm nhận frame, frame count trong khoảng thời gian, dropped frame và bối cảnh sink sync. Muốn biết latency đầu cuối phải đo qua các mốc capture, queue, decode, xử lý và output với clock cùng hệ quy chiếu.

Tài liệu chuẩn: [Clocks and synchronization](https://gstreamer.freedesktop.org/documentation/application-development/advanced/clocks.html), [GstBuffer](https://gstreamer.freedesktop.org/documentation/gstreamer/gstbuffer.html).

## 2. `tee`, `queue`, thread và backpressure

`tee` tạo nhiều nhánh từ một luồng. Đặt `queue` sau mỗi nhánh `tee` khi muốn các nhánh có streaming thread riêng:

```bash
gst-launch-1.0 -v \
  videotestsrc is-live=true num-buffers=30 ! tee name=t \
  t. ! queue ! fakesink sync=false \
  t. ! queue ! fakesink sync=false
```

`queue` mặc định **chặn thread đẩy vào** khi đạt một trong các giới hạn buffers/bytes/time. Điều này tạo backpressure lên upstream. Với nhánh xử lý chậm, phải chọn policy rõ ràng: chấp nhận chờ để giữ frame, hoặc giới hạn queue và drop theo yêu cầu latency. Ví dụ `queue max-size-buffers=2 max-size-bytes=0 max-size-time=0 leaky=downstream` giới hạn theo số buffer và cho phép bỏ buffer cũ khi đầy. Cần đo frame được giao và độ trễ trước khi khẳng định policy phù hợp; chỉ nhìn graph không đủ.

[Lab 09, ví dụ 04](../practice/lesson-09/src/04_queue_drop.cpp) tạo nhánh chậm có chủ đích và so sánh số buffer hai nhánh. Kết quả ít buffer hơn ở nhánh chậm chứng minh **drop trong ví dụ tổng hợp đó**, không chứng minh latency của camera thật.

Không được suy luận rằng thêm `queue` luôn làm nhanh hơn. Nó tách thread và thay đổi nơi tích tụ dữ liệu. Một nhánh bị chặn vẫn có thể ảnh hưởng graph tùy chính sách từng nhánh.

Tài liệu chuẩn: [tee](https://gstreamer.freedesktop.org/documentation/coreelements/tee.html), [queue](https://gstreamer.freedesktop.org/documentation/coreelements/queue.html).

## 3. `appsink` và `appsrc`: ranh giới với ứng dụng C++

`appsink` cho ứng dụng lấy `GstSample`; sample chứa buffer và caps tương ứng. Khi dùng `gst_app_sink_try_pull_sample()`, application sở hữu sample trả về và phải `gst_sample_unref()` sau khi xử lý. Nếu application lấy mẫu chậm, queue nội bộ của appsink có thể tăng sử dụng bộ nhớ. Đặt giới hạn và chọn drop policy theo mục tiêu. `max-buffers` có trên GStreamer 1.20; `max-bytes` và `max-time` chỉ có từ 1.24; `leaky-type` (chọn bỏ buffer cũ hay mới) chỉ có từ 1.28. Bản 1.20 dùng property `drop` để bỏ buffer cũ khi đầy. Kiểm tra `gst-inspect-1.0 appsink` trên target trước khi đặt property. Callback `new-sample` chạy trong streaming context; tránh xử lý nặng hoặc chờ lâu trong callback.

`appsrc` cho ứng dụng đưa dữ liệu vào pipeline. Phải khai báo caps phù hợp và gắn timestamp/duration đúng với stream type. Action signal `push-buffer` **không lấy mất reference của caller**; appsrc tự lấy thêm reference, nên caller có thể unref reference của mình sau lời gọi. Trái lại, API C `gst_app_src_push_buffer()` **lấy ownership** của reference truyền vào. [Lab 09, ví dụ 03](../practice/lesson-09/src/03_appsrc_appsink.cpp) dùng API C này. Nếu queue appsrc đầy và `block=true`, lời gọi có thể block.

Tài liệu chuẩn: [appsink](https://gstreamer.freedesktop.org/documentation/app/appsink.html), [appsrc](https://gstreamer.freedesktop.org/documentation/app/appsrc.html), [application pipeline manipulation](https://gstreamer.freedesktop.org/documentation/application-development/advanced/pipeline-manipulation.html).

## 4. Event, query và message là ba đường giao tiếp khác nhau

- **`GstEvent`** mang điều kiện của dataflow, ví dụ `CAPS`, `SEGMENT`, `EOS`, `SEEK`, `FLUSH` và `RECONFIGURE`. Hướng truyền phụ thuộc loại event. Một EOS event đi trong pipeline; chỉ khi pipeline hoàn tất xử lý, ứng dụng mới thấy EOS message trên Bus. [Bài 05](05-gstreamer-gstcaps-caps-negotiation-format-contract-final.md) và [Bài 08](08-gstreamer-gstbus-gstmessage-runtime-error-handling-final.md) giải thích hai trường hợp này.
- **`GstQuery`** hỏi một phần của graph để lấy câu trả lời, như caps, duration, position, latency hoặc allocation. Query position/duration không tự seek; muốn đổi vị trí phát phải dùng seek event. Giá trị duration có thể chưa biết hoặc không hữu ích cho live source.
- **`GstMessage`** báo thông tin runtime từ graph về ứng dụng qua `GstBus`, ví dụ ERROR, EOS, STATE_CHANGED, BUFFERING, CLOCK_LOST. Message không thay thế buffer hay CAPS event.

Tài liệu chuẩn: [GstEvent](https://gstreamer.freedesktop.org/documentation/gstreamer/gstevent.html), [GstQuery](https://gstreamer.freedesktop.org/documentation/gstreamer/gstquery.html), [GstBus](https://gstreamer.freedesktop.org/documentation/gstreamer/gstbus.html), [seeking and queries](https://gstreamer.freedesktop.org/documentation/application-development/advanced/queryevents.html).

### Thứ tự event và seek

Một stream thông thường bắt đầu bằng `STREAM_START`, tiếp theo là `CAPS` để báo format, rồi `SEGMENT` trước buffer đầu tiên. Các event này là *sticky*: pad giữ thông tin cần thiết để một nhánh mới có thể nhận đúng ngữ cảnh stream. Khi tự viết source, pad probe hoặc thay nhánh đang chạy, không được đẩy buffer trước `SEGMENT`; sau khi đổi caps phải gửi caps mới trước buffer theo format mới. Một flushing seek có thể tạo `SEGMENT` mới mà không tạo `STREAM_START` mới. `FLUSH_START`/`FLUSH_STOP` làm gián đoạn dataflow và xóa trạng thái EOS; sau EOS thông thường không đẩy tiếp buffer cho tới khi luồng được reset theo contract.

Ví dụ thứ tự cần quan sát trong log/pad probe:

```text
STREAM_START → CAPS → SEGMENT → buffer ... → EOS event → EOS Bus message
```

`CAPS` có thể xuất hiện lại khi renegotiate; thứ tự trên mô tả một đoạn stream bình thường, không thay thế toàn bộ state machine. Xem [Streams](https://gstreamer.freedesktop.org/documentation/additional/design/streams.html), [Events](https://gstreamer.freedesktop.org/documentation/additional/design/events.html) và [GstPad sticky events](https://gstreamer.freedesktop.org/documentation/gstreamer/gstpad.html).

### Drop vì queue và QoS

`queue leaky=downstream` chủ động bỏ buffer cũ **tại queue** khi đạt giới hạn. QoS là cơ chế khác: một element, thường là sink đồng bộ, đo dữ liệu sớm/muộn so với clock và có thể gửi `QOS` event **upstream** để các element thích hợp điều chỉnh tốc độ hoặc bỏ frame. Khi element quyết định drop vì QoS, nó có thể post `GST_MESSAGE_QOS` lên Bus. Vì vậy một nhánh có ít frame hơn không tự cho biết frame bị bỏ tại queue, sink hay upstream. Muốn chẩn đoán phải đọc queue level/drop counter (nếu phiên bản hỗ trợ), QoS message và buffer count ở các pad liên quan. [Tài liệu QoS chính thức](https://gstreamer.freedesktop.org/documentation/additional/design/qos.html).

## 5. Container, codec, decoder và pad động

**Container** (ví dụ MP4, Matroska) tổ chức các stream trong file hoặc luồng truyền; **codec** (ví dụ H.264, H.265, JPEG) quy định cách mã hóa payload. Demuxer tách container thành elementary streams; parser xác định ranh giới bitstream và chuẩn bị dữ liệu theo codec; decoder biến compressed data thành raw media; converter đổi raw format. Vì vậy `jpegdec` và `videoconvert` làm hai việc khác nhau ([Bài 04](04-gstreamer-raw-video-yuyv-yuy2-mjpeg-formats-final.md)).

Một demuxer hoặc `decodebin` thường chỉ biết các output pad sau khi đọc stream. Ứng dụng cần xử lý `pad-added`, kiểm tra caps của pad mới, link đúng nhánh và quản lý callback lifetime ([Lab 02, ví dụ 07](../practice/lesson-02/src/07_dynamic_pad.cpp)). Một file có cả audio và video có thể tạo nhiều pad; không được link mọi pad vào cùng một video sink. RTSP/RTP và muxing cần bài chuyên sâu vì còn transport, jitter, timestamp và lỗi mạng.

Tài liệu chuẩn: [dynamic pipelines](https://gstreamer.freedesktop.org/documentation/tutorials/basic/dynamic-pipelines.html), [first GStreamer application](https://gstreamer.freedesktop.org/documentation/application-development/basics/helloworld.html).

## 6. Quy trình debug từ thiết bị tới ứng dụng

1. **Thiết bị:** xác định `/dev/videoN` là capture node; dùng `v4l2-ctl --list-devices` và `--list-formats-ext`. Nếu open/streaming thất bại, đọc errno và `dmesg`.
2. **Plugin:** dùng `gst-inspect-1.0` xác nhận factory, property, pad template và phiên bản GStreamer đang được load.
3. **Graph:** thử pipeline tối thiểu `source ! fakesink`, sau đó thêm từng `capsfilter`, decoder, converter, queue và sink. Chạy `gst-launch-1.0 -v` để xem current caps.
4. **Runtime:** đọc `ERROR` và debug string trên Bus. Phân biệt `NOT_LINKED`, `NOT_NEGOTIATED`, lỗi resource, timeout và EOS. `GST_DEBUG` theo category giúp cô lập tầng lỗi; `GST_DEBUG_DUMP_DOT_DIR` có thể tạo graph để xem đường link và caps.
5. **Hiệu năng:** đo delivered FPS, queue level, drop count, CPU, memory, USB bandwidth và latency đầu cuối trên phần cứng đích. Một pipeline chạy đạt EOS không chứng minh các chỉ số này.

Tài liệu chuẩn: [GStreamer debugging tools](https://gstreamer.freedesktop.org/documentation/tutorials/basic/debugging-tools.html), [Bus](https://gstreamer.freedesktop.org/documentation/application-development/basics/bus.html).

## 7. Câu hỏi tự kiểm tra theo kiểu phỏng vấn

| Câu hỏi | Một câu trả lời đạt yêu cầu |
| --- | --- |
| `GstElement`, `GstPad`, `GstBin`, `GstPipeline` khác nhau thế nào? | Element là đơn vị xử lý; pad là cổng nối và truyền data/event/query; bin chứa element; pipeline là bin cấp cao quản lý clock, Bus và state. |
| Link thành công có bảo đảm pipeline chạy được không? | Không. Static link có thể thành công nhưng runtime caps negotiation, thiết bị hoặc decoder vẫn thất bại. Kiểm tra current caps và Bus. |
| Template caps, queried caps và current caps khác nhau thế nào? | Template là khả năng của loại pad; query phản ánh khả năng tại thời điểm hỏi; current là format fixed của stream đã negotiation. |
| Ai quyết định format trong push-mode? | Downstream đề xuất bằng CAPS query; upstream chọn format, gửi CAPS event xuống. |
| Khi nào caller mất quyền sở hữu buffer? | Tùy API. `gst_pad_push()` và `gst_app_src_push_buffer()` lấy reference caller; action signal `appsrc::push-buffer` tự ref và không lấy reference caller. |
| Vì sao `NO_PREROLL` không phải lỗi? | Live source thường chỉ tạo dữ liệu trong PLAYING, nên PAUSED không thể preroll theo kiểu non-live. |
| EOS và ERROR khác nhau ra sao? | EOS là kết thúc dataflow; ERROR là lỗi. Ứng dụng quyết định EOS có phải kết thúc mong đợi hay không theo mode. |
| YUYV, YUY2 và MJPEG khác nhau thế nào? | V4L2 YUYV và GStreamer YUY2 cùng thứ tự byte packed 4:2:2; MJPEG là dữ liệu JPEG nén, cần decode để xử lý raw pixel. |
| Vì sao caps 30/1 không chứng minh 30 fps thực? | Caps là format contract; cần đo delivered frames và thời gian trên thiết bị thực. |
| Vì sao đặt `queue` sau `tee`? | Cho nhánh có thread riêng; vẫn phải đặt giới hạn và policy backpressure/drop phù hợp. |
| `appsink` làm bộ nhớ tăng khi nào? | Application pull chậm hơn tốc độ nguồn và queue không được giới hạn hoặc drain kịp. |
| Event, query và Bus message khác nhau thế nào? | Event tác động/thông báo trên dataflow; query yêu cầu câu trả lời từ graph; message báo runtime về application. |
| `STREAM_START`, `CAPS`, `SEGMENT` dùng khi nào? | Chúng thiết lập danh tính stream, format và timeline trước buffer; `SEGMENT` mới có thể xuất hiện sau seek mà không có `STREAM_START` mới. |
| Running time khác stream time thế nào? | Running time dùng đồng bộ với clock; stream time là vị trí trong nội dung để query/seek. Segment nối timestamp với hai timeline này. |
| Queue drop khác QoS drop thế nào? | Queue leaky bỏ tại queue khi đầy; QoS dựa trên timing và có thể yêu cầu upstream điều chỉnh hoặc bỏ frame. Cần đo tại các pad để biết vị trí drop. |
| Demuxer, parser, decoder và converter khác nhau thế nào? | Tách container, xử lý bitstream codec, giải nén thành raw media, rồi đổi raw format. |
| Debug camera không lên hình theo thứ tự nào? | Device/capability → plugin → graph tối thiểu → current caps → Bus error/debug → decoder/sink → đo runtime. |

## 8. Ranh giới của lộ trình

Các chủ đề như RTSP/RTP, codec H.264/H.265, muxing, hardware codec, DMA-BUF zero-copy, `GstVideoMeta`, multi-camera sync và dynamic pipeline modification là quan trọng cho nhiều vị trí việc làm, nhưng **không phải kiến thức đã được lab 01–09 kiểm chứng đầy đủ**. Nên học và kiểm tra chúng trong các lab nâng cao riêng theo phần cứng và loại sản phẩm. Đừng đưa kết luận về độ trễ, zero-copy hay ổn định sản phẩm chỉ từ các ví dụ tổng hợp.
