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

    GstBuffer* buffer = gst_buffer_new_allocate(nullptr, 64, nullptr);

    if (buffer == nullptr) {
        std::cerr << "Failed to create buffer\n";
        return 1;
    }

    GST_BUFFER_PTS(buffer) = 100 * GST_MSECOND;
    GST_BUFFER_DTS(buffer) = 90 * GST_MSECOND;
    GST_BUFFER_DURATION(buffer) = 33 * GST_MSECOND;
    GST_BUFFER_OFFSET(buffer) = 42;
    GST_BUFFER_OFFSET_END(buffer) = 43;
    GST_BUFFER_FLAG_SET(buffer, GST_BUFFER_FLAG_DISCONT);

    GstCaps* reference = gst_caps_from_string("timestamp/x-system-monotonic");

    if (reference == nullptr) {
        std::cerr << "Failed to create timestamp reference caps\n";
        gst_buffer_unref(buffer);
        return 1;
    }

    GstReferenceTimestampMeta* added_meta =
        gst_buffer_add_reference_timestamp_meta(buffer, reference, 500 * GST_MSECOND, 33 * GST_MSECOND);

    if (added_meta == nullptr) {
        std::cerr << "Failed to add reference timestamp metadata\n";
        gst_caps_unref(reference);
        gst_buffer_unref(buffer);
        return 1;
    }

    GstBuffer* metadata_copy = gst_buffer_new();

    if (metadata_copy == nullptr) {
        gst_caps_unref(reference);
        gst_buffer_unref(buffer);
        return 1;
    }

    const gboolean copied =
        gst_buffer_copy_into(metadata_copy, buffer, GST_BUFFER_COPY_METADATA, 0, static_cast<gsize>(-1));

    if (copied == FALSE) {
        std::cerr << "Failed to copy buffer metadata\n";
        gst_buffer_unref(metadata_copy);
        gst_caps_unref(reference);
        gst_buffer_unref(buffer);
        return 1;
    }

    GstReferenceTimestampMeta* copied_meta = gst_buffer_get_reference_timestamp_meta(metadata_copy, reference);

    if (copied_meta == nullptr ||
        GST_BUFFER_PTS(metadata_copy) != 100 * GST_MSECOND ||
        GST_BUFFER_DTS(metadata_copy) != 90 * GST_MSECOND ||
        GST_BUFFER_DURATION(metadata_copy) != 33 * GST_MSECOND ||
        GST_BUFFER_OFFSET(metadata_copy) != 42 ||
        GST_BUFFER_OFFSET_END(metadata_copy) != 43 ||
        !GST_BUFFER_FLAG_IS_SET(metadata_copy, GST_BUFFER_FLAG_DISCONT) ||
        gst_buffer_n_memory(metadata_copy) != 0 ||
        copied_meta->timestamp != 500 * GST_MSECOND ||
        copied_meta->duration != 33 * GST_MSECOND) {
        std::cerr << "Copied metadata differs from the source\n";
        gst_buffer_unref(metadata_copy);
        gst_caps_unref(reference);
        gst_buffer_unref(buffer);
        return 1;
    }

    std::cout << "PTS              : " << GST_BUFFER_PTS(metadata_copy) << '\n';
    std::cout << "DTS              : " << GST_BUFFER_DTS(metadata_copy) << '\n';
    std::cout << "Duration         : " << GST_BUFFER_DURATION(metadata_copy) << '\n';
    std::cout << "Offset           : " << GST_BUFFER_OFFSET(metadata_copy) << '\n';
    std::cout << "DISCONT flag     : " << (GST_BUFFER_FLAG_IS_SET(metadata_copy, GST_BUFFER_FLAG_DISCONT) ? "yes" : "no") << '\n';
    std::cout << "Reference TS     : " << copied_meta->timestamp << '\n';
    std::cout << "Copied memories  : " << gst_buffer_n_memory(metadata_copy) << '\n';

    gst_buffer_unref(metadata_copy);
    gst_caps_unref(reference);
    gst_buffer_unref(buffer);

    std::cout << "Buffer metadata: PASS\n";
    return 0;
}
