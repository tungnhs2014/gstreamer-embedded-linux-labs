# Lab 09 — Clock, Branching and Application Integration

Theory: [Lesson 09](../../knowledge/09-gstreamer-runtime-integration-and-interview-guide.md) · [Full theory-to-lab map](../../knowledge/README.md).

## Objective and evidence

Observe four runtime contracts relevant to Embedded Linux media applications. These examples use synthetic data and make no claim about real camera FPS, latency, zero-copy or production reliability.

| Program | What it does | PASS proves | Does not prove |
| --- | --- | --- | --- |
| `01_clock_timestamps` | Finite live `videotestsrc`; handoff checks three valid, ordered PTS values and reads pipeline clock/base time | Timestamp and clock information is available in this execution | Sink synchronization or camera FPS |
| `02_tee_queues` | One finite source through `tee` and a `queue` on each branch | Both sinks receive all three buffers and the graph reaches EOS | Throughput or a specific thread schedule |
| `03_appsrc_appsink` | C++ pushes three 2×2 GRAY8 frames with PTS/duration; C++ pulls owned samples | All four payload bytes, fixed caps, PTS and duration match at the app boundary; sample ownership is released correctly | Backpressure or sustained high-rate transfer |
| `04_queue_drop` | A fast branch and a deliberately slow branch with `queue leaky=downstream` | Fast branch gets 30 buffers; slow branch gets fewer and both reach EOS | A latency target or a correct drop policy for a real product |

## Code organization

Example 01 separates timestamp collection from clock observation. Examples 02 and 04 share [`src/branch_observation.hpp`](src/branch_observation.hpp) for handoff counts, Bus wait and callback shutdown. Example 03 separates buffer production, sample validation and the transfer sequence. Each `main()` shows the complete setup and cleanup path.

## Build and run

```bash
cd ~/TungNHS/Gstreamer-Lesson
source .venv/bin/activate
source env.sh
pkg-config --modversion gstreamer-1.0 gstreamer-app-1.0
cd practice/lesson-09
meson setup build
meson compile -C build
./build/01_clock_timestamps
./build/02_tee_queues
./build/03_appsrc_appsink
./build/04_queue_drop
```

On a configured GStreamer 1.28.6 installation, all four should print `PASS` and exit 0. For Example 03, `gst_app_src_push_buffer()` takes ownership of the supplied buffer reference; `gst_app_sink_try_pull_sample()` returns an owned sample that the application unrefs. This differs from the `appsrc::push-buffer` **action signal**, which takes an additional reference without consuming the caller's reference. Verify the contract of the exact API used.

## Practice questions and next steps

1. Why does `sync=false` on a `fakesink` not make PTS meaningless?
2. Why can one slow branch still affect an upstream `tee`, even if each branch has a `queue`?
3. What limits would you put on an `appsink` queue if its consumer is slower than the producer?
4. Where would you measure actual delivered FPS and end-to-end latency on a real camera?
5. Why must `STREAM_START`, `CAPS` and `SEGMENT` establish stream context before buffers? What changes after a flushing seek?
6. How would you distinguish a frame dropped by a leaky `queue` from one dropped due to QoS?

Questions 5 and 6 extend the theory; these four programs do not exercise seek or QoS. Example 04 deliberately makes one branch slower and observes dropping. For real workloads, measure bounded queues and the chosen drop policy on the target board. See [Lesson 09](../../knowledge/09-gstreamer-runtime-integration-and-interview-guide.md) for the policy questions and official references.

```text
LAB 09: SYNTHETIC EXAMPLES VERIFIED
HARDWARE PERFORMANCE: NOT TESTED
```
