#include <gst/gst.h>

#include <iostream>

struct HandoffContext
{
    guint buffer_count = 0;
    bool caps_valid = false;
    bool payloads_valid = true;
};

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
 * @brief Prints compressed JPEG buffer sizes received by the sink.
 */
static void on_handoff(GstElement*, GstBuffer* buffer, GstPad* pad, gpointer user_data)
{
    auto* context = static_cast<HandoffContext*>(user_data);
    ++context->buffer_count;

    if (context->buffer_count == 1)
    {
        GstCaps* caps = gst_pad_get_current_caps(pad);

        if (caps != nullptr)
        {
            const GstStructure* structure =
                gst_caps_get_size(caps) == 1 ? gst_caps_get_structure(caps, 0) : nullptr;
            context->caps_valid = structure != nullptr &&
                                  g_strcmp0(gst_structure_get_name(structure), "image/jpeg") == 0;
            gchar* caps_text = gst_caps_to_string(caps);
            std::cout << "Compressed caps: " << caps_text << '\n';
            g_free(caps_text);
            gst_caps_unref(caps);
        }
    }

    context->payloads_valid &= gst_buffer_get_size(buffer) > 0;
    std::cout << "JPEG frame " << context->buffer_count
              << " payload: " << gst_buffer_get_size(buffer) << " bytes\n";
}

int main(int argc, char* argv[])
{
    gst_init(&argc, &argv);

    GstElement* pipeline = gst_pipeline_new("mjpeg-pipeline");

    if (pipeline == nullptr)
    {
        return 1;
    }

    GstElement* source = create_and_add_element(GST_BIN(pipeline), "videotestsrc", "source");
    GstElement* filter = create_and_add_element(GST_BIN(pipeline), "capsfilter", "raw-filter");
    GstElement* encoder = create_and_add_element(GST_BIN(pipeline), "jpegenc", "jpeg-encoder");
    GstElement* sink = create_and_add_element(GST_BIN(pipeline), "fakesink", "sink");

    if (source == nullptr || filter == nullptr || encoder == nullptr || sink == nullptr)
    {
        gst_object_unref(pipeline);
        return 1;
    }

    GstCaps* caps =
        gst_caps_from_string("video/x-raw,format=YUY2,width=320,height=240,framerate=30/1");

    if (caps == nullptr)
    {
        gst_object_unref(pipeline);
        return 1;
    }

    g_object_set(source, "num-buffers", 5, nullptr);
    g_object_set(filter, "caps", caps, nullptr);
    g_object_set(sink, "signal-handoffs", TRUE, "sync", FALSE, nullptr);
    gst_caps_unref(caps);

    HandoffContext context{};
    g_signal_connect(sink, "handoff", G_CALLBACK(on_handoff), &context);

    if (gst_element_link(source, filter) == FALSE || gst_element_link(filter, encoder) == FALSE ||
        gst_element_link(encoder, sink) == FALSE)
    {
        gst_object_unref(pipeline);
        return 1;
    }

    GstBus* bus = gst_element_get_bus(pipeline);

    if (bus == nullptr)
    {
        gst_object_unref(pipeline);
        return 1;
    }

    if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
    {
        std::cerr << "Failed to start MJPEG pipeline\n";
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(bus);
        gst_object_unref(pipeline);
        return 1;
    }

    GstMessage* message = gst_bus_timed_pop_filtered(
        bus, 10 * GST_SECOND, static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));

    const bool success = message != nullptr && GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS;

    if (message != nullptr && GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR)
    {
        GError* error = nullptr;
        gchar* debug = nullptr;
        gst_message_parse_error(message, &error, &debug);
        std::cerr << "Pipeline error: " << error->message << '\n';
        g_error_free(error);
        g_free(debug);
    }
    else if (message == nullptr)
    {
        std::cerr << "Timed out waiting for EOS\n";
    }

    if (message != nullptr)
    {
        gst_message_unref(message);
    }

    const bool stopped =
        gst_element_set_state(pipeline, GST_STATE_NULL) != GST_STATE_CHANGE_FAILURE;
    gst_object_unref(bus);
    gst_object_unref(pipeline);

    if (!success || !stopped || context.buffer_count != 5 || !context.caps_valid ||
        !context.payloads_valid)
    {
        return 1;
    }

    std::cout << "MJPEG compressed payload: PASS\n";
    return 0;
}
