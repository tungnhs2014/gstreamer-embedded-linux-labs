#include <gst/gst.h>

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

    std::cout << "Writable while retained: " << (gst_buffer_is_writable(source) ? "yes" : "no") << '\n';

    gst_buffer_unref(retained);

    GstBuffer* shallow_copy = gst_buffer_copy(source);
    GstBuffer* deep_copy = gst_buffer_copy_deep(source);

    if (shallow_copy == nullptr || deep_copy == nullptr) {
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

    std::cout << "Shallow copy shares memory: " << (source_memory == shallow_memory ? "yes" : "no") << '\n';
    std::cout << "Deep copy shares memory   : " << (source_memory == deep_memory ? "yes" : "no") << '\n';

    gst_buffer_unref(deep_copy);
    gst_buffer_unref(shallow_copy);
    gst_buffer_unref(source);

    std::cout << "Buffer copy semantics: PASS\n";
    return 0;
}