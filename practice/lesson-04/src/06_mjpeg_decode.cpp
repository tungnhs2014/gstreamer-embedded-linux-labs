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

    if (gst_bin_add(bin, element) == FALSE) {
        gst_object_unref(element);
        return nullptr;
    }

    return element;
}

/**
 * @brief Prints the current negotiated caps of one static pad.
 */
static bool print_current_caps(GstElement* element, const char* pad_name, const char* label)
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

    gchar* caps_text = gst_caps_to_string(caps);
    std::cout << label << ": " << caps_text << '\n';

    g_free(caps_text);
    gst_caps_unref(caps);

    return true;
}

int main(int argc, char* argv[])
{
    gst_init(&argc, &argv);

    GstElement* pipeline = gst_pipeline_new("mjpeg-decode-pipeline");

    if (pipeline == nullptr) {
        return 1;
    }

    GstElement* source = create_and_add_element(GST_BIN(pipeline), "videotestsrc", "source");
    GstElement* input_filter = create_and_add_element(GST_BIN(pipeline), "capsfilter", "input-filter");
    GstElement* encoder = create_and_add_element(GST_BIN(pipeline), "jpegenc", "jpeg-encoder");
    GstElement* decoder = create_and_add_element(GST_BIN(pipeline), "jpegdec", "jpeg-decoder");
    GstElement* converter = create_and_add_element(GST_BIN(pipeline), "videoconvert", "converter");
    GstElement* output_filter = create_and_add_element(GST_BIN(pipeline), "capsfilter", "output-filter");
    GstElement* sink = create_and_add_element(GST_BIN(pipeline), "fakesink", "sink");

    if (source == nullptr || input_filter == nullptr || encoder == nullptr ||
        decoder == nullptr || converter == nullptr || output_filter == nullptr ||
        sink == nullptr) {
        gst_object_unref(pipeline);
        return 1;
    }

    GstCaps* input_caps = gst_caps_from_string("video/x-raw,format=YUY2,width=320,height=240,framerate=30/1");
    GstCaps* output_caps = gst_caps_from_string("video/x-raw,format=NV12,width=320,height=240,framerate=30/1");

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
        gst_element_link(input_filter, encoder) == FALSE ||
        gst_element_link(encoder, decoder) == FALSE ||
        gst_element_link(decoder, converter) == FALSE ||
        gst_element_link(converter, output_filter) == FALSE ||
        gst_element_link(output_filter, sink) == FALSE) {
        gst_object_unref(pipeline);
        return 1;
    }

    if (gst_element_set_state(pipeline, GST_STATE_PAUSED) == GST_STATE_CHANGE_FAILURE) {
        gst_object_unref(pipeline);
        return 1;
    }

    const GstStateChangeReturn state_result = gst_element_get_state(pipeline, nullptr, nullptr, 5 * GST_SECOND);

    if (state_result != GST_STATE_CHANGE_SUCCESS) {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        return 1;
    }

    const bool encoded_ok = print_current_caps(encoder, "src", "Encoder output");
    const bool decoded_ok = print_current_caps(decoder, "src", "Decoder output");
    const bool converted_ok = print_current_caps(converter, "src", "Converter output");

    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    GstBus* bus = gst_element_get_bus(pipeline);

    if (bus == nullptr) {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        return 1;
    }

    GstMessage* message = gst_bus_timed_pop_filtered(
        bus, GST_CLOCK_TIME_NONE,
        static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));

    const bool success = message != nullptr && GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS;

    if (message != nullptr) {
        gst_message_unref(message);
    }

    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(bus);
    gst_object_unref(pipeline);

    if (!encoded_ok || !decoded_ok || !converted_ok || !success) {
        return 1;
    }

    std::cout << "MJPEG decode pipeline: PASS\n";
    return 0;
}