#include <gst/gst.h>

#include <iostream>

/** @brief Confirms that a live source reaches PAUSED without a preroll buffer. */
static bool expect_no_preroll(GstElement* pipeline)
{
    const GstStateChangeReturn requested = gst_element_set_state(pipeline, GST_STATE_PAUSED);
    GstState reached = GST_STATE_VOID_PENDING;
    const GstStateChangeReturn completed =
        requested == GST_STATE_CHANGE_FAILURE
            ? requested
            : gst_element_get_state(pipeline, &reached, nullptr, 5 * GST_SECOND);
    std::cout << "Live PAUSED: request=" << gst_element_state_change_return_get_name(requested)
              << " wait=" << gst_element_state_change_return_get_name(completed) << '\n';
    return requested == GST_STATE_CHANGE_NO_PREROLL && completed == GST_STATE_CHANGE_NO_PREROLL &&
           reached == GST_STATE_PAUSED;
}

/** @brief Waits for the live pipeline to reach PLAYING. */
static bool reach_playing(GstElement* pipeline)
{
    const GstStateChangeReturn requested = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    if (requested == GST_STATE_CHANGE_FAILURE)
        return false;
    GstState reached = GST_STATE_VOID_PENDING;
    const GstStateChangeReturn completed =
        gst_element_get_state(pipeline, &reached, nullptr, 5 * GST_SECOND);
    return completed != GST_STATE_CHANGE_FAILURE && completed != GST_STATE_CHANGE_ASYNC &&
           reached == GST_STATE_PLAYING;
}

int main(int argc, char* argv[])
{
    gst_init(&argc, &argv);
    GError* error = nullptr;
    GstElement* pipeline =
        gst_parse_launch("videotestsrc is-live=true ! fakesink sync=false", &error);
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

    const bool no_preroll = expect_no_preroll(pipeline);
    const bool playing = no_preroll && reach_playing(pipeline);
    const bool stopped =
        gst_element_set_state(pipeline, GST_STATE_NULL) != GST_STATE_CHANGE_FAILURE;
    gst_object_unref(pipeline);

    if (!no_preroll || !playing || !stopped)
        return 1;
    std::cout << "Live source NO_PREROLL: PASS\n";
    return 0;
}
