#include <gst/gst.h>

#include <iostream>

struct TimestampObservation
{
    guint count = 0;
    GstClockTime previous = GST_CLOCK_TIME_NONE;
    bool valid_and_ordered = true;
};

/** @brief Inspects borrowed buffers; the callback does not own them. */
static void on_handoff(GstElement*, GstBuffer* buffer, GstPad*, gpointer user_data)
{
    auto* observation = static_cast<TimestampObservation*>(user_data);
    const GstClockTime pts = GST_BUFFER_PTS(buffer);
    observation->valid_and_ordered &=
        GST_CLOCK_TIME_IS_VALID(pts) &&
        (!GST_CLOCK_TIME_IS_VALID(observation->previous) || pts >= observation->previous);
    observation->previous = pts;
    ++observation->count;
}

/** @brief Runs the finite live source and waits for a real EOS message. */
static bool collect_timestamps(GstElement* pipeline, GstElement* sink, GstBus* bus,
                               TimestampObservation& observation)
{
    g_signal_connect(sink, "handoff", G_CALLBACK(on_handoff), &observation);
    if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
        return false;
    GstMessage* message = gst_bus_timed_pop_filtered(
        bus, 5 * GST_SECOND, static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
    const bool eos = message && GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS;
    if (message)
        gst_message_unref(message);
    return eos && observation.count == 3 && observation.valid_and_ordered;
}

/** @brief Reads the pipeline clock and base time while it remains active. */
static bool print_clock_observation(GstElement* pipeline, const TimestampObservation& observation)
{
    GstClock* clock = gst_element_get_clock(pipeline);
    const GstClockTime base_time = gst_element_get_base_time(pipeline);
    const bool available = clock && GST_CLOCK_TIME_IS_VALID(base_time);
    std::cout << "Buffers: " << observation.count << ", last PTS: " << observation.previous
              << ", base time: " << base_time << '\n';
    if (clock)
        gst_object_unref(clock);
    return available;
}

int main(int argc, char* argv[])
{
    gst_init(&argc, &argv);
    GError* error = nullptr;
    GstElement* pipeline = gst_parse_launch("videotestsrc is-live=true num-buffers=3 ! "
                                            "fakesink name=sink signal-handoffs=true sync=false",
                                            &error);
    if (error || !pipeline)
    {
        std::cerr << "Could not build the timestamp graph\n";
        g_clear_error(&error);
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

    TimestampObservation observation{};
    const bool captured = collect_timestamps(pipeline, sink, bus, observation);
    const bool clock_available = print_clock_observation(pipeline, observation);
    const bool stopped =
        gst_element_set_state(pipeline, GST_STATE_NULL) != GST_STATE_CHANGE_FAILURE;
    gst_object_unref(bus);
    gst_object_unref(sink);
    gst_object_unref(pipeline);

    if (!captured || !clock_available || !stopped)
        return 1;
    std::cout << "Live timestamp observation: PASS\n";
    return 0;
}
