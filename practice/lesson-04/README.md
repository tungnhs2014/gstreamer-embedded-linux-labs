# Lab 04 — Raw Video, YUY2 and MJPEG

Theory: [Lesson 04](../../knowledge/04-gstreamer-raw-video-yuyv-yuy2-mjpeg-formats-final.md) · [Full theory-to-lab map](../../knowledge/README.md). This lab verifies only the outcomes stated in its examples and completion criteria.

## 1. Objective

This lab introduces the media-format concepts required to correctly interpret video payloads in GStreamer.

The lab focuses on:

```text
Raw video
├── YUY2 / YUYV
├── I420
└── NV12

Compressed video
└── MJPEG / image/jpeg
```

The main objectives are to understand:

* raw vs compressed video;
* packed, planar and semi-planar layouts;
* YUY2 byte ordering;
* chroma subsampling;
* frame size and stride;
* GStreamer raw-video caps;
* raw format conversion;
* JPEG encoding;
* JPEG decoding;
* the difference between decoding and raw format conversion.

Detailed caps negotiation mechanics are intentionally deferred to Lab 05.

---

## 2. Examples

### Example 01 — Raw Video Layout

Source:

```text
src/01_video_format_layout.cpp
```

Topics:

* `GstVideoInfo`
* `GstVideoFormat`
* YUY2
* I420
* NV12
* plane count
* plane offset
* stride
* frame storage size

The example compares the default GStreamer layout for:

```text
YUY2
I420
NV12
```

Mental model:

```text
width + height
    is not enough

width + height + format + layout/stride
    describes the pixel storage
```

Important distinction:

```text
video plane count
    ≠
number of GstMemory objects
```

Pixel planes describe the video format.

`GstMemory` describes how payload memory is represented by GStreamer.

---

### Example 02 — YUY2 Byte Layout

Source:

```text
src/02_yuy2_byte_layout.cpp
```

Topics:

* YUY2/YUYV
* packed 4:2:2
* luma
* chroma
* byte interpretation
* shared chroma samples

Four bytes represent two horizontal pixels:

```text
Y0 Cb0 Y1 Cr0
```

Mental model:

```text
Pixel 0:
    Y0 + shared Cb0/Cr0

Pixel 1:
    Y1 + shared Cb0/Cr0
```

Therefore:

```text
2 pixels
    ↓
4 bytes
    ↓
2 bytes per pixel on average
```

The two pixels retain independent luma values but share chroma information.

---

### Example 03 — Raw YUY2 Pipeline

Source:

```text
src/03_raw_yuy2_pipeline.cpp
```

Graph:

```text
videotestsrc
      ↓
capsfilter
video/x-raw
format=YUY2
320x240
30/1
      ↓
fakesink
```

Topics:

* `video/x-raw`
* YUY2 format selection
* width
* height
* framerate
* raw buffer size
* negotiated current caps
* finite test pipeline

The example demonstrates that a `GstBuffer` carrying raw video must be interpreted together with its media-format contract.

Important:

```text
GstBuffer bytes
      +
video format/layout
      =
meaningful pixels
```

A buffer pointer alone is not enough to interpret video data.

---

### Example 04 — Raw Format Conversion

Source:

```text
src/04_raw_format_conversion.cpp
```

Graph:

```text
YUY2
video/x-raw
      ↓
videoconvert
      ↓
I420
video/x-raw
```

Topics:

* `videoconvert`
* raw-to-raw conversion
* input raw format
* output raw format
* current negotiated caps

The important point is:

```text
videoconvert
    does not decode compressed video

videoconvert
    converts one raw representation
    into another raw representation
```

Example:

```text
YUY2 → I420
```

Both sides remain:

```text
video/x-raw
```

---

### Example 05 — MJPEG Compressed Payload

Source:

```text
src/05_mjpeg_payload.cpp
```

Graph:

```text
videotestsrc
      ↓
video/x-raw,YUY2
      ↓
jpegenc
      ↓
image/jpeg
      ↓
fakesink
```

Topics:

* `jpegenc`
* raw → compressed
* `image/jpeg`
* compressed frame payload
* variable encoded size

Mental model:

```text
raw pixels
    ↓
JPEG compression
    ↓
compressed JPEG payload
```

Unlike tightly packed raw video, the encoded JPEG payload size is not calculated using a simple:

```text
width × height × bytes_per_pixel
```

