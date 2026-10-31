#include <gst/gst.h>

#include <iostream>

struct HandoffContext
{
    guint buffer_count = 0;
    bool payloads_valid = true;
};

/** @brief Observes decoded and converted buffers at the final sink. */
static void on_handoff(GstElement*, GstBuffer* buffer, GstPad*, gpointer user_data)
{
    auto* context = static_cast<HandoffContext*>(user_data);
    ++context->buffer_count;
    context->payloads_valid &= gst_buffer_get_size(buffer) > 0;
}

/**
 * @brief Creates an element and transfers ownership to a bin.
 */
static GstElement* create_and_add_element(GstBin* bin, const char* factory_name,
                                          const char* element_name)
{
    GstElement* element = gst_element_factory_make(factory_name, element_name);

    if (element == nullptr)
    {
        return nullptr;
    }

    gst_object_ref_sink(element);

    if (gst_bin_add(bin, element) == FALSE)
    {
        gst_object_unref(element);
        return nullptr;
    }

    gst_object_unref(element);
    return element; // Borrowed from bin while the bin remains alive.
}

/**
 * @brief Prints and validates the current negotiated caps of one static pad.
 */
static bool check_current_caps(GstElement* element, const char* pad_name, const char* label,
                               const char* expected_media_type,
                               const char* expected_format = nullptr)
{
    GstPad* pad = gst_element_get_static_pad(element, pad_name);

    if (pad == nullptr)
    {
        return false;
    }

    GstCaps* caps = gst_pad_get_current_caps(pad);
    gst_object_unref(pad);

    if (caps == nullptr)
    {
        return false;
    }

    const GstStructure* structure =
        gst_caps_get_size(caps) == 1 ? gst_caps_get_structure(caps, 0) : nullptr;
    const bool valid =
        structure != nullptr &&
        g_strcmp0(gst_structure_get_name(structure), expected_media_type) == 0 &&
        (expected_format == nullptr ||
         g_strcmp0(gst_structure_get_string(structure, "format"), expected_format) == 0);
    gchar* caps_text = gst_caps_to_string(caps);
    std::cout << label << ": " << caps_text << '\n';

    g_free(caps_text);
    gst_caps_unref(caps);

    return valid;
}

struct PipelineParts
{
    GstElement* source = nullptr;
    GstElement* input_filter = nullptr;
    GstElement* encoder = nullptr;
    GstElement* decoder = nullptr;
    GstElement* converter = nullptr;
    GstElement* output_filter = nullptr;
    GstElement* sink = nullptr;
};

/** @brief Creates the seven children; returned pointers are borrowed from pipeline. */
static bool create_parts(GstElement* pipeline, PipelineParts& parts)
{
    GstBin* bin = GST_BIN(pipeline);
    parts.source = create_and_add_element(bin, "videotestsrc", "source");
    parts.input_filter = create_and_add_element(bin, "capsfilter", "input-filter");
    parts.encoder = create_and_add_element(bin, "jpegenc", "jpeg-encoder");
    parts.decoder = create_and_add_element(bin, "jpegdec", "jpeg-decoder");
    parts.converter = create_and_add_element(bin, "videoconvert", "converter");
    parts.output_filter = create_and_add_element(bin, "capsfilter", "output-filter");
    parts.sink = create_and_add_element(bin, "fakesink", "sink");
    return parts.source && parts.input_filter && parts.encoder && parts.decoder &&
           parts.converter && parts.output_filter && parts.sink;
}

