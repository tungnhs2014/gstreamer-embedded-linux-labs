#include <gst/gst.h>

#include <iostream>

struct HandoffContext {
    guint buffer_count = 0;
    bool caps_valid = false;
    bool sizes_valid = true;
};

/**
 * @brief Creates an element and transfers ownership to a bin.
 */
static GstElement* create_and_add_element(GstBin* bin, const char* factory_name, const char* element_name)
{
    GstElement* element = gst_element_factory_make(factory_name, element_name);

    if (element == nullptr) {
        std::cerr << "Failed to create element '" << element_name << "'\n";
        return nullptr;
    }

    gst_object_ref_sink(element);

    if (gst_bin_add(bin, element) == FALSE) {
        gst_object_unref(element);
        return nullptr;
    }

    gst_object_unref(element);
    return element; // Borrowed from bin while the bin remains alive.
}

/**
 * @brief Prints the negotiated raw caps and buffer size received by fakesink.
 */
static void on_handoff(GstElement*, GstBuffer* buffer, GstPad* pad, gpointer user_data)
{
    auto* context = static_cast<HandoffContext*>(user_data);
    ++context->buffer_count;

    GstCaps* caps = gst_pad_get_current_caps(pad);

    if (context->buffer_count == 1 && caps != nullptr && gst_caps_get_size(caps) == 1) {
        const GstStructure* structure = gst_caps_get_structure(caps, 0);
        gint width = 0;
        gint height = 0;
        gint fps_n = 0;
        gint fps_d = 0;
        context->caps_valid = g_strcmp0(gst_structure_get_name(structure), "video/x-raw") == 0 &&
            g_strcmp0(gst_structure_get_string(structure, "format"), "YUY2") == 0 &&
            gst_structure_get_int(structure, "width", &width) && width == 320 &&
            gst_structure_get_int(structure, "height", &height) && height == 240 &&
            gst_structure_get_fraction(structure, "framerate", &fps_n, &fps_d) &&
            fps_n == 30 && fps_d == 1;
        gchar* caps_text = gst_caps_to_string(caps);
        std::cout << "Negotiated caps: " << caps_text << '\n';
        g_free(caps_text);
    }

    if (caps != nullptr) {
        gst_caps_unref(caps);
    }

    // This fixture uses 320x240 YUY2 with GStreamer's default packed layout.
    context->sizes_valid &= gst_buffer_get_size(buffer) == 320 * 240 * 2;
    std::cout << "Buffer " << context->buffer_count << " size: "
              << gst_buffer_get_size(buffer) << " bytes\n";
}

/**
 * @brief Runs a finite pipeline until EOS or ERROR.
 */
static bool run_pipeline(GstElement* pipeline)
{
    GstBus* bus = gst_element_get_bus(pipeline);

    if (bus == nullptr) {
        return false;
    }

    if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(bus);
        return false;
    }

    GstMessage* message = gst_bus_timed_pop_filtered(
        bus, 10 * GST_SECOND,
        static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));

    const bool success = message != nullptr && GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS;

    if (message != nullptr && GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
        GError* error = nullptr;
        gchar* debug = nullptr;
        gst_message_parse_error(message, &error, &debug);
        std::cerr << "Pipeline error: " << error->message << '\n';
        g_error_free(error);
        g_free(debug);
    } else if (message == nullptr) {
        std::cerr << "Timed out waiting for EOS\n";
    }

    if (message != nullptr) {
        gst_message_unref(message);
    }

    const bool stopped = gst_element_set_state(pipeline, GST_STATE_NULL) != GST_STATE_CHANGE_FAILURE;
    gst_object_unref(bus);

    return success && stopped;
}

int main(int argc, char* argv[])
{
    gst_init(&argc, &argv);

    GstElement* pipeline = gst_pipeline_new("raw-yuy2-pipeline");

    if (pipeline == nullptr) {
        return 1;
    }

    GstElement* source = create_and_add_element(GST_BIN(pipeline), "videotestsrc", "source");
    GstElement* filter = create_and_add_element(GST_BIN(pipeline), "capsfilter", "raw-filter");
    GstElement* sink = create_and_add_element(GST_BIN(pipeline), "fakesink", "sink");

    if (source == nullptr || filter == nullptr || sink == nullptr) {
        gst_object_unref(pipeline);
        return 1;
    }

    g_object_set(source, "num-buffers", 3, nullptr);
    g_object_set(sink, "signal-handoffs", TRUE, "sync", FALSE, nullptr);

    GstCaps* caps = gst_caps_new_simple(
        "video/x-raw",
        "format", G_TYPE_STRING, "YUY2",
        "width", G_TYPE_INT, 320,
        "height", G_TYPE_INT, 240,
        "framerate", GST_TYPE_FRACTION, 30, 1,
        nullptr);

    if (caps == nullptr) {
        gst_object_unref(pipeline);
        return 1;
    }

    g_object_set(filter, "caps", caps, nullptr);
    gst_caps_unref(caps);

    HandoffContext context{};
    g_signal_connect(sink, "handoff", G_CALLBACK(on_handoff), &context);

    if (gst_element_link(source, filter) == FALSE || gst_element_link(filter, sink) == FALSE) {
        gst_object_unref(pipeline);
        return 1;
    }

    const bool success = run_pipeline(pipeline);

    gst_object_unref(pipeline);

    if (!success || context.buffer_count != 3 || !context.caps_valid || !context.sizes_valid) {
        return 1;
    }

    std::cout << "Raw YUY2 pipeline: PASS\n";
    return 0;
}
