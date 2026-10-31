#include <gst/gst.h>

#include <iostream>

#include "branch_observation.hpp"

int main(int argc, char* argv[])
{
    gst_init(&argc, &argv);
    GError* error = nullptr;
    GstElement* pipeline =
        gst_parse_launch("videotestsrc num-buffers=30 ! tee name=t "
                         "t. ! queue ! fakesink name=fast signal-handoffs=true sync=false "
                         "t. ! queue max-size-buffers=2 max-size-bytes=0 max-size-time=0 "
                         "leaky=downstream ! identity sleep-time=100000 ! "
                         "fakesink name=slow signal-handoffs=true sync=false",
                         &error);
    if (error || !pipeline)
    {
        std::cerr << "Could not build the queue-drop graph\n";
        g_clear_error(&error);
        if (pipeline)
            gst_object_unref(pipeline);
        return 1;
    }

    GstElement* fast = gst_bin_get_by_name(GST_BIN(pipeline), "fast");
    GstElement* slow = gst_bin_get_by_name(GST_BIN(pipeline), "slow");
    GstBus* bus = gst_element_get_bus(pipeline);
    if (!fast || !slow || !bus)
    {
        if (fast)
            gst_object_unref(fast);
        if (slow)
            gst_object_unref(slow);
        if (bus)
            gst_object_unref(bus);
        gst_object_unref(pipeline);
        return 1;
    }

    const BranchObservation result = observe_branches(pipeline, bus, fast, slow, 8 * GST_SECOND);
    gst_object_unref(bus);
    gst_object_unref(slow);
    gst_object_unref(fast);
    gst_object_unref(pipeline);

    std::cout << "Fast branch: " << result.first_count
              << ", slow leaky branch: " << result.second_count << '\n';
    if (!result.eos || !result.stopped || result.first_count != 30 || result.second_count == 0 ||
        result.second_count >= result.first_count)
        return 1;
    std::cout << "Bounded queue drops frames on the slow branch: PASS\n";
    return 0;
}
