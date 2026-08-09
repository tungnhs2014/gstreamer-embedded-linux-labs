#include <gst/gst.h>

#include <atomic>
#include <iostream>

struct DynamicLinkContext {
    GstElement* sink;
    std::atomic_bool linked{false};
};

/**
 * @brief Creates an element and transfers its ownership to a bin.
 */
static GstElement* create_and_add_element(GstBin* bin, const char* factory_name, const char* element_name)
{
    GstElement* element = gst_element_factory_make(factory_name, element_name);

    if (element == nullptr) {
        std::cerr << "Failed to create element '" << element_name << "'\n";
        return nullptr;
    }

    if (gst_bin_add(bin, element) == FALSE) {
        std::cerr << "Failed to add element '" << element_name << "'\n";
        gst_object_unref(element);
        return nullptr;
    }

    return element;
}

/**
 * @brief Links a newly exposed raw-video pad from decodebin to the sink.
 *
 * @param new_pad Borrowed dynamic pad supplied by the pad-added signal.
 * @param user_data DynamicLinkContext supplied by the application.
 */
static void on_pad_added(GstElement*, GstPad* new_pad, gpointer user_data)
{
    auto* context = static_cast<DynamicLinkContext*>(user_data);

    GstPad* sink_pad = gst_element_get_static_pad(context->sink, "sink");

    if (sink_pad == nullptr) {
        std::cerr << "Failed to get sink pad\n";
        return;
    }

    if (gst_pad_is_linked(sink_pad)) {
        gst_object_unref(sink_pad);
        return;
    }

    GstCaps* caps = gst_pad_get_current_caps(new_pad);

    if (caps == nullptr) {
        caps = gst_pad_query_caps(new_pad, nullptr);
    }

    if (caps == nullptr || gst_caps_is_empty(caps) || gst_caps_get_size(caps) == 0) {
        std::cerr << "Dynamic pad has no usable caps\n";

        if (caps != nullptr) {
            gst_caps_unref(caps);
        }

        gst_object_unref(sink_pad);
        return;
    }

    const GstStructure* structure = gst_caps_get_structure(caps, 0);
    const gchar* media_type = gst_structure_get_name(structure);

    std::cout << "Dynamic pad media type: " << media_type << '\n';

    if (!g_str_has_prefix(media_type, "video/x-raw")) {
        std::cout << "Dynamic pad ignored\n";
        gst_caps_unref(caps);
        gst_object_unref(sink_pad);
        return;
    }

    const GstPadLinkReturn link_result = gst_pad_link(new_pad, sink_pad);

    if (GST_PAD_LINK_FAILED(link_result)) {
        std::cerr << "Failed to link dynamic pad\n";
    } else {
        context->linked.store(true);
        std::cout << "Dynamic video pad linked\n";
    }

    gst_caps_unref(caps);
    gst_object_unref(sink_pad);
}

int main(int argc, char* argv[])
{
    GError* error = nullptr;

    if (gst_init_check(&argc, &argv, &error) == FALSE) {
        std::cerr << "Failed to initialize GStreamer";

        if (error != nullptr) {
            std::cerr << ": " << error->message;
            g_error_free(error);
        }

        std::cerr << '\n';
        return 1;
    }

    GstElement* pipeline = gst_pipeline_new("dynamic-pad-pipeline");

    if (pipeline == nullptr) {
        std::cerr << "Failed to create pipeline\n";
        return 1;
    }

    GstElement* source = create_and_add_element(GST_BIN(pipeline), "videotestsrc", "test-source");
    GstElement* encoder = create_and_add_element(GST_BIN(pipeline), "jpegenc", "jpeg-encoder");
    GstElement* decoder = create_and_add_element(GST_BIN(pipeline), "decodebin", "decoder");
    GstElement* sink = create_and_add_element(GST_BIN(pipeline), "fakesink", "test-sink");

    if (source == nullptr || encoder == nullptr || decoder == nullptr || sink == nullptr) {
        gst_object_unref(pipeline);
        return 1;
    }

    g_object_set(source, "num-buffers", 1, nullptr);

    if (gst_element_link(source, encoder) == FALSE || gst_element_link(encoder, decoder) == FALSE) {
        std::cerr << "Failed to link fixed part of graph\n";
        gst_object_unref(pipeline);
        return 1;
    }

    DynamicLinkContext context{sink};
    g_signal_connect(decoder, "pad-added", G_CALLBACK(on_pad_added), &context);

    const GstStateChangeReturn set_result = gst_element_set_state(pipeline, GST_STATE_PAUSED);

    if (set_result == GST_STATE_CHANGE_FAILURE) {
        std::cerr << "Failed to start stream discovery\n";
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        return 1;
    }

    const GstStateChangeReturn wait_result = gst_element_get_state(pipeline, nullptr, nullptr, 5 * GST_SECOND);

    if (wait_result == GST_STATE_CHANGE_FAILURE || wait_result == GST_STATE_CHANGE_ASYNC) {
        std::cerr << "Pipeline did not complete PAUSED transition\n";
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        return 1;
    }

    const bool linked = context.linked.load();

    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);

    if (!linked) {
        std::cerr << "Dynamic pad was not linked\n";
        return 1;
    }

    std::cout << "Dynamic pad handling: PASS\n";
    return 0;
}