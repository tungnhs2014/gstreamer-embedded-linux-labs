#include <gst/gst.h>

#include <iostream>

/** @brief Requests a state and waits briefly for the pipeline to reach it. */
static bool reach_state(GstElement* pipeline, GstState target)
{
    const GstStateChangeReturn requested = gst_element_set_state(pipeline, target);
    GstState actual = GST_STATE_VOID_PENDING;
    const GstStateChangeReturn completed =
        requested == GST_STATE_CHANGE_FAILURE
            ? requested
            : gst_element_get_state(pipeline, &actual, nullptr, 5 * GST_SECOND);
    std::cout << gst_element_state_get_name(target)
              << ": request=" << gst_element_state_change_return_get_name(requested)
              << " reached=" << gst_element_state_get_name(actual) << '\n';
    return requested != GST_STATE_CHANGE_FAILURE && completed != GST_STATE_CHANGE_FAILURE &&
           completed != GST_STATE_CHANGE_ASYNC && actual == target;
}

int main(int argc, char* argv[])
{
    gst_init(&argc, &argv);
    GError* error = nullptr;
    GstElement* pipeline =
        gst_parse_launch("videotestsrc num-buffers=5 ! fakesink sync=false", &error);
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

    const bool ready = reach_state(pipeline, GST_STATE_READY);
    const bool paused = ready && reach_state(pipeline, GST_STATE_PAUSED);
    const bool playing = paused && reach_state(pipeline, GST_STATE_PLAYING);
    const bool stopped = reach_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);

    if (!ready || !paused || !playing || !stopped)
        return 1;
    std::cout << "Basic state transitions: PASS\n";
    return 0;
}
