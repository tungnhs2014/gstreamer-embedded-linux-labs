# Lab 01 — GStreamer Overview and Media Graph

Theory: [Lesson 01](../../knowledge/01-gstreamer-overview-camera-embedded-linux-updated.md) · [Full theory-to-lab map](../../knowledge/README.md). This lab verifies only the outcomes stated in its examples and completion criteria.

## 1. Objective

This lab introduces the basic GStreamer runtime workflow using command-line tools.

After completing this lab, you should be able to:

* verify that the custom GStreamer installation is active;
* inspect GStreamer elements and plugins;
* understand a simple linear media graph;
* identify source, processing, and sink elements;
* distinguish upstream and downstream directions;
* run a pipeline with `gst-launch-1.0`;
* observe negotiated caps;
* enable basic GStreamer debug logging;
* recognize normal pipeline startup, EOS, and shutdown behavior.

This lab uses command-line tools only. C++ application development starts in Lab 02.

---

## 2. Environment

The lab uses the following environment:

```text
Ubuntu          : 22.04.5 LTS
Architecture    : x86_64
GStreamer       : 1.28.6
Source tree     : upstream/gstreamer
Install prefix  : $HOME/opt/gstreamer
```

Before starting the lab:

```bash
cd /home/tungnhs/TungNHS/Gstreamer-Lesson

source .venv/bin/activate
source env.sh
```

Verify the active GStreamer installation:

```bash
which gst-launch-1.0
gst-launch-1.0 --version

which gst-inspect-1.0
gst-inspect-1.0 --version

pkg-config --modversion gstreamer-1.0
```

Expected version:

```text
GStreamer 1.28.6
```

The binaries must come from:

```text
/home/tungnhs/opt/gstreamer/bin
```

---

## 3. Basic Mental Model

A GStreamer application builds a media processing graph from elements.

For this lab:

```text
Upstream                                      Downstream

videotestsrc  ──────>  videoconvert  ──────>  fakesink
    Source             Processing                Sink
```

### Source

`videotestsrc`

Generates video frames and pushes them downstream.

### Processing Element

`videoconvert`

Converts raw video formats when the downstream element requires a different compatible format.

### Sink

`fakesink`

Consumes incoming buffers without displaying or storing them.

It is useful for testing a pipeline without introducing display or file-output dependencies.

---

## 4. Inspect Elements and Plugins

Check the elements used by this lab:

```bash
gst-inspect-1.0 videotestsrc | grep -E 'Filename|Version'
gst-inspect-1.0 videoconvert | grep -E 'Filename|Version'
gst-inspect-1.0 fakesink | grep -E 'Filename|Version'
```

Expected plugin locations:

```text
$HOME/opt/gstreamer/lib/gstreamer-1.0/
```

Expected version:

```text
1.28.6
```

The elements are provided by different plugins:

```text
videotestsrc
    → libgstvideotestsrc.so

videoconvert
    → libgstvideoconvertscale.so

fakesink
    → libgstcoreelements.so
```

This demonstrates an important relationship:

```text
Plugin
    ↓ registers
Element Factory
    ↓ creates
Element
```

Detailed object-model behavior is covered in Lab 02.

---

## 5. Run the First Pipeline

Run:

```bash
gst-launch-1.0 -v \
    videotestsrc num-buffers=30 \
    ! videoconvert \
    ! fakesink
```

Pipeline:

```text
videotestsrc
     │
     ▼
videoconvert
     │
     ▼
fakesink
```

The `!` operator tells `gst-launch-1.0` to link neighboring elements.

`num-buffers=30` makes the source finite. After producing 30 buffers, the pipeline eventually reaches EOS.

---

## 6. Observe Negotiated Caps

The `-v` option prints caps negotiated between connected elements.

Example:

```text
video/x-raw,
width=(int)320,
height=(int)240,
framerate=(fraction)30/1,
format=(string)A444_16LE,
interlace-mode=(string)progressive
```

In this execution, GStreamer negotiated:

```text
Media type  : video/x-raw
Resolution  : 320x240
Frame rate  : 30/1
Format      : A444_16LE
Scan mode   : progressive
```

These are the **negotiated current caps for this pipeline execution**.

They should not be interpreted as a fixed format that `videotestsrc` always produces.

Because no explicit caps were requested, GStreamer selected a format compatible with the complete graph.

In this run, `videoconvert` had the same caps on its sink and source pads, so no format conversion was required.

Caps and negotiation are covered in detail in Lab 05.

---

## 7. Pipeline Runtime

A successful run shows output similar to:

```text
Setting pipeline to PAUSED ...
Pipeline is PREROLLING ...
Pipeline is PREROLLED ...
Setting pipeline to PLAYING ...
Redistribute latency...
New clock: GstSystemClock
Got EOS from element "pipeline0".
Setting pipeline to NULL ...
Freeing pipeline ...
```

