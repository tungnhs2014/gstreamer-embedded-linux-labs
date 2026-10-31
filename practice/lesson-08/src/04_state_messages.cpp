#include <gst/gst.h>

#include <iostream>

struct StateObservation
{
    guint pipeline_changes = 0;
    guint child_changes = 0;
    bool eos = false;
    bool failed = false;
};

/** @brief Counts the top-level transition separately from child transitions. */
static void record_state_change(GstElement* pipeline, GstMessage* message,
                                StateObservation& observation)
{
    GstState old_state;
    GstState new_state;
    gst_message_parse_state_changed(message, &old_state, &new_state, nullptr);
    if (GST_MESSAGE_SRC(message) == GST_OBJECT(pipeline))
    {
        ++observation.pipeline_changes;
        std::cout << "Pipeline: " << gst_element_state_get_name(old_state) << " -> "
                  << gst_element_state_get_name(new_state) << '\n';
    }
    else
    {
        ++observation.child_changes;
    }
}

/** @brief Reads every Bus message until EOS, ERROR or the absolute deadline. */
static StateObservation collect_messages(GstElement* pipeline, GstBus* bus)
{
    StateObservation observation{};
    if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
    {
        observation.failed = true;
        return observation;
    }
    const gint64 deadline = g_get_monotonic_time() + 5 * G_USEC_PER_SEC;
    while (!observation.eos && !observation.failed && g_get_monotonic_time() < deadline)
    {
        const gint64 remaining = deadline - g_get_monotonic_time();
        GstMessage* message = gst_bus_timed_pop(bus, remaining > 0 ? remaining * 1000 : 0);
        if (!message)
            break;
        if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_STATE_CHANGED)
        {
            record_state_change(pipeline, message, observation);
        }
        else if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS)
        {
            observation.eos = true;
        }
        else if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR)
        {
            GError* error = nullptr;
            gst_message_parse_error(message, &error, nullptr);
            std::cerr << (error ? error->message : "unknown pipeline error") << '\n';
            g_clear_error(&error);
            observation.failed = true;
        }
        gst_message_unref(message);
    }
    return observation;
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

    const StateObservation observation = collect_messages(pipeline, bus);

    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(bus);
    gst_object_unref(pipeline);
    std::cout << "Pipeline changes: " << observation.pipeline_changes
              << ", child changes: " << observation.child_changes << '\n';
    if (!observation.eos || observation.failed || observation.pipeline_changes < 3 ||
        observation.child_changes == 0)
        return 1;
    std::cout << "STATE_CHANGED messages: PASS\n";
    return 0;
}