formula.

Different frames may produce different compressed sizes.

---

### Example 06 — MJPEG Decode and Raw Conversion

Source:

```text
src/06_mjpeg_decode.cpp
```

Graph:

```text
videotestsrc
      ↓
video/x-raw,YUY2
      ↓
jpegenc
      ↓
image/jpeg
      ↓
jpegdec
      ↓
video/x-raw
      ↓
videoconvert
      ↓
video/x-raw,NV12
      ↓
fakesink
```

Topics:

* JPEG encode
* JPEG decode
* compressed → raw
* raw → raw conversion
* decoder output format
* converter output format

Core distinction:

```text
jpegdec
    image/jpeg
        ↓
    video/x-raw
```

while:

```text
videoconvert
    video/x-raw format A
        ↓
    video/x-raw format B
```

Therefore:

```text
jpegdec
    = decoder

videoconvert
    = raw format converter
```

---

## 3. Build

Activate the project environment:

```bash
cd ~/TungNHS/Gstreamer-Lesson

source .venv/bin/activate
source env.sh
```

Verify that the intended GStreamer installation is active:

```bash
pkg-config --modversion gstreamer-1.0
pkg-config --modversion gstreamer-video-1.0
pkg-config --variable=prefix gstreamer-1.0
```

Expected project environment:

```text
GStreamer : 1.28.6
Prefix    : $HOME/opt/gstreamer
```

Enter the lab:

```bash
cd practice/lesson-04
```

First build:

```bash
meson setup build
meson compile -C build
```

If an existing build directory still resolves dependencies from another
GStreamer installation, keep it intact and use a separate build directory:

```bash
meson setup build-custom
meson compile -C build-custom
```

Run examples from `build-custom/` in that case. Check the configured dependency
paths before deciding which build directory to use.

---

## 4. Required Plugins

Verify the required elements:

```bash
gst-inspect-1.0 videotestsrc
gst-inspect-1.0 capsfilter
gst-inspect-1.0 videoconvert
gst-inspect-1.0 jpegenc
gst-inspect-1.0 jpegdec
gst-inspect-1.0 fakesink
```

---

## 5. Run

```bash
./build/01_video_format_layout
./build/02_yuy2_byte_layout
./build/03_raw_yuy2_pipeline
./build/04_raw_format_conversion
./build/05_mjpeg_payload
./build/06_mjpeg_decode
```

---

## 6. Raw Video Mental Model

Raw video contains uncompressed pixel samples.

Examples:

```text
YUY2
I420
NV12
RGB
BGR
```

In GStreamer these commonly use:

```text
video/x-raw
```

Important:

```text
video/x-raw
```

means uncompressed video samples in GStreamer.

It does not mean sensor Bayer RAW.

---

## 7. YUY2 / YUYV

The format used by V4L2 as `YUYV` corresponds to the GStreamer raw-video format name `YUY2` for this byte layout:

```text
Y0 Cb0 Y1 Cr0
```

It is:

```text
packed
4:2:2
```

For each two horizontal pixels:

```text
Y0
Y1
```

are independent luma samples.

The pair shares:

```text
Cb0
Cr0
```

This reduces horizontal chroma resolution while preserving luma resolution.

---

## 8. Packed, Planar and Semi-Planar

### YUY2

Packed:

```text
Y0 Cb0 Y1 Cr0 Y2 Cb2 Y3 Cr2 ...
```

All components are interleaved in one pixel stream.

---

### I420

Planar 4:2:0:

```text
Y plane
↓
U plane
↓
V plane
```

Conceptually:

```text
YYYYYYYY
YYYYYYYY

UUUU

VVVV
```

---

### NV12

Semi-planar 4:2:0:

```text
Y plane
↓
interleaved UV plane
```

Conceptually:

```text
YYYYYYYY
YYYYYYYY

UVUVUVUV
```

---

## 9. Frame Size and Stride

For tightly packed YUY2:

```text
logical bytes per row
    = width × 2
```

and:

```text
logical frame size
    = width × height × 2
```

However, real producers may use a larger stride:

```text
stride > width × 2
```

because of:

* alignment;
* hardware requirements;
* DMA layout;
* row padding.

Therefore the actual storage requirement may be:

```text
stride × height
```

instead of:

```text
width × height × 2
```

General rule:

```text
do not hard-code logical frame size
when the producer already provides layout information
```

