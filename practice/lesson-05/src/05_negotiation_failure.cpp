#include <gst/gst.h>

#include <iostream>

int main(int argc, char* argv[])
{
    gst_init(&argc, &argv);

    GstElement* audio = gst_element_factory_make("audiotestsrc", nullptr);
    GstElement* converter = gst_element_factory_make("videoconvert", nullptr);
    if (audio == nullptr || converter == nullptr)
    {
        if (audio)
            gst_object_unref(audio);
        if (converter)
            gst_object_unref(converter);
        return 1;
    }
    const bool static_link_failed = !gst_element_link(audio, converter);
    gst_object_unref(converter);
    gst_object_unref(audio);

    // Link first with an unconstrained capsfilter, then request audio at runtime.
    GError* parse_error = nullptr;
    GstElement* pipeline = gst_parse_launch(
        "videotestsrc num-buffers=1 ! capsfilter name=filter ! fakesink", &parse_error);
    if (parse_error != nullptr || pipeline == nullptr)
    {
        if (parse_error)
        {
            std::cerr << parse_error->message << '\n';
            g_error_free(parse_error);
        }
        if (pipeline)
            gst_object_unref(pipeline);
        return 1;
    }

    GstElement* filter = gst_bin_get_by_name(GST_BIN(pipeline), "filter");
    GstCaps* impossible = gst_caps_from_string("audio/x-raw");
    if (filter == nullptr || impossible == nullptr)
    {
        if (filter)
            gst_object_unref(filter);
        if (impossible)
            gst_caps_unref(impossible);
        gst_object_unref(pipeline);
        return 1;
    }
    g_object_set(filter, "caps", impossible, nullptr);
    gst_caps_unref(impossible);
    gst_object_unref(filter);

    GstBus* bus = gst_element_get_bus(pipeline);
    const GstStateChangeReturn state = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    std::cout << "Start result: " << gst_element_state_change_return_get_name(state) << '\n';
    GstMessage* message = bus ? gst_bus_timed_pop_filtered(bus, 5 * GST_SECOND,
                                                           static_cast<GstMessageType>(
                                                               GST_MESSAGE_ERROR | GST_MESSAGE_EOS))
                              : nullptr;
    bool not_negotiated = false;
    if (message && GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR)
    {
        GError* error = nullptr;
        gchar* debug = nullptr;
        gst_message_parse_error(message, &error, &debug);
        std::cout << "Runtime error: " << error->message << '\n'
                  << "Debug: " << (debug ? debug : "none") << '\n';
        not_negotiated = debug != nullptr && g_strrstr(debug, "not-negotiated") != nullptr;
        g_error_free(error);
        g_free(debug);
    }
    if (message)
        gst_message_unref(message);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    if (bus)
        gst_object_unref(bus);
    gst_object_unref(pipeline);

    std::cout << "Static incompatible link failed: " << static_link_failed << '\n';
    if (!static_link_failed || !not_negotiated)
        return 1;
    std::cout << "Controlled negotiation failure: PASS\n";
    return 0;
}
