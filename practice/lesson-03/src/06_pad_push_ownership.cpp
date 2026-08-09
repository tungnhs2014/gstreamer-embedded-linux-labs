#include <gst/gst.h>

#include <iostream>

int main(int argc, char* argv[])
{
    GError* error = nullptr;

    if (gst_init_check(&argc, &argv, &error) == FALSE) {
        if (error != nullptr) {
            g_error_free(error);
        }

        return 1;
    }

    GstPad* src_pad = gst_pad_new("src", GST_PAD_SRC);

    if (src_pad == nullptr) {
        std::cerr << "Failed to create source pad\n";
        return 1;
    }

    if (gst_pad_set_active(src_pad, TRUE) == FALSE) {
        std::cerr << "Failed to activate source pad\n";
        gst_object_unref(src_pad);
        return 1;
    }

    GstBuffer* buffer = gst_buffer_new_allocate(nullptr, 32, nullptr);

    if (buffer == nullptr) {
        gst_pad_set_active(src_pad, FALSE);
        gst_object_unref(src_pad);
        return 1;
    }

    // Keep a separate reference because gst_pad_push() consumes buffer.
    GstBuffer* retained = gst_buffer_ref(buffer);

    const GstFlowReturn result = gst_pad_push(src_pad, buffer);

    std::cout << "Push result         : " << gst_flow_get_name(result) << '\n';
    std::cout << "Retained buffer size: " << gst_buffer_get_size(retained) << '\n';

    gst_buffer_unref(retained);
    gst_pad_set_active(src_pad, FALSE);
    gst_object_unref(src_pad);

    if (result != GST_FLOW_NOT_LINKED) {
        std::cerr << "Expected controlled NOT_LINKED result\n";
        return 1;
    }

    std::cout << "gst_pad_push ownership: PASS\n";
    return 0;
}