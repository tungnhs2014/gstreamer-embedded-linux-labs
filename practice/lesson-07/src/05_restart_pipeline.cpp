#include <gst/gst.h>

#include <iostream>

/** @brief Counts borrowed buffers received in both PLAYING cycles. */
static void on_handoff(GstElement*, GstBuffer* buffer, GstPad*, gpointer user_data)
{
    auto* count = static_cast<guint*>(user_data);
    if (gst_buffer_get_size(buffer) > 0)
        ++*count;
}

/** @brief Runs one finite cycle, checks EOS, and reaches NULL before reuse. */
static bool run_cycle(GstElement* pipeline, GstBus* bus)
{
    if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
    {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        return false;
    }
    GstMessage* message = gst_bus_timed_pop_filtered(
        bus, 5 * GST_SECOND, static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
    const bool eos = message && GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS;
    if (message && GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR)
    {
        GError* error = nullptr;
        gst_message_parse_error(message, &error, nullptr);
        std::cerr << (error ? error->message : "unknown pipeline error") << '\n';
        g_clear_error(&error);
    }
    if (message)
        gst_message_unref(message);
    const GstStateChangeReturn stopped = gst_element_set_state(pipeline, GST_STATE_NULL);
    GstState reached = GST_STATE_VOID_PENDING;
    const GstStateChangeReturn settled =
        stopped == GST_STATE_CHANGE_FAILURE
            ? stopped
            : gst_element_get_state(pipeline, &reached, nullptr, 5 * GST_SECOND);
    return eos && settled == GST_STATE_CHANGE_SUCCESS && reached == GST_STATE_NULL;
}

int main(int argc, char* argv[])
{
    gst_init(&argc, &argv);
    GError* error = nullptr;
    GstElement* pipeline = gst_parse_launch(
        "videotestsrc num-buffers=2 ! fakesink name=sink signal-handoffs=true sync=false", &error);
    if (error || !pipeline)
    {
        if (error)
        {
            std::cerr << error->message << '\n';
            g_error_free(error);
        }
        if (pipeline)
            gst_object_unref(pipeline);
        return 1;
    }
    GstElement* sink = gst_bin_get_by_name(GST_BIN(pipeline), "sink");
    GstBus* bus = gst_element_get_bus(pipeline);
    if (!sink || !bus)
    {
        if (sink)
            gst_object_unref(sink);
        if (bus)
            gst_object_unref(bus);
        gst_object_unref(pipeline);
        return 1;
    }
    guint count = 0;
    g_signal_connect(sink, "handoff", G_CALLBACK(on_handoff), &count);
    const bool first = run_cycle(pipeline, bus);
    const guint after_first = count;
    const bool second = first && run_cycle(pipeline, bus);

    gst_object_unref(bus);
    gst_object_unref(sink);
    gst_object_unref(pipeline);
    std::cout << "Buffers after first/second cycle: " << after_first << '/' << count << '\n';
    if (!first || !second || after_first != 2 || count != 4)
        return 1;
    std::cout << "Stop and restart: PASS\n";
    return 0;
}
