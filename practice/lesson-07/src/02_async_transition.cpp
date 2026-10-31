#include <gst/gst.h>

#include <iostream>

/** @brief Requests PAUSED and distinguishes an immediate result from completion. */
static bool observe_paused_transition(GstElement* pipeline)
{
    const GstStateChangeReturn requested = gst_element_set_state(pipeline, GST_STATE_PAUSED);
    GstState reached = GST_STATE_VOID_PENDING;
    const GstStateChangeReturn completed =
        requested == GST_STATE_CHANGE_FAILURE
            ? requested
            : gst_element_get_state(pipeline, &reached, nullptr, 5 * GST_SECOND);
    std::cout << "Request returned " << gst_element_state_change_return_get_name(requested)
              << "; wait returned " << gst_element_state_change_return_get_name(completed)
              << "; reached " << gst_element_state_get_name(reached) << '\n';
    return requested != GST_STATE_CHANGE_FAILURE && completed == GST_STATE_CHANGE_SUCCESS &&
           reached == GST_STATE_PAUSED;
}

int main(int argc, char* argv[])
{
    gst_init(&argc, &argv);
    GError* error = nullptr;
    GstElement* pipeline = gst_parse_launch("videotestsrc num-buffers=2 ! fakesink", &error);
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

    const bool valid = observe_paused_transition(pipeline);
    const bool stopped =
        gst_element_set_state(pipeline, GST_STATE_NULL) != GST_STATE_CHANGE_FAILURE;
    gst_object_unref(pipeline);
    if (!valid || !stopped)
        return 1;
    std::cout << "Asynchronous state observation: PASS\n";
    return 0;
}
