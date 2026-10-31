# Lab 07 — Pipeline States and Live Sources

Theory: [Lesson 07](../../knowledge/07-gstreamer-state-machine-live-source-lifecycle-final.md) · [Full theory-to-lab map](../../knowledge/README.md). This lab verifies only the outcomes stated in its examples and completion criteria.

## Objective

Observe the lifecycle of a GStreamer pipeline and distinguish a requested
state from a reached state. A live source cannot normally preroll in PAUSED.

```text
NULL ↔ READY ↔ PAUSED ↔ PLAYING
                  │
          non-live: preroll buffer
          live: NO_PREROLL (not an error)
```

`gst_element_set_state()` may report `ASYNC`; `gst_element_get_state()` with
a bounded timeout waits for completion. `FAILURE` is the failure result.
These examples use a synthetic live source to teach state behavior without
claiming camera validation.

## Examples

| Executable / source | Main APIs and execution | Observable result and PASS condition |
|---|---|---|
| `01_basic_states` — `src/01_basic_states.cpp` | `gst_element_set_state`, `gst_element_get_state` request READY, PAUSED, PLAYING and NULL in order. | Prints request result and reached state at each step. PASS requires all four target states, allowing an intermediate ASYNC result. |
| `02_async_transition` — `src/02_async_transition.cpp` | Requests PAUSED on a non-live pipeline, then waits up to five seconds. | Prints immediate and completed results. PASS requires PAUSED reached; the immediate result need not always be ASYNC on every system. |
| `03_nonlive_preroll` — `src/03_nonlive_preroll.cpp` | `fakesink::preroll-handoff` observes a borrowed buffer while PAUSED. | PASS requires successful PAUSED transition and at least one nonempty preroll buffer before PLAYING. |
| `04_live_no_preroll` — `src/04_live_no_preroll.cpp` | `videotestsrc is-live=true`, state return values. | PASS requires `NO_PREROLL` in PAUSED, then PLAYING and clean NULL shutdown. This is a synthetic live source. |
| `05_restart_pipeline` — `src/05_restart_pipeline.cpp` | Runs the same finite graph to EOS, reaches NULL, then runs it again. | PASS requires two EOS cycles and two buffers per cycle, four total, with no stale callback or object reference. |

Example 05 demonstrates a valid restart for this synthetic graph. The CamStream Control case study in the theory lesson uses a stricter stop-and-rebuild policy for its camera service; that is an application policy, not a universal GStreamer requirement.

## Code organization

Each example keeps `main()` focused on setup, the scenario call and cleanup. Named helpers such as `observe_paused_transition()`, `observe_preroll()`, `expect_no_preroll()` and `run_cycle()` hold the state checks; their return values are the lab assertions. Callback data stays alive until the pipeline reaches NULL.

## Prerequisites, build and run

Required elements: `videotestsrc` and `fakesink`. Use the custom GStreamer
1.28.6 installation and C++17 toolchain.

```bash
cd ~/TungNHS/Gstreamer-Lesson
source .venv/bin/activate
source env.sh
pkg-config --modversion gstreamer-1.0
pkg-config --variable=prefix gstreamer-1.0
cd practice/lesson-07
meson setup build
meson compile -C build
./build/01_basic_states
./build/02_async_transition
./build/03_nonlive_preroll
./build/04_live_no_preroll
./build/05_restart_pipeline
```

## Ownership and debugging

`gst_parse_launch()` returns a pipeline reference owned by the application.
Each pipeline is returned to NULL before unref. The sink returned by
`gst_bin_get_by_name()` and each bus returned by `gst_element_get_bus()` are
owned references and are unreffed. Handoff callback buffer/pad pointers are
borrowed; stack callback state remains alive until streaming stops.

A timeout in PAUSED can mean preroll could not complete. For a live source,
`NO_PREROLL` is expected rather than a timeout or error. Example 05 waits for
EOS/ERROR with a bound; it does not treat a zero process exit from a hanging
pipeline as proof of success. Detailed message dispatch belongs to Lab 08.

## Completion criteria and status

Explain all four states, request vs completion, preroll, `ASYNC`, `NO_PREROLL`,
and why restart requires a finished NULL transition. All five examples must
meet their explicit state and buffer checks.

```text
LAB 07: PASS
```

All five examples built and ran with custom GStreamer 1.28.6 on 2026-10-24.
