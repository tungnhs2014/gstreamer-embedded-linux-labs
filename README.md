# GStreamer Learning Labs

A practical GStreamer learning repository focused on Embedded Linux camera and media-pipeline development.

The repository complements the GStreamer theory lessons with incremental hands-on labs written primarily in C++17.

Start with the [theory-to-lab map](knowledge/README.md). The [knowledge map](knowledge/gstreamer-knowledge-map-and-workflows-fixed.md) shows how the concepts connect; each lab states what its examples actually verify.

## Purpose

The goal of this repository is to build a practical understanding of GStreamer from the fundamentals required by Embedded Linux camera applications.

The learning path progresses from simple command-line pipelines to the GStreamer object model, buffer ownership, media formats, caps negotiation, V4L2 camera capture, pipeline state management, and runtime error handling.

The examples are intentionally incremental. Each example introduces a small number of new concepts instead of hiding the GStreamer object model behind large applications.

## Learning Path

| Lab    | Topic                                                                |
| ------ | -------------------------------------------------------------------- |
| Lab 01 | GStreamer overview, media graph, `gst-launch-1.0`, `gst-inspect-1.0` |
| Lab 02 | Plugin, Registry, Factory, Element, Pad, Bin and Pipeline            |
| Lab 03 | `GstBuffer`, `GstMemory`, ownership and data lifecycle               |
| Lab 04 | Raw video, YUY2/YUYV and MJPEG                                       |
| Lab 05 | `GstCaps` and caps negotiation                                       |
| Lab 06 | V4L2 and `v4l2src` camera capture                                    |
| Lab 07 | Pipeline state machine and live-source lifecycle                     |
| Lab 08 | `GstBus`, `GstMessage` and runtime error handling                    |
| Lab 09 | Clock/timestamp, `tee`/`queue`, and `appsrc`/`appsink` basics        |

After Lab 08, read the [runtime integration guide](knowledge/09-gstreamer-runtime-integration-and-interview-guide.md) and run [Lab 09](practice/lesson-09/README.md). The examples verify synthetic behavior; camera performance and full backpressure policy remain future work.

## Repository Structure

```text
.
├── README.md
├── env.sh.example
├── knowledge/
│   ├── README.md
│   ├── 01–08 theory lessons
│   ├── 09 runtime integration and interview guide
│   └── gstreamer-knowledge-map-and-workflows-fixed.md
└── practice/
    ├── lesson-01/
    │   └── README.md
    ├── lesson-02/
    ├── lesson-03/
    ├── lesson-04/
    ├── lesson-05/
    ├── lesson-06/
    ├── lesson-07/
    ├── lesson-08/
    └── lesson-09/
```

Each C++ lab (02–09) contains `README.md`, `meson.build`, and `src/`.

Generated build directories, local Python virtual environments, GStreamer upstream source trees and machine-specific environment files are intentionally excluded from version control.

## Development Environment

Reference environment:

```text
Ubuntu       : 22.04 LTS
C++          : C++17
Build system : Meson + Ninja
GStreamer    : 1.28.6
```

GStreamer is installed outside this repository under a custom prefix.

Example:

```text
$HOME/opt/gstreamer
```

The upstream GStreamer source tree is also kept outside version control.

## Environment Setup

Create the local environment configuration:

```bash
cp env.sh.example env.sh
source .venv/bin/activate
source env.sh
```

Verify the active GStreamer installation:

```bash
gst-launch-1.0 --version
gst-inspect-1.0 --version
pkg-config --modversion gstreamer-1.0
pkg-config --variable=prefix gstreamer-1.0
```

The expected GStreamer version for the current labs is:

```text
1.28.6
```

The prefix should be `$HOME/opt/gstreamer` before configuring a Meson build.

## Building a C++ Lab

For labs containing C++ examples:

```bash
cd practice/lesson-02

meson setup build
meson compile -C build
```

Run an example:

```bash
./build/01_registry_factory
```

The exact commands and expected behavior are documented in each lesson directory.

## Coding Principles

Examples follow these rules:

* C++17;
* clean and readable code;
* `snake_case` naming;
* explicit error checking;
* clear GStreamer ownership and lifetime handling;
* Doxygen for functions where the contract or ownership requires explanation;
* comments explain intent, ownership, or non-obvious behavior;
* no unused objects or variables;
* each example introduces only concepts relevant to its learning objective.

Use the repository [`.clang-format`](.clang-format) for consistent C++ formatting.

## Scope

This repository focuses on application-side GStreamer programming for Embedded Linux camera systems.

Topics such as production optimization, zero-copy pipelines, DMA-BUF integration, hardware-specific acceleration, long-duration reliability, and platform-specific performance are not assumed unless they are explicitly implemented and validated.

## Status

```text
Lab 01 : COMPLETE
Lab 02 : COMPLETE
Lab 03 : COMPLETE
Lab 04 : COMPLETE
Lab 05 : COMPLETE
Lab 06 : SOFTWARE VERIFIED — HARDWARE TESTS PENDING
Lab 07 : COMPLETE
Lab 08 : COMPLETE
Lab 09 : SYNTHETIC EXAMPLES VERIFIED
```

The 20 C++ examples in Labs 02–04 were built and run with the custom GStreamer
1.28.6 installation on 2026-10-24. Lab 01's status records its earlier
command-line validation; its commands were not rerun as part of that C++ check.

Labs 05, 07 and 08 were built and run with custom GStreamer 1.28.6 on
2026-10-24. Lab 06 discovery and controlled missing-device handling were
verified; capability enumeration and real YUY2/MJPEG capture await a suitable
V4L2 camera. The original Lessons 05–08 were not available when those labs
were first implemented. The current `knowledge/` files document all eight
original topics plus runtime integration; the [theory-to-lab map](knowledge/README.md)
identifies coverage and gaps. On 2026-10-31, all 41 C++ examples runnable without
camera hardware and the Lab 01 CLI pipeline passed again. The three Lab 06
capture examples still need a suitable camera.
