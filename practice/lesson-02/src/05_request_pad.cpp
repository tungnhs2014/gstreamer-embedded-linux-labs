#include <gst/gst.h>

#include <iostream>

/**
 * @brief Creates an element and transfers its ownership to a bin.
 *
 * @return Borrowed pointer owned by the bin, or nullptr on failure.
 */
static GstElement* create_and_add_element(GstBin* bin, const char* factory_name, const char* element_name)
{
    GstElement* element = gst_element_factory_make(factory_name, element_name);

    if (element == nullptr) {
        std::cerr << "Failed to create element '" << element_name << "'\n";
        return nullptr;
    }

    gst_object_ref_sink(element);

    if (gst_bin_add(bin, element) == FALSE) {
        std::cerr << "Failed to add element '" << element_name << "'\n";
        gst_object_unref(element);
        return nullptr;
    }

    gst_object_unref(element);
    return element; // Borrowed from bin while the bin remains alive.
}

/**
 * @brief Requests one tee source pad and links it to a queue sink pad.
 *
 * @return Requested GstPad reference. The caller must release and unref it.
 */
static GstPad* request_and_link_tee_pad(GstElement* tee, GstElement* queue)
{
    GstPad* tee_src_pad = gst_element_request_pad_simple(tee, "src_%u");

    if (tee_src_pad == nullptr) {
        std::cerr << "Failed to request tee source pad\n";
        return nullptr;
    }

    GstPad* queue_sink_pad = gst_element_get_static_pad(queue, "sink");

    if (queue_sink_pad == nullptr) {
        std::cerr << "Failed to get queue sink pad\n";
        gst_element_release_request_pad(tee, tee_src_pad);
        gst_object_unref(tee_src_pad);
        return nullptr;
    }

    const GstPadLinkReturn link_result = gst_pad_link(tee_src_pad, queue_sink_pad);

    if (GST_PAD_LINK_FAILED(link_result)) {
        std::cerr << "Failed to link tee request pad to queue\n";
        gst_object_unref(queue_sink_pad);
        gst_element_release_request_pad(tee, tee_src_pad);
        gst_object_unref(tee_src_pad);
        return nullptr;
    }

    gchar* tee_pad_name = gst_object_get_name(GST_OBJECT(tee_src_pad));
    std::cout << "Requested and linked pad: " << tee_pad_name << '\n';
    g_free(tee_pad_name);

    gst_object_unref(queue_sink_pad);

    return tee_src_pad;
}

/**
 * @brief Unlinks, releases and drops the caller's request-pad reference.
 * @return true if the released pad is no longer present on tee.
 */
static bool release_tee_pad(GstElement* tee, GstPad* tee_src_pad, GstElement* queue)
{
    gchar* pad_name = gst_object_get_name(GST_OBJECT(tee_src_pad));
    GstPad* queue_sink_pad = gst_element_get_static_pad(queue, "sink");
    bool unlinked = false;

    if (queue_sink_pad != nullptr) {
        unlinked = gst_pad_unlink(tee_src_pad, queue_sink_pad) != FALSE;

        if (!unlinked) {
            std::cerr << "Failed to unlink tee branch\n";
        }

        gst_object_unref(queue_sink_pad);
    }

    gst_element_release_request_pad(tee, tee_src_pad);
    gst_object_unref(tee_src_pad);

    GstPad* remaining_pad = gst_element_get_static_pad(tee, pad_name);
    const bool released = remaining_pad == nullptr;

    if (remaining_pad != nullptr) {
        gst_object_unref(remaining_pad);
    }

    g_free(pad_name);
    return unlinked && released;
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

    GstElement* pipeline = gst_pipeline_new("request-pad-pipeline");

    if (pipeline == nullptr) {
        std::cerr << "Failed to create pipeline\n";
        return 1;
    }

    GstElement* source = create_and_add_element(GST_BIN(pipeline), "videotestsrc", "test-source");
    GstElement* tee = create_and_add_element(GST_BIN(pipeline), "tee", "splitter");
    GstElement* queue_a = create_and_add_element(GST_BIN(pipeline), "queue", "queue-a");
    GstElement* sink_a = create_and_add_element(GST_BIN(pipeline), "fakesink", "sink-a");
    GstElement* queue_b = create_and_add_element(GST_BIN(pipeline), "queue", "queue-b");
    GstElement* sink_b = create_and_add_element(GST_BIN(pipeline), "fakesink", "sink-b");

    if (source == nullptr || tee == nullptr || queue_a == nullptr || sink_a == nullptr ||
        queue_b == nullptr || sink_b == nullptr) {
        gst_object_unref(pipeline);
        return 1;
    }

    if (gst_element_link(source, tee) == FALSE ||
        gst_element_link(queue_a, sink_a) == FALSE ||
        gst_element_link(queue_b, sink_b) == FALSE) {
        std::cerr << "Failed to link fixed pads\n";
        gst_object_unref(pipeline);
        return 1;
    }

    GstPad* tee_src_a = request_and_link_tee_pad(tee, queue_a);

    if (tee_src_a == nullptr) {
        gst_object_unref(pipeline);
        return 1;
    }

    GstPad* tee_src_b = request_and_link_tee_pad(tee, queue_b);

    if (tee_src_b == nullptr) {
        release_tee_pad(tee, tee_src_a, queue_a);
        gst_object_unref(pipeline);
        return 1;
    }

    std::cout << "Two tee branches created successfully\n";

    const bool branch_b_released = release_tee_pad(tee, tee_src_b, queue_b);
    const bool branch_a_released = release_tee_pad(tee, tee_src_a, queue_a);

    gst_object_unref(pipeline);

    if (!branch_a_released || !branch_b_released) {
        std::cerr << "A tee request pad remained after release\n";
        return 1;
    }

    std::cout << "Request pad lifecycle: PASS\n";
    return 0;
}
