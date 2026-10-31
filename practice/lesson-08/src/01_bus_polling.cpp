#include <gst/gst.h>

#include <iostream>

struct PollResult
{
    guint messages = 0;
    bool eos = false;
    bool failed = false;
};

/** @brief Consumes every Bus message until EOS, ERROR or the absolute deadline. */
static PollResult poll_bus(GstBus* bus)
{
    PollResult result{};
    const gint64 deadline = g_get_monotonic_time() + 5 * G_USEC_PER_SEC;
    while (!result.eos && !result.failed && g_get_monotonic_time() < deadline)
    {
        const gint64 remaining = deadline - g_get_monotonic_time();
        GstMessage* message = gst_bus_timed_pop(bus, remaining > 0 ? remaining * 1000 : 0);
        if (!message)
            break;
        ++result.messages;
        std::cout << "Bus message: " << GST_MESSAGE_TYPE_NAME(message) << '\n';
        if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS)
            result.eos = true;
        else if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR)
        {
            GError* error = nullptr;
            gst_message_parse_error(message, &error, nullptr);
            std::cerr << "Pipeline error: " << (error ? error->message : "unknown") << '\n';
            g_clear_error(&error);
            result.failed = true;
        }
        gst_message_unref(message); // Polling transfers one message reference.
    }
    return result;
}

int main(int argc, char* argv[])
{
    gst_init(&argc, &argv);
    GError* error = nullptr;
    GstElement* pipeline =
        gst_parse_launch("videotestsrc num-buffers=2 ! fakesink sync=false", &error);
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
    GstBus* bus = gst_element_get_bus(pipeline);
    if (!bus)
    {
        gst_object_unref(pipeline);
        return 1;
    }

    const bool started =
        gst_element_set_state(pipeline, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE;
    const PollResult result = started ? poll_bus(bus) : PollResult{0, false, true};

    const bool stopped =
        gst_element_set_state(pipeline, GST_STATE_NULL) != GST_STATE_CHANGE_FAILURE;
    gst_object_unref(bus);
    gst_object_unref(pipeline);
    std::cout << "Messages received: " << result.messages << ", EOS: " << result.eos << '\n';
    if (!result.eos || result.failed || !stopped || result.messages < 2)
        return 1;
    std::cout << "Bus polling: PASS\n";
    return 0;
}
