#include <gst/gst.h>

#include <algorithm>
#include <cstring>
#include <iostream>

/**
 * @brief Fills the complete payload with one byte value.
 */
static bool fill_buffer(GstBuffer* buffer, guint8 value)
{
    GstMapInfo map = GST_MAP_INFO_INIT;

    if (gst_buffer_map(buffer, &map, GST_MAP_WRITE) == FALSE) {
        return false;
    }

    std::memset(map.data, value, map.size);
    gst_buffer_unmap(buffer, &map);

    return true;
}

/** @brief Checks all bytes without changing the buffer or its memory. */
static bool buffer_matches(GstBuffer* buffer, guint8 value)
{
    GstMapInfo map = GST_MAP_INFO_INIT;

    if (gst_buffer_map(buffer, &map, GST_MAP_READ) == FALSE) {
        return false;
    }

    const bool valid = map.size == 32 &&
        std::all_of(map.data, map.data + map.size, [value](guint8 byte) { return byte == value; });

    gst_buffer_unmap(buffer, &map);
    return valid;
}

int main(int argc, char* argv[])
{
    GError* error = nullptr;

    if (gst_init_check(&argc, &argv, &error) == FALSE) {
        if (error != nullptr) {
            g_error_free(error);
        }

        return 1;
    }

    GstBuffer* source = gst_buffer_new_allocate(nullptr, 32, nullptr);

    if (source == nullptr || !fill_buffer(source, 0x5A)) {
        std::cerr << "Failed to create source buffer\n";

        if (source != nullptr) {
            gst_buffer_unref(source);
        }

        return 1;
    }

    GstBuffer* retained = gst_buffer_ref(source);

    const bool same_object = retained == source;
    const bool shared_ref_not_writable = !gst_buffer_is_writable(source);
    std::cout << "Writable while retained: " << (shared_ref_not_writable ? "no" : "yes") << '\n';

    gst_buffer_unref(retained);

    GstBuffer* shallow_copy = gst_buffer_copy(source);
    GstBuffer* deep_copy = gst_buffer_copy_deep(source);

    if (shallow_copy == nullptr || deep_copy == nullptr || !same_object || !shared_ref_not_writable) {
        std::cerr << "Failed to copy buffer\n";

        if (shallow_copy != nullptr) {
            gst_buffer_unref(shallow_copy);
        }

        if (deep_copy != nullptr) {
            gst_buffer_unref(deep_copy);
        }

        gst_buffer_unref(source);
        return 1;
    }

    GstMemory* source_memory = gst_buffer_peek_memory(source, 0);
    GstMemory* shallow_memory = gst_buffer_peek_memory(shallow_copy, 0);
    GstMemory* deep_memory = gst_buffer_peek_memory(deep_copy, 0);

    if (source_memory == nullptr || shallow_memory == nullptr || deep_memory == nullptr) {
        std::cerr << "Failed to inspect buffer memory\n";
        gst_buffer_unref(deep_copy);
        gst_buffer_unref(shallow_copy);
        gst_buffer_unref(source);
        return 1;
    }

    // Sharing is permitted for a shallow copy, but not required of every allocator.
    std::cout << "Shallow copy shares memory: " << (source_memory == shallow_memory ? "yes" : "no") << '\n';
    std::cout << "Deep copy shares memory   : " << (source_memory == deep_memory ? "yes" : "no") << '\n';

    const bool initial_contents_match = source != shallow_copy && source != deep_copy &&
        buffer_matches(source, 0x5A) && buffer_matches(shallow_copy, 0x5A) &&
        buffer_matches(deep_copy, 0x5A);

    const bool modified = initial_contents_match && fill_buffer(shallow_copy, 0xC3) &&
        fill_buffer(deep_copy, 0xA5);
    const bool isolated = modified && buffer_matches(source, 0x5A) &&
        buffer_matches(shallow_copy, 0xC3) && buffer_matches(deep_copy, 0xA5);

    gst_buffer_unref(deep_copy);
    gst_buffer_unref(shallow_copy);
    gst_buffer_unref(source);

    if (!isolated) {
        std::cerr << "Buffer copy content or isolation check failed\n";
        return 1;
    }

    std::cout << "After writes: source=0x5a shallow=0xc3 deep=0xa5\n";
    std::cout << "Buffer copy semantics: PASS\n";
    return 0;
}