/** @brief Sets requested caps and links the complete encode/decode graph. */
static bool configure_and_link(const PipelineParts& parts, HandoffContext& context)
{
    GstCaps* input =
        gst_caps_from_string("video/x-raw,format=YUY2,width=320,height=240,framerate=30/1");
    GstCaps* output =
        gst_caps_from_string("video/x-raw,format=NV12,width=320,height=240,framerate=30/1");
    if (!input || !output)
    {
        if (input)
            gst_caps_unref(input);
        if (output)
            gst_caps_unref(output);
        return false;
    }

    g_object_set(parts.source, "num-buffers", 3, nullptr);
    g_object_set(parts.sink, "signal-handoffs", TRUE, "sync", FALSE, nullptr);
    g_object_set(parts.input_filter, "caps", input, nullptr);
    g_object_set(parts.output_filter, "caps", output, nullptr);
    gst_caps_unref(input);
    gst_caps_unref(output);
    g_signal_connect(parts.sink, "handoff", G_CALLBACK(on_handoff), &context);

    return gst_element_link(parts.source, parts.input_filter) &&
           gst_element_link(parts.input_filter, parts.encoder) &&
           gst_element_link(parts.encoder, parts.decoder) &&
           gst_element_link(parts.decoder, parts.converter) &&
           gst_element_link(parts.converter, parts.output_filter) &&
           gst_element_link(parts.output_filter, parts.sink);
}

/** @brief Checks all four relevant current-caps boundaries after preroll. */
static bool check_formats(const PipelineParts& parts)
{
    const bool input =
        check_current_caps(parts.input_filter, "src", "Encoder input", "video/x-raw", "YUY2");
    const bool encoded = check_current_caps(parts.encoder, "src", "Encoder output", "image/jpeg");
    const bool decoded = check_current_caps(parts.decoder, "src", "Decoder output", "video/x-raw");
    const bool converted =
        check_current_caps(parts.converter, "src", "Converter output", "video/x-raw", "NV12");
    return input && encoded && decoded && converted;
}

/** @brief Waits for EOS or prints the real pipeline error. */
static bool wait_for_eos(GstElement* pipeline)
{
    GstBus* bus = gst_element_get_bus(pipeline);
    if (!bus)
        return false;

    GstMessage* message = gst_bus_timed_pop_filtered(
        bus, 10 * GST_SECOND, static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
    const bool eos = message && GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS;
    if (message && GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR)
    {
        GError* error = nullptr;
        gchar* debug = nullptr;
        gst_message_parse_error(message, &error, &debug);
        std::cerr << "Pipeline error: " << (error ? error->message : "unknown") << '\n';
        g_clear_error(&error);
        g_free(debug);
    }
    else if (!message)
    {
        std::cerr << "Timed out waiting for EOS\n";
    }
    if (message)
        gst_message_unref(message);
    gst_object_unref(bus);
    return eos;
}

/** @brief Prerolls, validates the data formats, then counts three output buffers. */
static bool run_pipeline(GstElement* pipeline, const PipelineParts& parts, HandoffContext& context)
{
    if (gst_element_set_state(pipeline, GST_STATE_PAUSED) == GST_STATE_CHANGE_FAILURE ||
        gst_element_get_state(pipeline, nullptr, nullptr, 5 * GST_SECOND) !=
            GST_STATE_CHANGE_SUCCESS)
    {
        std::cerr << "MJPEG preroll failed\n";
        return false;
    }
    if (!check_formats(parts) ||
        gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
    {
        std::cerr << "MJPEG format validation or PLAYING transition failed\n";
        return false;
    }
    const bool eos = wait_for_eos(pipeline);
    return eos && context.buffer_count == 3 && context.payloads_valid;
}

int main(int argc, char* argv[])
{
    gst_init(&argc, &argv);
    GstElement* pipeline = gst_pipeline_new("mjpeg-decode-pipeline");
    if (!pipeline)
        return 1;

    PipelineParts parts{};
    HandoffContext context{};
    const bool configured = create_parts(pipeline, parts) && configure_and_link(parts, context);
    const bool passed = configured && run_pipeline(pipeline, parts, context);
    const bool stopped =
        gst_element_set_state(pipeline, GST_STATE_NULL) != GST_STATE_CHANGE_FAILURE;
    gst_object_unref(pipeline);

    if (!passed || !stopped)
        return 1;
    std::cout << "Buffers at final sink: " << context.buffer_count << '\n';
    std::cout << "MJPEG decode pipeline: PASS\n";
    return 0;
}
