#include <gst/gst.h>

#include <iostream>

/**
 * @brief Creates an element and transfers its ownership to a bin.
 *
 * @param bin Parent bin.
 * @param factory_name Element factory name.
 * @param element_name Element instance name.
 * @return Borrowed pointer owned by the bin, or nullptr on failure.
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
 * @brief Verifies that two static pads are linked to each other.
 *
 * @param upstream Upstream element.
 * @param downstream Downstream element.
 * @return true if upstream.src is linked to downstream.sink.
 */
static bool verify_static_link(GstElement* upstream, GstElement* downstream)
{
    GstPad* src_pad = gst_element_get_static_pad(upstream, "src");
    GstPad* sink_pad = gst_element_get_static_pad(downstream, "sink");

    if (src_pad == nullptr || sink_pad == nullptr) {
        std::cerr << "Failed to get static pads\n";

        if (src_pad != nullptr) {
            gst_object_unref(src_pad);
        }

        if (sink_pad != nullptr) {
            gst_object_unref(sink_pad);
        }

        return false;
    }

    GstPad* peer = gst_pad_get_peer(src_pad);

    const bool linked = peer == sink_pad;

    std::cout << GST_ELEMENT_NAME(upstream) << ".src"
              << " -> "
              << GST_ELEMENT_NAME(downstream) << ".sink"
              << " : " << (linked ? "linked" : "not linked") << '\n';

    if (peer != nullptr) {
        gst_object_unref(peer);
    }

    gst_object_unref(sink_pad);
    gst_object_unref(src_pad);

    return linked;
}

/**
 * @brief Demonstrates a predictable link failure caused by incompatible media types.
 *
 * @return true if the expected link failure occurs.
 */
static bool demonstrate_incompatible_link()
{
    GstElement* pipeline = gst_pipeline_new("failure-pipeline");

    if (pipeline == nullptr) {
        std::cerr << "Failed to create failure pipeline\n";
        return false;
    }

    GstElement* audio_source = create_and_add_element(GST_BIN(pipeline), "audiotestsrc", "audio-source");
    GstElement* video_converter = create_and_add_element(GST_BIN(pipeline), "videoconvert", "video-converter");

    if (audio_source == nullptr || video_converter == nullptr) {
        gst_object_unref(pipeline);
        return false;
    }

    const gboolean linked = gst_element_link(audio_source, video_converter);

    std::cout << "\nControlled incompatible link\n";
    std::cout << "audio-source -> video-converter : " << (linked ? "unexpected success" : "expected failure") << '\n';

    gst_object_unref(pipeline);

    return linked == FALSE;
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

    GstElement* pipeline = gst_pipeline_new("lesson-02-link-pipeline");

    if (pipeline == nullptr) {
        std::cerr << "Failed to create pipeline\n";
        return 1;
    }

    GstElement* source = create_and_add_element(GST_BIN(pipeline), "videotestsrc", "test-source");
    GstElement* converter = create_and_add_element(GST_BIN(pipeline), "videoconvert", "video-converter");
    GstElement* sink = create_and_add_element(GST_BIN(pipeline), "fakesink", "test-sink");

    if (source == nullptr || converter == nullptr || sink == nullptr) {
        gst_object_unref(pipeline);
        return 1;
    }

    if (gst_element_link(source, converter) == FALSE) {
        std::cerr << "Failed to link test-source -> video-converter\n";
        gst_object_unref(pipeline);
        return 1;
    }

    if (gst_element_link(converter, sink) == FALSE) {
        std::cerr << "Failed to link video-converter -> test-sink\n";
        gst_object_unref(pipeline);
        return 1;
    }

    std::cout << "Main graph\n";

    const bool first_link_ok = verify_static_link(source, converter);
    const bool second_link_ok = verify_static_link(converter, sink);

    if (!first_link_ok || !second_link_ok) {
        gst_object_unref(pipeline);
        return 1;
    }

    gst_object_unref(pipeline);

    if (!demonstrate_incompatible_link()) {
        return 1;
    }

    std::cout << "\nStatic linking: PASS\n";
    return 0;
}