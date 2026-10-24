#include <gst/gst.h>

#include <iostream>

/**
 * @brief Creates an element and transfers ownership to a bin.
 */
static GstElement* create_and_add_element(GstBin* bin, const char* factory_name, const char* element_name)
{
    GstElement* element = gst_element_factory_make(factory_name, element_name);

    if (element == nullptr) {
        return nullptr;
    }

    gst_object_ref_sink(element);

    if (gst_bin_add(bin, element) == FALSE) {
        gst_object_unref(element);
        return nullptr;
    }

    gst_object_unref(element);
    return element; // Borrowed from bin while the bin remains alive.
}

/**
 * @brief Prints and validates the negotiated raw format on one element pad.
 */
static bool check_current_caps(GstElement* element, const char* pad_name,
                               const char* label, const char* expected_format)
{
    GstPad* pad = gst_element_get_static_pad(element, pad_name);

    if (pad == nullptr) {
        return false;
    }

    GstCaps* caps = gst_pad_get_current_caps(pad);
    gst_object_unref(pad);

    if (caps == nullptr) {
        return false;
    }

    const GstStructure* structure = gst_caps_get_size(caps) == 1 ? gst_caps_get_structure(caps, 0) : nullptr;
    const bool valid = structure != nullptr &&
        g_strcmp0(gst_structure_get_name(structure), "video/x-raw") == 0 &&
        g_strcmp0(gst_structure_get_string(structure, "format"), expected_format) == 0;
    gchar* caps_text = gst_caps_to_string(caps);
    std::cout << label << ": " << caps_text << '\n';

    g_free(caps_text);
    gst_caps_unref(caps);

    return valid;
}

int main(int argc, char* argv[])
{
    gst_init(&argc, &argv);

    GstElement* pipeline = gst_pipeline_new("raw-conversion-pipeline");

    if (pipeline == nullptr) {
        return 1;
    }

    GstElement* source = create_and_add_element(GST_BIN(pipeline), "videotestsrc", "source");
    GstElement* input_filter = create_and_add_element(GST_BIN(pipeline), "capsfilter", "input-filter");
    GstElement* converter = create_and_add_element(GST_BIN(pipeline), "videoconvert", "converter");
    GstElement* output_filter = create_and_add_element(GST_BIN(pipeline), "capsfilter", "output-filter");
    GstElement* sink = create_and_add_element(GST_BIN(pipeline), "fakesink", "sink");

    if (source == nullptr || input_filter == nullptr || converter == nullptr ||
        output_filter == nullptr || sink == nullptr) {
        gst_object_unref(pipeline);
        return 1;
    }

    GstCaps* input_caps = gst_caps_from_string("video/x-raw,format=YUY2,width=320,height=240,framerate=30/1");
    GstCaps* output_caps = gst_caps_from_string("video/x-raw,format=I420,width=320,height=240,framerate=30/1");

    if (input_caps == nullptr || output_caps == nullptr) {
        if (input_caps != nullptr) {
            gst_caps_unref(input_caps);
        }

        if (output_caps != nullptr) {
            gst_caps_unref(output_caps);
        }

        gst_object_unref(pipeline);
        return 1;
    }

    g_object_set(source, "num-buffers", 3, nullptr);
    g_object_set(input_filter, "caps", input_caps, nullptr);
    g_object_set(output_filter, "caps", output_caps, nullptr);

    gst_caps_unref(input_caps);
    gst_caps_unref(output_caps);

    if (gst_element_link(source, input_filter) == FALSE ||
        gst_element_link(input_filter, converter) == FALSE ||
        gst_element_link(converter, output_filter) == FALSE ||
        gst_element_link(output_filter, sink) == FALSE) {
        gst_object_unref(pipeline);
        return 1;
    }

    if (gst_element_set_state(pipeline, GST_STATE_PAUSED) == GST_STATE_CHANGE_FAILURE) {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        return 1;
    }

    const GstStateChangeReturn state_result = gst_element_get_state(pipeline, nullptr, nullptr, 5 * GST_SECOND);

    if (state_result != GST_STATE_CHANGE_SUCCESS) {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        return 1;
    }

    const bool input_ok = check_current_caps(converter, "sink", "Converter input", "YUY2");
    const bool output_ok = check_current_caps(converter, "src", "Converter output", "I420");

    const bool stopped = gst_element_set_state(pipeline, GST_STATE_NULL) != GST_STATE_CHANGE_FAILURE;
    gst_object_unref(pipeline);

    if (!input_ok || !output_ok || !stopped) {
        return 1;
    }

    std::cout << "Raw format conversion: PASS\n";
    return 0;
}
