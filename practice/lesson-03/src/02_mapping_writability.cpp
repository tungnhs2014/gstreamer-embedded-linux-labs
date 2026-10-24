#include <gst/gst.h>

#include <iostream>

/**
 * @brief Reads the first payload byte from a buffer.
 *
 * @param buffer Valid buffer. Ownership is not transferred.
 * @param value Receives the first byte.
 * @return true on success.
 */
static bool read_first_byte(GstBuffer* buffer, guint8& value)
{
    GstMapInfo map = GST_MAP_INFO_INIT;

    if (gst_buffer_map(buffer, &map, GST_MAP_READ) == FALSE) {
        return false;
    }

    if (map.size == 0) {
        gst_buffer_unmap(buffer, &map);
        return false;
    }

    value = map.data[0];
    gst_buffer_unmap(buffer, &map);

    return true;
}

/**
 * @brief Makes an owned buffer writable and updates its first payload byte.
 *
 * @param buffer Owned buffer reference. The pointer may change.
 * @param value New first-byte value.
 * @return true on success.
 */
static bool write_first_byte(GstBuffer*& buffer, guint8 value)
{
    if (buffer == nullptr) {
        return false;
    }

    buffer = gst_buffer_make_writable(buffer);

    if (buffer == nullptr || !gst_buffer_is_writable(buffer)) {
        return false;
    }

    GstMapInfo map = GST_MAP_INFO_INIT;

    if (gst_buffer_map(buffer, &map, GST_MAP_WRITE) == FALSE) {
        return false;
    }

    if (map.size == 0) {
        gst_buffer_unmap(buffer, &map);
        return false;
    }

    map.data[0] = value;
    gst_buffer_unmap(buffer, &map);

    return true;
}

int main(int argc, char* argv[])
{
    GError* error = nullptr;

    if (gst_init_check(&argc, &argv, &error) == FALSE) {
        std::cerr << "Failed to initialize GStreamer\n";

        if (error != nullptr) {
            g_error_free(error);
        }

        return 1;
    }

    GstBuffer* buffer = gst_buffer_new_allocate(nullptr, 16, nullptr);

    if (buffer == nullptr) {
        std::cerr << "Failed to allocate buffer\n";
        return 1;
    }

    if (!write_first_byte(buffer, 0x11)) {
        std::cerr << "Failed to initialize payload\n";
        gst_buffer_unref(buffer);
        return 1;
    }

    GstBuffer* shared_buffer = gst_buffer_ref(buffer);
    const bool shared_is_not_writable = !gst_buffer_is_writable(buffer);

    std::cout << "Writable with two references: "
              << (gst_buffer_is_writable(buffer) ? "yes" : "no") << '\n';

    if (!shared_is_not_writable || !write_first_byte(buffer, 0xAA)) {
        std::cerr << "Failed to modify writable buffer\n";
        gst_buffer_unref(shared_buffer);

        if (buffer != nullptr) {
            gst_buffer_unref(buffer);
        }

        return 1;
    }

    guint8 modified_value = 0;
    guint8 shared_value = 0;

    if (!read_first_byte(buffer, modified_value) || !read_first_byte(shared_buffer, shared_value)) {
        std::cerr << "Failed to read buffers\n";
        gst_buffer_unref(shared_buffer);
        gst_buffer_unref(buffer);
        return 1;
    }

    std::cout << "Modified buffer first byte: 0x" << std::hex << static_cast<unsigned>(modified_value) << '\n';
    std::cout << "Shared buffer first byte  : 0x" << std::hex << static_cast<unsigned>(shared_value) << '\n';

    gst_buffer_unref(shared_buffer);
    gst_buffer_unref(buffer);

    if (modified_value != 0xAA || shared_value != 0x11) {
        std::cerr << "Writability isolation check failed\n";
        return 1;
    }

    std::cout << std::dec << "Mapping and writability: PASS\n";
    return 0;
}
