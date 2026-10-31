# Lab 02 — GStreamer Object Model

Theory: [Lesson 02](../../knowledge/02-gstreamer-object-model-plugin-element-pad-bin-pipeline.md) · [Full theory-to-lab map](../../knowledge/README.md). This lab verifies only the outcomes stated in its examples and completion criteria.

## 1. Objective

This lab introduces the core GStreamer object model through incremental C++17 examples.

The lab focuses on:

```text
Plugin
  ↓
Registry
  ↓
Element Factory
  ↓
Element
  ↓
Pad
  ↓
Link
  ↓
Bin
  ↓
Pipeline
```

It also introduces the three important pad availability models:

* `ALWAYS`
* `REQUEST`
* `SOMETIMES`

and the use of `GstGhostPad` to expose internal pads from a reusable `GstBin`.

The examples are intentionally incremental. Each example introduces only the concepts required for that step.

---

## 2. Examples

### Example 01 — Registry, Factory and Element

Source:

```text
src/01_registry_factory.cpp
```

Topics:

* `GstRegistry`
* `GstPluginFeature`
* `GstElementFactory`
* plugin metadata
* element instance creation
* factory name vs element instance name
* basic reference ownership

Mental model:

```text
Plugin
  ↓ registers
Registry
  ↓ lookup
GstElementFactory
  ↓ create
GstElement
```

---

### Example 02 — Pad and Pad Template

Source:

```text
src/02_pad_template.cpp
```

Topics:

* `GstPad`
* `GstPadTemplate`
* `SRC` and `SINK` directions
* `ALWAYS` pads
* template caps
* actual runtime pad vs pad template

Mental model:

```text
GstPadTemplate
    = pad contract

        ↓

GstPad
    = actual runtime endpoint
```

---

### Example 03 — Bin, Pipeline and Ownership

Source:

```text
src/03_bin_pipeline.cpp
```

Topics:

* `GstBin`
* `GstPipeline`
* parent-child hierarchy
* `gst_bin_add()`
* ownership transfer
* cleanup through the parent container

Type hierarchy:

```text
GstElement
    ↑
GstBin
    ↑
GstPipeline
```

Ownership model:

```text
element creation
      ↓
caller owns element
      ↓
gst_bin_add() succeeds
      ↓
bin owns child
```

---

### Example 04 — Static Linking

Source:

```text
src/04_static_linking.cpp
```

Topics:

* `gst_element_link()`
* actual pad peer relationship
* static `SRC → SINK` linking
* compatible and incompatible element linking
* linking vs runtime negotiation

Successful graph:

```text
videotestsrc → videoconvert → fakesink
```

Controlled failure:

```text
audiotestsrc ──X──> videoconvert
```

A successful link proves that the topology can be connected.

It does not prove:

* runtime caps negotiation success;
* successful device configuration;
* buffer flow;
* stable runtime operation.

---

### Example 05 — Request Pads

Source:

```text
src/05_request_pad.cpp
```

Topics:

* `REQUEST` pad lifecycle
* `tee`
* `gst_element_request_pad_simple()`
* explicit `gst_pad_link()`
* `gst_element_release_request_pad()`
* request-pad ownership

Graph:

```text
                    ┌→ queue-a → fakesink
videotestsrc → tee ─┤
                    └→ queue-b → fakesink
```

Lifecycle:

```text
request
  ↓
link
  ↓
use
  ↓
unlink
  ↓
release
  ↓
unref
```

---

### Example 06 — Ghost Pads and Reusable Bins

Source:

```text
src/06_ghost_pad.cpp
```

Topics:

* `GstGhostPad`
* reusable `GstBin`
* exposing internal pads
* hierarchical graph design

Internal graph:

```text
processing-bin

sink
 ↓
videoconvert → identity
                    ↓
                   src
```

External graph:

```text
videotestsrc
      ↓
processing-bin
      ↓
fakesink
```

The external pipeline does not need to know the internal child topology.

---

### Example 07 — Dynamic Pads

Source:

```text
src/07_dynamic_pad.cpp
```

Topics:

* `SOMETIMES` pads
* `decodebin`
* `pad-added` signal
* dynamic pad callbacks
* media-type validation before linking

Mental model:

```text
decodebin
    │
    │ discovers stream
    ↓
creates new src pad
    │
    │ pad-added
    ↓
application callback
    │
    ↓
gst_pad_link()
```

