# Lab 08 — GstBus, GstMessage and Error Handling

Theory: [Lesson 08](../../knowledge/08-gstreamer-gstbus-gstmessage-runtime-error-handling-final.md) · [Full theory-to-lab map](../../knowledge/README.md). This lab verifies only the outcomes stated in its examples and completion criteria.

## Objective

Receive and act on runtime information from GStreamer. Bus messages travel
to the application; media buffers travel through pads in the pipeline.

```text
elements → GstMessage → GstBus → application
                              ├── synchronous polling
                              └── GMainLoop bus watch
```

EOS means stream completion, not automatic destruction of the pipeline.
ERROR is a failure that needs diagnostics and cleanup. Many elements can post
STATE_CHANGED messages during one pipeline transition.

## Examples

| Executable / source | Main APIs and execution | Observable result and PASS condition |
|---|---|---|
| `01_bus_polling` — `src/01_bus_polling.cpp` | `gst_element_get_bus`, `gst_bus_timed_pop`, message type inspection and unref. | Prints real messages. PASS requires multiple messages and EOS before a monotonic-clock deadline. |
| `02_eos_handling` — `src/02_eos_handling.cpp` | Filters EOS/ERROR and checks the message source. | PASS requires pipeline EOS, observes the pipeline state before explicit NULL cleanup, then releases the bus and pipeline. |
| `03_error_warning` — `src/03_error_warning.cpp` | Real `filesrc` missing-file ERROR via `gst_message_parse_error`; separate `gst_message_parse_warning` exercise. | PASS requires a real resource ERROR and successful parsing of an explicitly **locally constructed** WARNING. No element WARNING is claimed. |
| `04_state_messages` — `src/04_state_messages.cpp` | `gst_message_parse_state_changed`, `GST_MESSAGE_SRC`. | Prints pipeline NULL→READY→PAUSED→PLAYING transitions; PASS requires at least three pipeline changes, child changes and EOS. |
| `05_async_bus_watch` — `src/05_async_bus_watch.cpp` | `gst_bus_add_watch`, `GMainLoop`, GLib timeout and explicit source removal. | PASS requires callback messages, pipeline state changes and EOS, with neither ERROR nor timeout. |

Examples 01–04 poll synchronously; Example 05 integrates the bus with a main
loop. A sixth example would repeat these concepts without adding a distinct
learning objective.

## Code organization

The message-consuming helpers (`poll_bus()`, `wait_for_pipeline_eos()`, `collect_messages()` and `run_watch()`) own the wait or dispatch logic for their examples. `main()` handles graph setup and cleanup. Every popped `GstMessage` is unreffed in the helper that received it; the bus-watch callback uses a borrowed message.

## Prerequisites, build and run

Required elements: `videotestsrc`, `filesrc`, and `fakesink`. The Meson file
declares GStreamer and GLib dependencies. Use the project C++17 environment:

```bash
cd ~/TungNHS/Gstreamer-Lesson
source .venv/bin/activate
source env.sh
pkg-config --modversion gstreamer-1.0
pkg-config --variable=prefix gstreamer-1.0
cd practice/lesson-08
meson setup build
meson compile -C build
./build/01_bus_polling
./build/02_eos_handling
./build/03_error_warning
./build/04_state_messages
./build/05_async_bus_watch
```

## Ownership, callback lifetime and debugging

`gst_element_get_bus()` returns an owned reference. A message returned by
`gst_bus_timed_pop()` is also owned and must be unreffed. In a bus-watch
callback the message is borrowed for the callback; the watch releases it
afterward. `gst_message_parse_error()` and `gst_message_parse_warning()`
allocate `GError` and optional debug strings for the caller to free.

The watch and timeout in Example 05 are removed before its stack callback
context and `GMainLoop` are destroyed. Every example returns the pipeline to
NULL. A timeout, EOS and ERROR are distinct outcomes; the expected missing
file in Example 03 is deliberately verified as a real resource error.

For an unexpected ERROR, read both its public message and debug string.
Do not treat the locally constructed WARNING example as evidence that an
element emitted a runtime warning.

## Completion criteria and status

Explain polling vs a watch, EOS vs ERROR, pipeline vs child STATE_CHANGED,
message ownership and watch cleanup. All five programs must satisfy their
real-message or clearly labeled parser assertions.

```text
LAB 08: PASS
```

All five examples built and ran with custom GStreamer 1.28.6 on 2026-10-24.
