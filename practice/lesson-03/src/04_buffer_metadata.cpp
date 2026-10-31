#include <gst/gst.h>

#include <iostream>

/** @brief Copies metadata only and verifies that no payload memory was copied. */
static GstBuffer* copy_verified_metadata(GstBuffer* source, GstCaps* reference)
{
    GstBuffer* copy = gst_buffer_new();
    if (!copy)
        return nullptr;

    const gboolean copied =
        gst_buffer_copy_into(copy, source, GST_BUFFER_COPY_METADATA, 0, static_cast<gsize>(-1));
    GstReferenceTimestampMeta* meta =
        copied ? gst_buffer_get_reference_timestamp_meta(copy, reference) : nullptr;
    const bool valid =
        meta && GST_BUFFER_PTS(copy) == 100 * GST_MSECOND &&
        GST_BUFFER_DTS(copy) == 90 * GST_MSECOND && GST_BUFFER_DURATION(copy) == 33 * GST_MSECOND &&
        GST_BUFFER_OFFSET(copy) == 42 && GST_BUFFER_OFFSET_END(copy) == 43 &&
        GST_BUFFER_FLAG_IS_SET(copy, GST_BUFFER_FLAG_DISCONT) && gst_buffer_n_memory(copy) == 0 &&
        meta->timestamp == 500 * GST_MSECOND && meta->duration == 33 * GST_MSECOND;
    if (!valid)
    {
        gst_buffer_unref(copy);
        return nullptr;
    }
    return copy; // Full reference owned by the caller.
}

/** @brief Prints the verified timing and metadata fields. */
static void print_metadata(GstBuffer* buffer, GstCaps* reference)
{
    const GstReferenceTimestampMeta* meta =
        gst_buffer_get_reference_timestamp_meta(buffer, reference);
    std::cout << "PTS              : " << GST_BUFFER_PTS(buffer) << '\n';
    std::cout << "DTS              : " << GST_BUFFER_DTS(buffer) << '\n';
    std::cout << "Duration         : " << GST_BUFFER_DURATION(buffer) << '\n';
    std::cout << "Offset           : " << GST_BUFFER_OFFSET(buffer) << '\n';
    std::cout << "DISCONT flag     : "
              << (GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DISCONT) ? "yes" : "no") << '\n';
    std::cout << "Reference TS     : " << meta->timestamp << '\n';
    std::cout << "Copied memories  : " << gst_buffer_n_memory(buffer) << '\n';
}

int main(int argc, char* argv[])
{
    GError* error = nullptr;

    if (gst_init_check(&argc, &argv, &error) == FALSE)
    {
        if (error != nullptr)
        {
            g_error_free(error);
        }

        return 1;
    }

    GstBuffer* buffer = gst_buffer_new_allocate(nullptr, 64, nullptr);

    if (buffer == nullptr)
    {
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

    if (reference == nullptr)
    {
        std::cerr << "Failed to create timestamp reference caps\n";
        gst_buffer_unref(buffer);
        return 1;
    }

    GstReferenceTimestampMeta* added_meta = gst_buffer_add_reference_timestamp_meta(
        buffer, reference, 500 * GST_MSECOND, 33 * GST_MSECOND);

    if (added_meta == nullptr)
    {
        std::cerr << "Failed to add reference timestamp metadata\n";
        gst_caps_unref(reference);
        gst_buffer_unref(buffer);
        return 1;
    }

    GstBuffer* metadata_copy = copy_verified_metadata(buffer, reference);

    if (metadata_copy == nullptr)
    {
        std::cerr << "Copied metadata differs from the source\n";
        gst_caps_unref(reference);
        gst_buffer_unref(buffer);
        return 1;
    }

    print_metadata(metadata_copy, reference);

    gst_buffer_unref(metadata_copy);
    gst_caps_unref(reference);
    gst_buffer_unref(buffer);

    std::cout << "Buffer metadata: PASS\n";
    return 0;
}
