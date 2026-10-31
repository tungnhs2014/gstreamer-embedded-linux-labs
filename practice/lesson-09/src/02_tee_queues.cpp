#include <gst/gst.h>

#include <iostream>

#include "branch_observation.hpp"

int main(int argc, char* argv[])
{
    gst_init(&argc, &argv);
    GError* error = nullptr;
    GstElement* pipeline =
        gst_parse_launch("videotestsrc num-buffers=3 ! tee name=t "
                         "t. ! queue ! fakesink name=first signal-handoffs=true sync=false "
                         "t. ! queue ! fakesink name=second signal-handoffs=true sync=false",
                         &error);
    if (error || !pipeline)
    {
        std::cerr << "Could not build the branched graph\n";
        g_clear_error(&error);
        if (pipeline)
            gst_object_unref(pipeline);
        return 1;
    }

    GstElement* first = gst_bin_get_by_name(GST_BIN(pipeline), "first");
    GstElement* second = gst_bin_get_by_name(GST_BIN(pipeline), "second");
    GstBus* bus = gst_element_get_bus(pipeline);
    if (!first || !second || !bus)
    {
        if (first)
            gst_object_unref(first);
        if (second)
            gst_object_unref(second);
        if (bus)
            gst_object_unref(bus);
        gst_object_unref(pipeline);
        return 1;
    }

    const BranchObservation result = observe_branches(pipeline, bus, first, second, 5 * GST_SECOND);
    gst_object_unref(bus);
    gst_object_unref(second);
    gst_object_unref(first);
    gst_object_unref(pipeline);

    std::cout << "Branch buffers: " << result.first_count << " / " << result.second_count << '\n';
    if (!result.eos || !result.stopped || result.first_count != 3 || result.second_count != 3)
        return 1;
    std::cout << "Tee and queues: PASS\n";
    return 0;
}
