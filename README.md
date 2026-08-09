# GStreamer Learning Labs

A practical GStreamer learning repository focused on Embedded Linux camera and media-pipeline development.

The repository complements the GStreamer theory lessons with incremental hands-on labs written primarily in C++17.

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

## Repository Structure

```text
.
├── README.md
├── env.sh.example
└── practice/
    ├── lesson-01/
    │   └── README.md
    └── lesson-02/
        ├── README.md
        ├── meson.build
        └── src/
```

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
source env.sh
```

Verify the active GStreamer installation:

```bash
gst-launch-1.0 --version
gst-inspect-1.0 --version
pkg-config --modversion gstreamer-1.0
```

The expected GStreamer version for the current labs is:

```text
1.28.6
```

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

## Scope

This repository focuses on application-side GStreamer programming for Embedded Linux camera systems.

Topics such as production optimization, zero-copy pipelines, DMA-BUF integration, hardware-specific acceleration, long-duration reliability, and platform-specific performance are not assumed unless they are explicitly implemented and validated.

## Status

```text
Lab 01 : COMPLETE
Lab 02 : COMPLETE
Lab 03 : PLANNED
Lab 04 : PLANNED
Lab 05 : PLANNED
Lab 06 : PLANNED
Lab 07 : PLANNED
Lab 08 : PLANNED
```
