#include <gst/gst.h>

#include <iostream>

struct HandoffContext
{
    guint count = 0;
    bool caps_valid = false;
};

/** @brief Inspects borrowed handoff data while the pipeline is running. */
static void on_handoff(GstElement*, GstBuffer* buffer, GstPad* pad, gpointer user_data)
{
    auto* context = static_cast<HandoffContext*>(user_data);
    ++context->count;
    if (context->count != 1)
        return;

    GstCaps* caps = gst_pad_get_current_caps(pad); // Full reference.
    const GstStructure* structure =
        caps && gst_caps_get_size(caps) == 1 ? gst_caps_get_structure(caps, 0) : nullptr;
    gint width = 0;
    gint height = 0;
    gint fps_n = 0;
    gint fps_d = 0;
    context->caps_valid = structure != nullptr && gst_buffer_get_size(buffer) > 0 &&
                          g_strcmp0(gst_structure_get_name(structure), "video/x-raw") == 0 &&
                          g_strcmp0(gst_structure_get_string(structure, "format"), "NV12") == 0 &&
                          gst_structure_get_int(structure, "width", &width) && width == 160 &&
                          gst_structure_get_int(structure, "height", &height) && height == 120 &&
                          gst_structure_get_fraction(structure, "framerate", &fps_n, &fps_d) &&
                          fps_n == 30 && fps_d == 1;
    if (caps)
        gst_caps_unref(caps);
}

int main(int argc, char* argv[])
{
    gst_init(&argc, &argv);
    GError* parse_error = nullptr;
    GstElement* pipeline = gst_parse_launch(
        "videotestsrc num-buffers=3 ! "
        "capsfilter caps=video/x-raw,format=NV12,width=160,height=120,framerate=30/1 ! "
        "fakesink name=sink signal-handoffs=true sync=false",
        &parse_error);
    if (parse_error != nullptr || pipeline == nullptr)
    {
        if (parse_error)
        {
            std::cerr << parse_error->message << '\n';
            g_error_free(parse_error);
        }
        if (pipeline)
            gst_object_unref(pipeline);
        return 1;
    }

    GstElement* sink = gst_bin_get_by_name(GST_BIN(pipeline), "sink");
    GstBus* bus = gst_element_get_bus(pipeline);
    if (sink == nullptr || bus == nullptr)
    {
        if (sink)
            gst_object_unref(sink);
        if (bus)
            gst_object_unref(bus);
        gst_object_unref(pipeline);
        return 1;
    }

    HandoffContext context{};
    g_signal_connect(sink, "handoff", G_CALLBACK(on_handoff), &context);
    const GstStateChangeReturn state = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    GstMessage* message = state != GST_STATE_CHANGE_FAILURE
                              ? gst_bus_timed_pop_filtered(bus, 5 * GST_SECOND,
                                                           static_cast<GstMessageType>(
                                                               GST_MESSAGE_EOS | GST_MESSAGE_ERROR))
                              : nullptr;
    const bool eos = message != nullptr && GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS;
    if (message && GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR)
    {
        GError* error = nullptr;
        gst_message_parse_error(message, &error, nullptr);
        std::cerr << "Negotiation error: " << error->message << '\n';
        g_error_free(error);
    }
    if (message)
        gst_message_unref(message);
    const bool stopped =
        gst_element_set_state(pipeline, GST_STATE_NULL) != GST_STATE_CHANGE_FAILURE;
    gst_object_unref(bus);
    gst_object_unref(sink);
    gst_object_unref(pipeline);

    std::cout << "Buffers: " << context.count << ", negotiated NV12: " << context.caps_valid
              << '\n';
    if (!eos || !stopped || context.count != 3 || !context.caps_valid)
        return 1;
    std::cout << "Runtime negotiation: PASS\n";
    return 0;
}
