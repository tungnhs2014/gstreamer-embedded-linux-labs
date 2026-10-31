# Lab 06 — V4L2 Devices and v4l2src

Theory: [Lesson 06](../../knowledge/06-gstreamer-v4l2-v4l2src-camera-capture-workflow-final.md) · [Full theory-to-lab map](../../knowledge/README.md). This lab verifies only the outcomes stated in its examples and completion criteria.

## Objective

Connect a Linux camera device to a GStreamer pipeline and distinguish device
capabilities from a requested and actually captured mode.

```text
Camera → V4L2 driver (often uvcvideo for USB cameras) → /dev/videoX
       → v4l2src → GstBuffer → downstream element
```

`/dev/videoX` is a device node, not proof of a usable capture stream. A V4L2
driver may expose multiple nodes, formats, sizes and frame intervals. V4L2
fourcc `YUYV` corresponds to GStreamer raw-video format `YUY2` for the byte
layout studied in Lab 04. MJPEG is compressed `image/jpeg` and needs `jpegdec`
before raw pixel processing.

## Examples

| Executable / source | Main APIs and execution | Observable result and PASS condition |
|---|---|---|
| `01_device_discovery` — `src/01_device_discovery.cpp` | `GstDeviceMonitor` scans `Video/Source` providers and prints discovered devices and advertised caps count. | PASS means the monitor scan completed. An empty result means no matching device was reported by those providers; it does not prove that no V4L2 node exists or that capture works. |
| `02_v4l2_capabilities` — `src/02_v4l2_capabilities.cpp` | `open`, `VIDIOC_QUERYCAP`, `VIDIOC_ENUM_FMT`, `VIDIOC_ENUM_FRAMESIZES`, `VIDIOC_ENUM_FRAMEINTERVALS` | With a real capture node, prints its driver, fourcc formats, discrete or ranged sizes and intervals. PASS requires at least one format, at least one size per format, the sampled intervals for each printed size, and no truncated listing. A driver that does not enumerate required details yields an incomplete-inspection error, not PASS. |
| `03_raw_yuy2_capture` — `src/03_raw_yuy2_capture.cpp` | `v4l2src`, capsfilter, sink handoff and bus | With a supported camera mode, PASS requires actual YUY2 caps within requested caps, three nonempty buffers and EOS. |
| `04_mjpeg_capture` — `src/04_mjpeg_capture.cpp` | `v4l2src`, `jpegdec`, capsfilter, handoff | PASS requires actual `image/jpeg` at the decoder input, `video/x-raw` at its output, three nonempty decoded buffers and EOS. |
| `05_camera_error` — `src/05_camera_error.cpp` | `v4l2src` error message and `gst_message_parse_error` | By default uses a verified nonexistent path and requires a camera-source `RESOURCE/NOT_FOUND` error. With a device and caps, it first requires READY and checks that the requested caps do not intersect the camera's advertised caps; PASS then requires a matching negotiation or settings error. A device-open failure never counts as an unsupported-mode PASS. |

Examples 02–04 require suitable capture hardware. Examples 01 and the default
05 are hardware-independent checks; a negative open test of 02 can run without
a camera, but does not verify capability enumeration.

The C270 and synthetic-driver measurements described in the theory lesson come from the separate CamStream Control project. They are not hardware results from this lab repository.

## Prerequisites and device selection

Use Linux V4L2 headers, the project C++17/Meson environment, and the custom
GStreamer 1.28.6 plugins `v4l2src`, `capsfilter`, `jpegdec`, and `fakesink`.
`v4l2-ctl` is useful for independent device inspection when installed.

On a physical board, first identify a real capture node and modes:

```bash
ls -l /dev/video*
v4l2-ctl --list-devices
v4l2-ctl --device=/dev/videoX --list-formats-ext
gst-inspect-1.0 v4l2src
```

Select the node reported for video capture, not a metadata-only or output
node. Check permissions if `open()` reports access denied. The program uses
the supplied device string through the `v4l2src` `device` property; it does
not assume `/dev/video0`.

## Build and run

```bash
cd ~/TungNHS/Gstreamer-Lesson
source .venv/bin/activate
source env.sh
pkg-config --modversion gstreamer-1.0
pkg-config --variable=prefix gstreamer-1.0
cd practice/lesson-06
meson setup build
meson compile -C build
./build/01_device_discovery
./build/05_camera_error
```

On a machine with a camera, replace `/dev/videoX` with the selected node:

```bash
./build/02_v4l2_capabilities /dev/videoX
./build/03_raw_yuy2_capture /dev/videoX
./build/03_raw_yuy2_capture /dev/videoX 'video/x-raw,format=YUY2,width=640,height=480,framerate=30/1'
./build/04_mjpeg_capture /dev/videoX
./build/04_mjpeg_capture /dev/videoX 'image/jpeg,width=640,height=480,framerate=30/1'
./build/05_camera_error /dev/videoX 'video/x-raw,format=YUY2,width=9999,height=9999'
```

Choose values advertised by Example 02 for successful capture. The last
command is a **negative test**: it first checks that the camera reaches READY
and that its advertised caps exclude the requested mode, then requires a
matching runtime error. If advertised caps are too broad to prove that the
mode is unsupported, the test exits 2 without claiming PASS. A missing or
inaccessible device does not count as mode rejection. The examples do not
substitute `videotestsrc` for a camera.

## Ownership, limits and debugging

The V4L2 file descriptor in Example 02 is closed on success and failure.
Its printed listing is bounded to 64 formats, 32 sizes per format and 16
intervals per size; for a ranged size it queries intervals at the minimum size.
It probes one extra index to detect truncation. If size or interval enumeration
fails at index zero, inspection is incomplete and the program exits nonzero;
an `EINVAL` after one or more entries is normal end-of-enumeration.
`GstDeviceMonitor` devices and returned caps are referenced objects; the list
and each item are released. Camera handoff buffers and pads are borrowed; the
callback context, requested caps and decoder remain alive through shutdown.
`gst_pad_get_current_caps()` returns a reference that is unreffed after use.

An unsupported mode may produce `not-negotiated` or a V4L2 resource error.
Inspect the actual bus diagnostic and compare the requested caps with
`v4l2-ctl --list-formats-ext`. A missing device, access denial and a device
without capture capability are different failures. These examples cover basic
capture lifecycle, not V4L2 buffer queue management, DMA-BUF or CamStream's
Camera HAL. They supply the conceptual chain **device → capabilities →
capture format → GStreamer source** used by that architecture.

## Completion criteria and status

Explain discovery vs capture, driver fourcc vs GStreamer format, raw YUY2 vs
MJPEG decode, requested vs negotiated caps, and real error diagnostics.

```text
LAB 06: SOFTWARE VERIFIED — HARDWARE TESTS PENDING
HARDWARE TEST: NOT RUN — NO CAPTURE DEVICE
```

On 2026-10-24 all five executables built. The monitor reported no matching
`Video/Source` device; the default missing-device case produced a real V4L2
resource error.
Capability enumeration and real YUY2/MJPEG capture were not run without
hardware. Full lab PASS requires those hardware tests on a suitable camera.