High-level flow:

```text
Create pipeline
      ↓
Prepare pipeline
      ↓
Preroll
      ↓
PLAYING
      ↓
Process buffers
      ↓
EOS
      ↓
NULL
      ↓
Cleanup
```

The exact GStreamer state machine is covered in Lab 07.

---

## 8. Upstream and Downstream

For:

```text
videotestsrc → videoconvert → fakesink
```

Relative to `videoconvert`:

```text
videotestsrc
    = upstream

fakesink
    = downstream
```

Media buffers normally flow:

```text
upstream → downstream
```

Do not confuse media-buffer flow with application runtime messages.

Runtime messages such as EOS and ERROR are reported through `GstBus`, which is covered in Lab 08.

---

## 9. Basic Debug Logging

Run:

```bash
GST_DEBUG=2 gst-launch-1.0 \
    videotestsrc num-buffers=5 \
    ! videoconvert \
    ! fakesink
```

`GST_DEBUG=2` enables warning-and-higher GStreamer debug output.

For this simple valid pipeline, no warning or error is expected.

To save the output:

```bash
GST_DEBUG=2 gst-launch-1.0 \
    videotestsrc num-buffers=5 \
    ! videoconvert \
    ! fakesink \
    2>&1 | tee gst-debug.log
```

Check for warnings and errors:

```bash
grep -E 'WARN|ERROR' gst-debug.log
```

No output means no matching warning or error was found.

Higher debug levels are useful later, but enabling excessive logging is unnecessary for this lab.

---

## 10. Expected Result

The lab is successful when:

```text
[PASS] gst-launch-1.0 uses GStreamer 1.28.6
[PASS] gst-inspect-1.0 uses GStreamer 1.28.6
[PASS] videotestsrc is available
[PASS] videoconvert is available
[PASS] fakesink is available
[PASS] the pipeline reaches PLAYING
[PASS] negotiated caps are visible with -v
[PASS] the finite pipeline reaches EOS
[PASS] the pipeline returns to NULL
[PASS] GST_DEBUG=2 reports no blocking error
```

---

## 11. Common Problems

### Wrong GStreamer Version

Symptom:

```text
gst-launch-1.0 version 1.20.3
```

Cause:

The Ubuntu system GStreamer is being used instead of the custom build.

Fix:

```bash
cd /home/tungnhs/TungNHS/Gstreamer-Lesson

source .venv/bin/activate
source env.sh
```

Then verify:

```bash
which gst-launch-1.0
gst-launch-1.0 --version
```

---

### Element Not Found

Example:

```text
WARNING: erroneous pipeline: no element "videotestsrc"
```

Check:

```bash
gst-inspect-1.0 videotestsrc
```

Then verify that the custom plugin path is active:

```bash
echo "$GST_PLUGIN_SYSTEM_PATH_1_0"
```

---

### Pipeline Does Not Reach EOS

For this lab, make sure the source contains:

```text
num-buffers=30
```

Without a finite buffer count, `videotestsrc` can continue generating frames until the application is interrupted.

---

## 12. Exercises

### Exercise 1 — Change the Number of Buffers

Run:

```bash
gst-launch-1.0 \
    videotestsrc num-buffers=100 \
    ! videoconvert \
    ! fakesink
```

Observe that EOS occurs only after the configured number of buffers has been produced.

### Exercise 2 — Remove `videoconvert`

Run:

```bash
gst-launch-1.0 -v \
    videotestsrc num-buffers=30 \
    ! fakesink
```

Compare the negotiated caps with the original pipeline.

Think about why the pipeline can still work without `videoconvert`.

### Exercise 3 — Inspect an Element Fully

Run:

```bash
gst-inspect-1.0 videotestsrc
```

Identify:

* element factory information;
* plugin information;
* pad templates;
* element properties.

Do not attempt to memorize the complete output.

The purpose is to understand what information `gst-inspect-1.0` can provide.

---

## 13. Completion Criteria

Lab 01 is complete when you can explain the following graph without referring to documentation:

```text
videotestsrc → videoconvert → fakesink
```

You should be able to answer:

1. Which element is the source?
2. Which element is the sink?
3. What does upstream mean?
4. What does downstream mean?
5. What does `!` mean in `gst-launch-1.0`?
6. What does `-v` show?
7. What are negotiated caps?
8. Why does this pipeline eventually produce EOS?
9. Why is `fakesink` useful during testing?
10. How do you verify that the custom GStreamer build is being used?

## Lab Status

```text
LAB 01: PASS
```

The next lab moves from the command-line graph to the GStreamer object model:

```text
Plugin
→ Registry
→ Factory
→ Element
→ Pad
→ Link
→ Bin
→ Pipeline
```

and introduces the first C++17 GStreamer applications.
