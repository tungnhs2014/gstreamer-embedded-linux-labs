#include <gst/gst.h>

#include <iostream>

/** @brief Counts borrowed preroll buffers delivered while the sink is PAUSED. */
static void on_preroll(GstElement*, GstBuffer* buffer, GstPad*, gpointer user_data)
{
    auto* count = static_cast<guint*>(user_data);
    if (gst_buffer_get_size(buffer) > 0)
        ++*count;
}

/** @brief Waits for PAUSED and checks that the sink received preroll data. */
static bool observe_preroll(GstElement* pipeline, guint& preroll_count)
{
    const GstStateChangeReturn requested = gst_element_set_state(pipeline, GST_STATE_PAUSED);
    GstState reached = GST_STATE_VOID_PENDING;
    const GstStateChangeReturn completed =
        requested == GST_STATE_CHANGE_FAILURE
            ? requested
            : gst_element_get_state(pipeline, &reached, nullptr, 5 * GST_SECOND);
    std::cout << "PAUSED preroll buffers: " << preroll_count << '\n';
    return completed == GST_STATE_CHANGE_SUCCESS && reached == GST_STATE_PAUSED &&
           preroll_count > 0;
}

int main(int argc, char* argv[])
{
    gst_init(&argc, &argv);
    GError* error = nullptr;
    GstElement* pipeline = gst_parse_launch(
        "videotestsrc num-buffers=3 ! fakesink name=sink signal-handoffs=true", &error);
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
    if (!sink)
    {
        gst_object_unref(pipeline);
        return 1;
    }
    guint preroll_count = 0;
    g_signal_connect(sink, "preroll-handoff", G_CALLBACK(on_preroll), &preroll_count);

    const bool valid = observe_preroll(pipeline, preroll_count);

    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(sink);
    gst_object_unref(pipeline);
    if (!valid)
        return 1;
    std::cout << "Non-live preroll: PASS\n";
    return 0;
}
