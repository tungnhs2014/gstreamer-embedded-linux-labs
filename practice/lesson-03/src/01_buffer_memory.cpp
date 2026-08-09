#include <gst/gst.h>

#include <iostream>

/**
 * @brief Prints the visible and allocated sizes of a GstMemory block.
 *
 * @param label Text used to identify the memory in the output.
 * @param memory Valid GstMemory pointer. Ownership is not transferred.
 */
static void print_memory_info(const char* label, GstMemory* memory)
{
    gsize offset = 0;
    gsize max_size = 0;
    const gsize size = gst_memory_get_sizes(memory, &offset, &max_size);

    std::cout << label << '\n';
    std::cout << "  size     : " << size << '\n';
    std::cout << "  offset   : " << offset << '\n';
    std::cout << "  max_size : " << max_size << '\n';
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

    GstBuffer* buffer = gst_buffer_new();

    if (buffer == nullptr) {
        std::cerr << "Failed to create GstBuffer\n";
        return 1;
    }

    std::cout << "Initial memory blocks: " << gst_buffer_n_memory(buffer) << '\n';

    GstMemory* memory = gst_allocator_alloc(nullptr, 64, nullptr);

    if (memory == nullptr) {
        std::cerr << "Failed to allocate GstMemory\n";
        gst_buffer_unref(buffer);
        return 1;
    }

    print_memory_info("Allocated memory", memory);

    // Ownership of memory is transferred to buffer.
    gst_buffer_append_memory(buffer, memory);

    std::cout << "Memory blocks after append: " << gst_buffer_n_memory(buffer) << "\n\n";

    // Borrowed pointer. Do not unref.
    GstMemory* borrowed_memory = gst_buffer_peek_memory(buffer, 0);

    if (borrowed_memory == nullptr) {
        std::cerr << "Failed to peek memory\n";
        gst_buffer_unref(buffer);
        return 1;
    }

    print_memory_info("Borrowed memory", borrowed_memory);

    // Full reference. Caller must unref.
    GstMemory* owned_memory = gst_buffer_get_memory(buffer, 0);

    if (owned_memory == nullptr) {
        std::cerr << "Failed to get memory reference\n";
        gst_buffer_unref(buffer);
        return 1;
    }

    print_memory_info("Owned memory reference", owned_memory);

    gst_memory_unref(owned_memory);
    gst_buffer_unref(buffer);

    std::cout << "Buffer and memory ownership: PASS\n";
    return 0;
}