#include <gst/gst.h>

#include <iostream>

int main(int argc, char* argv[])
{
    gst_init(&argc, &argv);
    GError* parse_error = nullptr;
    GstElement* pipeline = gst_parse_launch(
        "videotestsrc num-buffers=2 name=source ! "
        "capsfilter caps=video/x-raw,format=I420,width=160,height=120,framerate=30/1 ! "
        "fakesink sync=false",
        &parse_error);

    if (parse_error != nullptr || pipeline == nullptr)
    {
        std::cerr << "Pipeline construction failed\n";
        if (parse_error)
            g_error_free(parse_error);
        if (pipeline)
            gst_object_unref(pipeline);
        return 1;
    }

    GstElement* source = gst_bin_get_by_name(GST_BIN(pipeline), "source");
    GstPad* pad = source ? gst_element_get_static_pad(source, "src") : nullptr;
    GstCaps* template_caps = pad ? gst_pad_get_pad_template_caps(pad) : nullptr;
    GstCaps* query_caps = pad ? gst_pad_query_caps(pad, nullptr) : nullptr;
    GstCaps* before = pad ? gst_pad_get_current_caps(pad) : nullptr;
    GstCaps* requested =
        gst_caps_from_string("video/x-raw,format=I420,width=160,height=120,framerate=30/1");
    const bool before_ok = template_caps != nullptr && query_caps != nullptr &&
                           requested != nullptr && before == nullptr &&
                           gst_caps_can_intersect(template_caps, requested) &&
                           gst_caps_can_intersect(query_caps, requested);

    GstBus* bus = gst_element_get_bus(pipeline);
    const bool started =
        before_ok && bus != nullptr &&
        gst_element_set_state(pipeline, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE;
    GstMessage* message =
        started ? gst_bus_timed_pop_filtered(
                      bus, 5 * GST_SECOND,
                      static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR))
                : nullptr;
    GstCaps* current = pad ? gst_pad_get_current_caps(pad) : nullptr;
    const GstStructure* structure =
        current && gst_caps_get_size(current) == 1 ? gst_caps_get_structure(current, 0) : nullptr;
    const bool current_ok = structure != nullptr && gst_caps_is_fixed(current) &&
                            g_strcmp0(gst_structure_get_name(structure), "video/x-raw") == 0 &&
                            g_strcmp0(gst_structure_get_string(structure, "format"), "I420") == 0;
    const bool eos = message != nullptr && GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS;

    std::cout << "Before negotiation current caps: " << (before ? "present" : "none") << '\n'
              << "Template permits I420: "
              << (template_caps && requested && gst_caps_can_intersect(template_caps, requested))
              << '\n'
              << "Query permits I420: "
              << (query_caps && requested && gst_caps_can_intersect(query_caps, requested)) << '\n'
              << "After negotiation current format: " << (current_ok ? "I420" : "unexpected")
              << '\n';

    if (message)
        gst_message_unref(message);
    if (current)
        gst_caps_unref(current);
    if (before)
        gst_caps_unref(before);
    if (requested)
        gst_caps_unref(requested);
    if (query_caps)
        gst_caps_unref(query_caps);
    if (template_caps)
        gst_caps_unref(template_caps);
    if (pad)
        gst_object_unref(pad);
    if (source)
        gst_object_unref(source);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    if (bus)
        gst_object_unref(bus);
    gst_object_unref(pipeline);

    if (!before_ok || !started || !current_ok || !eos)
        return 1;
    std::cout << "Template/query/current caps: PASS\n";
    return 0;
}
