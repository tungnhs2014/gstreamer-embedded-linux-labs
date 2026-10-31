#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>

#include <array>
#include <iostream>

/** @brief Pushes three small raw frames; gst_app_src_push_buffer takes each reference. */
static bool push_frames(GstAppSrc* source)
{
    for (guint index = 0; index < 3; ++index)
    {
        GstBuffer* buffer = gst_buffer_new_allocate(nullptr, 4, nullptr);
        if (!buffer)
            return false;
        const guint8 pixels[4] = {static_cast<guint8>(index), 1, 2, 3};
        if (gst_buffer_fill(buffer, 0, pixels, sizeof(pixels)) != sizeof(pixels))
        {
            gst_buffer_unref(buffer);
            return false;
        }
        GST_BUFFER_PTS(buffer) = index * GST_SECOND;
        GST_BUFFER_DURATION(buffer) = GST_SECOND;
        if (gst_app_src_push_buffer(source, buffer) != GST_FLOW_OK)
            return false;
    }
    return gst_app_src_end_of_stream(source) == GST_FLOW_OK;
}

/** @brief Checks the full format, timing and four-byte payload of one sample. */
static bool sample_matches_frame(GstSample* sample, guint index)
{
    GstBuffer* buffer = gst_sample_get_buffer(sample);
    const GstCaps* caps = gst_sample_get_caps(sample);
    const GstStructure* structure =
        caps && gst_caps_get_size(caps) == 1 ? gst_caps_get_structure(caps, 0) : nullptr;
    if (!buffer || !structure)
        return false;

    gint width = 0;
    gint height = 0;
    gint fps_n = 0;
    gint fps_d = 0;
    const bool format_matches =
        g_strcmp0(gst_structure_get_name(structure), "video/x-raw") == 0 &&
        g_strcmp0(gst_structure_get_string(structure, "format"), "GRAY8") == 0 &&
        gst_structure_get_int(structure, "width", &width) && width == 2 &&
        gst_structure_get_int(structure, "height", &height) && height == 2 &&
        gst_structure_get_fraction(structure, "framerate", &fps_n, &fps_d) && fps_n == 1 &&
        fps_d == 1;

    std::array<guint8, 4> pixels{};
    const std::array<guint8, 4> expected = {static_cast<guint8>(index), 1, 2, 3};
    return format_matches && GST_BUFFER_PTS(buffer) == index * GST_SECOND &&
           GST_BUFFER_DURATION(buffer) == GST_SECOND &&
           gst_buffer_get_size(buffer) == pixels.size() &&
           gst_buffer_extract(buffer, 0, pixels.data(), pixels.size()) == pixels.size() &&
           pixels == expected;
}

/** @brief Pulls and releases each owned sample. */
static bool pull_frames(GstAppSink* sink)
{
    for (guint index = 0; index < 3; ++index)
    {
        GstSample* sample = gst_app_sink_try_pull_sample(sink, 5 * GST_SECOND);
        if (!sample)
            return false;
        const bool valid = sample_matches_frame(sample, index);
        gst_sample_unref(sample);
        if (!valid)
            return false;
    }
    return true;
}

/** @brief Runs the appsrc-to-appsink transfer and requires pipeline EOS. */
static bool transfer_frames(GstElement* pipeline, GstElement* source, GstElement* sink, GstBus* bus)
{
    if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
        return false;
    if (!push_frames(GST_APP_SRC(source)) || !pull_frames(GST_APP_SINK(sink)))
        return false;

    GstMessage* message = gst_bus_timed_pop_filtered(
        bus, 5 * GST_SECOND, static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
    const bool eos = message && GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS;
    if (message)
        gst_message_unref(message);
    return eos;
}

int main(int argc, char* argv[])
{
    gst_init(&argc, &argv);
    GError* error = nullptr;
    GstElement* pipeline =
        gst_parse_launch("appsrc name=source format=time "
                         "caps=video/x-raw,format=GRAY8,width=2,height=2,framerate=1/1 ! "
                         "appsink name=sink sync=false max-buffers=3",
                         &error);
    if (error || !pipeline)
    {
        std::cerr << "Could not build appsrc-to-appsink graph\n";
        g_clear_error(&error);
        if (pipeline)
            gst_object_unref(pipeline);
        return 1;
    }

    GstElement* source = gst_bin_get_by_name(GST_BIN(pipeline), "source");
    GstElement* sink = gst_bin_get_by_name(GST_BIN(pipeline), "sink");
    GstBus* bus = gst_element_get_bus(pipeline);
    if (!source || !sink || !bus)
    {
        if (source)
            gst_object_unref(source);
        if (sink)
            gst_object_unref(sink);
        if (bus)
            gst_object_unref(bus);
        gst_object_unref(pipeline);
        return 1;
    }

    const bool transferred = transfer_frames(pipeline, source, sink, bus);

    const bool stopped =
        gst_element_set_state(pipeline, GST_STATE_NULL) != GST_STATE_CHANGE_FAILURE;
    gst_object_unref(bus);
    gst_object_unref(sink);
    gst_object_unref(source);
    gst_object_unref(pipeline);
    if (!transferred || !stopped)
        return 1;
    std::cout << "Appsrc to appsink, three frames: PASS\n";
    return 0;
}