Difference from request pads:

```text
REQUEST
    application asks the element to create a pad

SOMETIMES
    element creates the pad when runtime stream information becomes available
```

The JPEG encoder supplies a finite encoded stream so `decodebin` exposes a
dynamic pad. JPEG format details are taught in Lab 04; this example checks the
`pad-added` callback and link.

---

## 3. Build

Required elements are `videotestsrc`, `identity`, `videoconvert`, `fakesink`,
`audiotestsrc`, `tee`, `queue`, `jpegenc`, and `decodebin`. Check a missing
element with `gst-inspect-1.0 ELEMENT_NAME` after sourcing `env.sh`.

Activate the development environment first:

```bash
cd /home/tungnhs/TungNHS/Gstreamer-Lesson

source .venv/bin/activate
source env.sh
```

Enter the lab:

```bash
cd practice/lesson-02
```

First build:

```bash
meson setup build
meson compile -C build
```

For subsequent changes:

```bash
meson compile -C build
```

---

## 4. Run

Run examples individually:

```bash
./build/01_registry_factory
./build/02_pad_template
./build/03_bin_pipeline
./build/04_static_linking
./build/05_request_pad
./build/06_ghost_pad
./build/07_dynamic_pad
```

---

## 5. Coding Rules

The examples follow these rules:

* C++17;
* `snake_case` naming;
* no unnecessary objects or variables;
* every GStreamer object has a clear purpose;
* explicit error checking;
* explicit ownership handling;
* Doxygen for functions with meaningful contracts;
* comments explain ownership, intent, or non-obvious behavior;
* lines are kept on one line when reasonably readable;
* no unnecessary line wrapping.

---

## 6. Key Ownership Rules

### Newly Created Element

Before adding it to a bin:

```text
caller owns the element
```

After successful:

```cpp
gst_bin_add(...)
```

the bin owns the child.

The local pointer is then used as a borrowed pointer while the parent remains alive.

---

### Static Pad

```cpp
gst_element_get_static_pad(...)
```

returns a reference that must be released with:

```cpp
gst_object_unref(...)
```

---

### Request Pad

A requested pad requires two different cleanup operations:

```cpp
gst_element_release_request_pad(element, pad);
gst_object_unref(pad);
```

`release_request_pad()` removes the requested pad from the element.

`gst_object_unref()` releases the application's reference.

---

## 7. Key Concepts

After completing this lab, you should be able to explain:

```text
Plugin
Registry
GstPluginFeature
GstElementFactory
GstElement
GstPadTemplate
GstPad
GstBin
GstPipeline
GstGhostPad
```

You should also understand:

```text
ALWAYS
    pad exists with the element instance

REQUEST
    application explicitly requests the pad

SOMETIMES
    element creates the pad depending on runtime stream information
```

And the standard graph-construction workflow:

```text
create
  ↓
add to bin/pipeline
  ↓
configure
  ↓
link
```

---

## 8. Scope Boundary

This lab focuses on the GStreamer object model and graph topology.

The following topics are intentionally covered in later labs:

```text
GstBuffer / GstMemory           → Lab 03
Raw video / YUY2 / MJPEG        → Lab 04
Caps negotiation                → Lab 05
V4L2 / v4l2src                  → Lab 06
State machine                   → Lab 07
GstBus / GstMessage             → Lab 08
```

---

## 9. Completion Criteria

Lab 02 is complete when:

```text
[PASS] Example 01 builds and runs
[PASS] Example 02 builds and runs
[PASS] Example 03 builds and runs
[PASS] Example 04 builds and runs
[PASS] Example 05 builds and runs
[PASS] Example 06 builds and runs
[PASS] Example 07 builds and runs
[PASS] Build produces no project-source warnings
```

You should be able to explain the following without referring to the source code:

```text
Registry → Factory → Element

PadTemplate → actual Pad

Element → Bin → Pipeline

ALWAYS vs REQUEST vs SOMETIMES

Static linking vs dynamic linking

Ghost pad abstraction

GStreamer object ownership after gst_bin_add()
```

## Lab Status

```text
LAB 02: PASS
```

The lab should only be marked `PASS` after all examples have been built and executed successfully.

The seven examples were built and run with the custom GStreamer 1.28.6
installation on 2026-10-24. The request-pad example also checks that both
released pads are absent, and the dynamic-pad example checks that its callback
linked a raw-video pad.