---

## 10. Raw vs Compressed

### YUY2

```text
video/x-raw
```

Characteristics:

* uncompressed;
* easy to interpret;
* relatively high bandwidth;
* no JPEG decode step.

---

### MJPEG

```text
image/jpeg
```

Characteristics:

* compressed;
* usually lower transport bandwidth;
* variable frame payload size;
* must be decoded before raw pixel processing.

Pipeline:

```text
image/jpeg
    ↓
jpegdec
    ↓
video/x-raw
```

---

## 11. Decoder vs Converter

This distinction must remain clear.

### Decoder

```text
compressed
    ↓
decoder
    ↓
raw
```

Example:

```text
image/jpeg
    ↓ jpegdec
video/x-raw
```

---

### Raw Format Converter

```text
raw format A
    ↓
videoconvert
    ↓
raw format B
```

Example:

```text
YUY2
 ↓
videoconvert
 ↓
NV12
```

No compression is involved.

---

## 12. Important Boundaries

Do not infer the following:

### Raw buffer size

```text
width × height × 2
```

is not a universal physical-storage formula for every YUY2 buffer.

Actual stride must be considered.

---

### MJPEG size

Do not calculate MJPEG payload size using raw-video formulas.

Compressed payload size depends on encoded content and compression behavior.

---

### Framerate

Caps such as:

```text
framerate=30/1
```

describe the stream contract.

They do not by themselves prove that 30 frames per second are actually being delivered at runtime.

Runtime measurement is required for that conclusion.

---

### Zero-copy

A working raw-video pipeline does not prove:

```text
zero-copy
```

Memory movement must be verified separately.

---

## 13. Relation to Camera Capture

Later, a V4L2 camera may expose modes such as:

```text
YUYV
MJPEG
```

A typical raw camera graph may be:

```text
v4l2src
    ↓
video/x-raw,format=YUY2
    ↓
consumer
```

A compressed camera graph may be:

```text
v4l2src
    ↓
image/jpeg
    ↓
jpegdec
    ↓
video/x-raw
    ↓
consumer
```

Lab 06 covers camera discovery, advertised formats and sizes, and capture through
`v4l2src`. Direct V4L2 buffer queue management and fields such as
`bytesperline`, `sizeimage` and `bytesused` are outside these labs' scope.
They remain required concepts in [theory Lesson 04](../../knowledge/04-gstreamer-raw-video-yuyv-yuy2-mjpeg-formats-final.md); none of the Lab 04 examples directly measures these V4L2 fields.

---

## 14. Scope Boundary

This lab focuses on:

```text
raw vs compressed video
YUY2/YUYV
I420
NV12
pixel layout
stride
frame storage
JPEG encoding
JPEG decoding
raw format conversion
```

The following topics belong to later labs:

```text
Caps intersection/fixation/negotiation    → Lab 05
V4L2 device discovery and capture         → Lab 06
Pipeline state-machine details            → Lab 07
GstBus and GstMessage handling            → Lab 08
```

---

## 15. Completion Criteria

Lab 04 is complete when:

```text
[PASS] Example 01 builds and runs
[PASS] Example 02 builds and runs
[PASS] Example 03 builds and runs
[PASS] Example 04 builds and runs
[PASS] Example 05 builds and runs
[PASS] Example 06 builds and runs
[PASS] Build produces no project-source warnings
```

The runtime checks require YUY2 caps and three raw buffers in Example 03,
YUY2-to-I420 negotiated formats in Example 04, five non-empty JPEG buffers
with `image/jpeg` caps in Example 05, and three buffers reaching the NV12 sink
after JPEG decode in Example 06. Finite pipelines must reach EOS rather than
ERROR or a timeout.

After completing the lab, you should be able to explain:

```text
raw vs compressed video

YUY2/YUYV byte layout

packed vs planar vs semi-planar

YUY2 vs I420 vs NV12

logical row size vs stride

raw frame storage vs compressed payload size

video/x-raw vs image/jpeg

jpegdec vs videoconvert

YUY2 transport vs MJPEG transport trade-off
```

## Lab Status

```text
LAB 04: PASS
```

Change the status to `PASS` only after all examples have been successfully built and executed.

All six examples were built and run with the custom GStreamer 1.28.6
installation on 2026-10-24. The caps and buffer checks above passed for that
environment; they do not assert a universal stride or JPEG payload size.
