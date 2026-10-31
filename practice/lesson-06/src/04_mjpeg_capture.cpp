#include <gst/gst.h>

#include <iostream>

struct CaptureContext
{
    GstElement* decoder = nullptr; // Borrowed while pipeline remains alive.
    GstCaps* requested = nullptr;  // Owned by main through shutdown.
    guint count = 0;
    bool formats_valid = false;
    bool payloads_valid = true;
};

/** @brief Confirms JPEG input and decoded raw output for borrowed handoff data. */
static void on_handoff(GstElement*, GstBuffer* buffer, GstPad*, gpointer user_data)
{
    auto* context = static_cast<CaptureContext*>(user_data);
    ++context->count;
    context->payloads_valid &= gst_buffer_get_size(buffer) > 0;
    if (context->count != 1)
        return;

    GstPad* input_pad = gst_element_get_static_pad(context->decoder, "sink");
    GstPad* output_pad = gst_element_get_static_pad(context->decoder, "src");
    GstCaps* input = input_pad ? gst_pad_get_current_caps(input_pad) : nullptr;
    GstCaps* output = output_pad ? gst_pad_get_current_caps(output_pad) : nullptr;
    const GstStructure* input_structure =
        input && gst_caps_get_size(input) == 1 ? gst_caps_get_structure(input, 0) : nullptr;
    const GstStructure* output_structure =
        output && gst_caps_get_size(output) == 1 ? gst_caps_get_structure(output, 0) : nullptr;
    context->formats_valid =
        input_structure && output_structure && gst_caps_is_subset(input, context->requested) &&
        g_strcmp0(gst_structure_get_name(input_structure), "image/jpeg") == 0 &&
        g_strcmp0(gst_structure_get_name(output_structure), "video/x-raw") == 0;
    if (input && output)
    {
        gchar* input_text = gst_caps_to_string(input);
        gchar* output_text = gst_caps_to_string(output);
        std::cout << "Camera JPEG caps: " << input_text << "\nDecoder raw caps: " << output_text
                  << '\n';
        g_free(input_text);
        g_free(output_text);
    }
    if (input)
        gst_caps_unref(input);
    if (output)
        gst_caps_unref(output);
    if (input_pad)
        gst_object_unref(input_pad);
    if (output_pad)
        gst_object_unref(output_pad);
}

int main(int argc, char* argv[])
{
    gst_init(&argc, &argv);
    if (argc < 2 || argc > 3)
    {
        std::cerr << "Usage: " << argv[0] << " /dev/videoX [JPEG caps]\n";
        return 2;
    }
    GstCaps* requested = gst_caps_from_string(argc == 3 ? argv[2] : "image/jpeg");
    const GstStructure* requested_structure = requested && gst_caps_get_size(requested) == 1
                                                  ? gst_caps_get_structure(requested, 0)
                                                  : nullptr;
    if (!requested_structure ||
        g_strcmp0(gst_structure_get_name(requested_structure), "image/jpeg") != 0)
    {
        std::cerr << "Requested caps must select image/jpeg\n";
        if (requested)
            gst_caps_unref(requested);
        return 2;
    }

    GError* parse_error = nullptr;
    GstElement* pipeline = gst_parse_launch(
        "v4l2src name=camera num-buffers=3 ! capsfilter name=mode ! "
        "jpegdec name=decoder ! fakesink name=sink signal-handoffs=true sync=false",
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
    GstElement* decoder = gst_bin_get_by_name(GST_BIN(pipeline), "decoder");
    GstElement* sink = gst_bin_get_by_name(GST_BIN(pipeline), "sink");
    GstBus* bus = gst_element_get_bus(pipeline);
    if (!camera || !mode || !decoder || !sink || !bus)
    {
        std::cerr << "Missing MJPEG pipeline component\n";
        if (camera)
            gst_object_unref(camera);
        if (mode)
            gst_object_unref(mode);
        if (decoder)
            gst_object_unref(decoder);
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
    CaptureContext context{decoder, requested};
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
    gst_object_unref(decoder);
    gst_object_unref(mode);
    gst_object_unref(camera);
    gst_object_unref(pipeline);
    gst_caps_unref(requested);

    std::cout << "Decoded buffers: " << context.count << '\n';
    if (!eos || !stopped || context.count != 3 || !context.formats_valid || !context.payloads_valid)
        return 1;
    std::cout << "MJPEG camera capture: PASS\n";
    return 0;
}
