# Lab 05 — GstCaps and Caps Negotiation

Theory: [Lesson 05](../../knowledge/05-gstreamer-gstcaps-caps-negotiation-format-contract-final.md) · [Full theory-to-lab map](../../knowledge/README.md). This lab verifies only the outcomes stated in its examples and completion criteria.

## Objective

Learn how a pipeline moves from possible media formats to one actual format.
Lab 04 used selected formats; this lab explains what a caps constraint means,
what a pad can offer, and what was negotiated at runtime.

```text
Possible caps → intersection → fixed choice → runtime negotiation → current caps
```

Fixating a `GstCaps` value in application code does **not** run a pipeline or
prove that a peer accepted the choice. A successful element link likewise does
not prove that streaming negotiation will succeed.

## Examples

| Executable / source | Main APIs and execution | Observable result and PASS condition |
|---|---|---|
| `01_caps_fundamentals` — `src/01_caps_fundamentals.cpp` | `gst_caps_from_string`, `GstStructure`, `gst_caps_is_fixed`, `gst_caps_new_any`, `gst_caps_new_empty` | Prints fixed and possible raw-video caps. PASS requires the media type, format, width, fixed/unfixed state, ANY and EMPTY checks to match. |
| `02_caps_intersection` — `src/02_caps_intersection.cpp` | `gst_caps_intersect`, `gst_caps_fixate`, `gst_caps_is_subset` | Prints the overlapping video formats and one fixed choice. PASS requires a nonempty unfixed intersection, a fixed subset and an empty video/audio intersection. |
| `03_pad_caps_views` — `src/03_pad_caps_views.cpp` | `gst_pad_get_pad_template_caps`, `gst_pad_query_caps`, `gst_pad_get_current_caps` | Before PLAYING, current caps are absent while template and query caps permit I420. After finite streaming, current caps are fixed I420 and EOS arrives. |
| `04_runtime_negotiation` — `src/04_runtime_negotiation.cpp` | `capsfilter`, `fakesink::handoff`, current caps, `GstBus` | PASS requires three buffers, EOS, and actual NV12 caps at 160×120, 30/1. The callback inspects borrowed buffer and pad pointers. |
| `05_negotiation_failure` — `src/05_negotiation_failure.cpp` | `gst_element_link`, `capsfilter`, `gst_message_parse_error` | An audio source cannot link to `videoconvert`. A separately linked video graph fails only after an impossible audio caps constraint is set; PASS requires a real error whose debug detail contains `not-negotiated`. |

The fifth example sets the impossible `capsfilter` value **after** building the
linked graph. If the value were supplied during parsing, GStreamer could reject
the link early and it would no longer demonstrate a runtime negotiation failure.

## Prerequisites

Use the project GStreamer 1.28.6 installation and C++17 toolchain. Required
elements: `videotestsrc`, `audiotestsrc`, `videoconvert`, `capsfilter`, and
`fakesink`. Inspect a missing element with `gst-inspect-1.0 ELEMENT_NAME`.

## Build and run

```bash
cd ~/TungNHS/Gstreamer-Lesson
source .venv/bin/activate
source env.sh
pkg-config --modversion gstreamer-1.0
pkg-config --variable=prefix gstreamer-1.0
cd practice/lesson-05
meson setup build
meson compile -C build
./build/01_caps_fundamentals
./build/02_caps_intersection
./build/03_pad_caps_views
./build/04_runtime_negotiation
./build/05_negotiation_failure
```

`pkg-config` should report `1.28.6` and `$HOME/opt/gstreamer`. The finite
pipelines use bounded waits and distinguish EOS from ERROR. Example 05 is
successful only when its **expected** runtime error is actually observed.

## Ownership and debugging

`GstCaps` returned by constructors, intersection, pad APIs and copies must be
unreffed. `GstStructure*` obtained from caps is borrowed. `gst_caps_fixate()`
consumes its input, so Example 02 passes it a copy. Bus messages received by
polling carry a reference that must be unreffed.

If current caps are absent before startup, that is expected: no CAPS event has
established a negotiated format. If Example 04 fails, inspect its negotiated
caps and the bus error; an installed plugin or supported format may differ.
`not-negotiated` in Example 05 is a controlled failure, not a passing media
pipeline. Detailed camera mode selection belongs to Lab 06.

## Completion criteria and status

Explain ANY vs EMPTY, fixed vs unfixed, intersection vs fixation, and template
vs queried vs current caps. All five programs must satisfy their assertions.

```text
LAB 05: PASS
```

All five examples built and ran with custom GStreamer 1.28.6 on 2026-10-24.
