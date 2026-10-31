#include <gst/gst.h>

#include <iostream>

struct CaptureContext
{
    GstCaps* requested = nullptr; // Owned by main for the complete callback lifetime.
    guint count = 0;
    bool caps_valid = false;
    bool payloads_valid = true;
};

/** @brief Checks borrowed camera buffers and the first negotiated sink caps. */
static void on_handoff(GstElement*, GstBuffer* buffer, GstPad* pad, gpointer user_data)
{
    auto* context = static_cast<CaptureContext*>(user_data);
    ++context->count;
    context->payloads_valid &= gst_buffer_get_size(buffer) > 0;
    if (context->count != 1)
        return;

    GstCaps* current = gst_pad_get_current_caps(pad);
    const GstStructure* structure =
        current && gst_caps_get_size(current) == 1 ? gst_caps_get_structure(current, 0) : nullptr;
    context->caps_valid = structure != nullptr && gst_caps_is_fixed(current) &&
                          gst_caps_is_subset(current, context->requested) &&
                          g_strcmp0(gst_structure_get_name(structure), "video/x-raw") == 0 &&
                          g_strcmp0(gst_structure_get_string(structure, "format"), "YUY2") == 0;
    if (current)
    {
        gchar* text = gst_caps_to_string(current);
        std::cout << "Captured caps: " << text << '\n';
        g_free(text);
        gst_caps_unref(current);
    }
}

int main(int argc, char* argv[])
{
    gst_init(&argc, &argv);
    if (argc < 2 || argc > 3)
    {
        std::cerr << "Usage: " << argv[0] << " /dev/videoX [YUY2 caps]\n";
        return 2;
    }

    GstCaps* requested = gst_caps_from_string(argc == 3 ? argv[2] : "video/x-raw,format=YUY2");
    const GstStructure* requested_structure = requested && gst_caps_get_size(requested) == 1
                                                  ? gst_caps_get_structure(requested, 0)
                                                  : nullptr;
    if (requested_structure == nullptr ||
        g_strcmp0(gst_structure_get_name(requested_structure), "video/x-raw") != 0 ||
        g_strcmp0(gst_structure_get_string(requested_structure, "format"), "YUY2") != 0)
    {
        std::cerr << "Requested caps must select video/x-raw,format=YUY2\n";
        if (requested)
            gst_caps_unref(requested);
        return 2;
    }

    GError* parse_error = nullptr;
    GstElement* pipeline =
        gst_parse_launch("v4l2src name=camera num-buffers=3 ! capsfilter name=mode ! "
                         "fakesink name=sink signal-handoffs=true sync=false",
                         &parse_error);
    if (parse_error || !pipeline)
    {
        if (parse_error)
        {
            std::cerr << parse_error->message << '\n';
            g_error_free(parse_error);
        }
        if (pipeline)
            gst_object_unref(pipeline);
        gst_caps_unref(requested);
        return 1;
    }

    GstElement* camera = gst_bin_get_by_name(GST_BIN(pipeline), "camera");
    GstElement* mode = gst_bin_get_by_name(GST_BIN(pipeline), "mode");
    GstElement* sink = gst_bin_get_by_name(GST_BIN(pipeline), "sink");
    GstBus* bus = gst_element_get_bus(pipeline);
    if (!camera || !mode || !sink || !bus)
    {
        std::cerr << "Missing camera pipeline component\n";
        if (camera)
            gst_object_unref(camera);
        if (mode)
            gst_object_unref(mode);
        if (sink)
            gst_object_unref(sink);
        if (bus)
            gst_object_unref(bus);
        gst_object_unref(pipeline);
        gst_caps_unref(requested);
        return 1;
    }

    g_object_set(camera, "device", argv[1], nullptr);
    g_object_set(mode, "caps", requested, nullptr);
    CaptureContext context{requested};
    g_signal_connect(sink, "handoff", G_CALLBACK(on_handoff), &context);
    const GstStateChangeReturn state = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    GstMessage* message = state != GST_STATE_CHANGE_FAILURE
                              ? gst_bus_timed_pop_filtered(bus, 20 * GST_SECOND,
                                                           static_cast<GstMessageType>(
                                                               GST_MESSAGE_EOS | GST_MESSAGE_ERROR))
                              : gst_bus_timed_pop_filtered(bus, GST_SECOND, GST_MESSAGE_ERROR);
    const bool eos = message && GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS;
    if (message && GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR)
    {
        GError* error = nullptr;
        gst_message_parse_error(message, &error, nullptr);
        std::cerr << "Capture error: " << (error ? error->message : "unknown") << '\n';
        if (error)
            g_error_free(error);
    }
    else if (!message)
    {
        std::cerr << "Capture timed out or state change failed\n";
    }
    if (message)
        gst_message_unref(message);
    const bool stopped =
        gst_element_set_state(pipeline, GST_STATE_NULL) != GST_STATE_CHANGE_FAILURE;
    gst_object_unref(bus);
    gst_object_unref(sink);
    gst_object_unref(mode);
    gst_object_unref(camera);
    gst_object_unref(pipeline);
    gst_caps_unref(requested);

    std::cout << "Captured buffers: " << context.count << '\n';
    if (!eos || !stopped || context.count != 3 || !context.caps_valid || !context.payloads_valid)
        return 1;
    std::cout << "Raw YUY2 camera capture: PASS\n";
    return 0;
}
