#include <gst/gst.h>

#include <iostream>

/** @brief Requires an EOS message forwarded by the top-level pipeline. */
static bool wait_for_pipeline_eos(GstElement* pipeline, GstBus* bus)
{
    if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
        return false;
    GstMessage* message = gst_bus_timed_pop_filtered(
        bus, 5 * GST_SECOND, static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
    const bool eos = message && GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS &&
                     GST_MESSAGE_SRC(message) == GST_OBJECT(pipeline);
    if (message && GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR)
    {
        GError* error = nullptr;
        gst_message_parse_error(message, &error, nullptr);
        std::cerr << (error ? error->message : "unknown pipeline error") << '\n';
        g_clear_error(&error);
    }
    if (message)
        gst_message_unref(message);
    return eos;
}

int main(int argc, char* argv[])
{
    gst_init(&argc, &argv);
    GError* error = nullptr;
    GstElement* pipeline =
        gst_parse_launch("videotestsrc num-buffers=3 ! fakesink sync=false", &error);
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

    const bool eos = wait_for_pipeline_eos(pipeline, bus);

    GstState current = GST_STATE_VOID_PENDING;
    const GstStateChangeReturn observed =
        gst_element_get_state(pipeline, &current, nullptr, GST_SECOND);
    std::cout << "EOS received: " << eos
              << ", state before cleanup: " << gst_element_state_get_name(current) << '\n';
    const bool stopped =
        gst_element_set_state(pipeline, GST_STATE_NULL) != GST_STATE_CHANGE_FAILURE;
    gst_object_unref(bus);
    gst_object_unref(pipeline);

    if (!eos || observed == GST_STATE_CHANGE_FAILURE || !stopped)
        return 1;
    std::cout << "EOS handling: PASS\n";
    return 0;
}
