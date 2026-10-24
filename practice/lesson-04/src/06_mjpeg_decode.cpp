#include <gst/gst.h>

#include <iostream>

struct HandoffContext {
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
static GstElement* create_and_add_element(GstBin* bin, const char* factory_name, const char* element_name)
{
    GstElement* element = gst_element_factory_make(factory_name, element_name);

    if (element == nullptr) {
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
 * @brief Prints and validates the current negotiated caps of one static pad.
 */
static bool check_current_caps(GstElement* element, const char* pad_name,
                               const char* label, const char* expected_media_type,
                               const char* expected_format = nullptr)
{
    GstPad* pad = gst_element_get_static_pad(element, pad_name);

    if (pad == nullptr) {
        return false;
    }

    GstCaps* caps = gst_pad_get_current_caps(pad);
    gst_object_unref(pad);

    if (caps == nullptr) {
        return false;
    }

    const GstStructure* structure = gst_caps_get_size(caps) == 1 ? gst_caps_get_structure(caps, 0) : nullptr;
    const bool valid = structure != nullptr &&
        g_strcmp0(gst_structure_get_name(structure), expected_media_type) == 0 &&
        (expected_format == nullptr ||
         g_strcmp0(gst_structure_get_string(structure, "format"), expected_format) == 0);
    gchar* caps_text = gst_caps_to_string(caps);
    std::cout << label << ": " << caps_text << '\n';

    g_free(caps_text);
    gst_caps_unref(caps);

    return valid;
}

int main(int argc, char* argv[])
{
    gst_init(&argc, &argv);

    GstElement* pipeline = gst_pipeline_new("mjpeg-decode-pipeline");

    if (pipeline == nullptr) {
        return 1;
    }

    GstElement* source = create_and_add_element(GST_BIN(pipeline), "videotestsrc", "source");
    GstElement* input_filter = create_and_add_element(GST_BIN(pipeline), "capsfilter", "input-filter");
    GstElement* encoder = create_and_add_element(GST_BIN(pipeline), "jpegenc", "jpeg-encoder");
    GstElement* decoder = create_and_add_element(GST_BIN(pipeline), "jpegdec", "jpeg-decoder");
    GstElement* converter = create_and_add_element(GST_BIN(pipeline), "videoconvert", "converter");
    GstElement* output_filter = create_and_add_element(GST_BIN(pipeline), "capsfilter", "output-filter");
    GstElement* sink = create_and_add_element(GST_BIN(pipeline), "fakesink", "sink");

    if (source == nullptr || input_filter == nullptr || encoder == nullptr ||
        decoder == nullptr || converter == nullptr || output_filter == nullptr ||
        sink == nullptr) {
        gst_object_unref(pipeline);
        return 1;
    }

    GstCaps* input_caps = gst_caps_from_string("video/x-raw,format=YUY2,width=320,height=240,framerate=30/1");
    GstCaps* output_caps = gst_caps_from_string("video/x-raw,format=NV12,width=320,height=240,framerate=30/1");

    if (input_caps == nullptr || output_caps == nullptr) {
        if (input_caps != nullptr) {
            gst_caps_unref(input_caps);
        }

        if (output_caps != nullptr) {
            gst_caps_unref(output_caps);
        }

        gst_object_unref(pipeline);
        return 1;
    }

    g_object_set(source, "num-buffers", 3, nullptr);
    g_object_set(sink, "signal-handoffs", TRUE, "sync", FALSE, nullptr);
    g_object_set(input_filter, "caps", input_caps, nullptr);
    g_object_set(output_filter, "caps", output_caps, nullptr);

    gst_caps_unref(input_caps);
    gst_caps_unref(output_caps);

    HandoffContext context{};
    g_signal_connect(sink, "handoff", G_CALLBACK(on_handoff), &context);

    if (gst_element_link(source, input_filter) == FALSE ||
        gst_element_link(input_filter, encoder) == FALSE ||
        gst_element_link(encoder, decoder) == FALSE ||
        gst_element_link(decoder, converter) == FALSE ||
        gst_element_link(converter, output_filter) == FALSE ||
        gst_element_link(output_filter, sink) == FALSE) {
        gst_object_unref(pipeline);
        return 1;
    }

    if (gst_element_set_state(pipeline, GST_STATE_PAUSED) == GST_STATE_CHANGE_FAILURE) {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        return 1;
    }

    const GstStateChangeReturn state_result = gst_element_get_state(pipeline, nullptr, nullptr, 5 * GST_SECOND);

    if (state_result != GST_STATE_CHANGE_SUCCESS) {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        return 1;
    }

    const bool input_ok = check_current_caps(input_filter, "src", "Encoder input", "video/x-raw", "YUY2");
    const bool encoded_ok = check_current_caps(encoder, "src", "Encoder output", "image/jpeg");
    const bool decoded_ok = check_current_caps(decoder, "src", "Decoder output", "video/x-raw");
    const bool converted_ok = check_current_caps(converter, "src", "Converter output", "video/x-raw", "NV12");

    if (!input_ok || !encoded_ok || !decoded_ok || !converted_ok ||
        gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        std::cerr << "MJPEG format validation or PLAYING transition failed\n";
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        return 1;
    }

    GstBus* bus = gst_element_get_bus(pipeline);

    if (bus == nullptr) {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        return 1;
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
    gst_object_unref(pipeline);

    if (!success || !stopped || context.buffer_count != 3 || !context.payloads_valid) {
        return 1;
    }

    std::cout << "Buffers at final sink: " << context.buffer_count << '\n';
    std::cout << "MJPEG decode pipeline: PASS\n";
    return 0;
}
