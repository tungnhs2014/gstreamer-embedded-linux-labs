#include <gst/gst.h>

#include <algorithm>
#include <array>
#include <iostream>

/**
 * @brief Prints YUY2 pixel-pair components from a mapped payload.
 *
 * Every four bytes represent two horizontal pixels:
 * Y0 Cb Y1 Cr.
 *
 * @param data YUY2 payload.
 * @param size Payload size in bytes.
 * @return true if the payload contains complete pixel pairs.
 */
static bool print_yuy2_pairs(const guint8* data, gsize size)
{
    if (data == nullptr || size % 4 != 0) {
        return false;
    }

    for (gsize offset = 0; offset < size; offset += 4) {
        const guint8 y0 = data[offset];
        const guint8 cb = data[offset + 1];
        const guint8 y1 = data[offset + 2];
        const guint8 cr = data[offset + 3];

        std::cout << "Pixels " << offset / 2 << "-" << offset / 2 + 1
                  << ": Y0=" << static_cast<unsigned>(y0)
                  << " Cb=" << static_cast<unsigned>(cb)
                  << " Y1=" << static_cast<unsigned>(y1)
                  << " Cr=" << static_cast<unsigned>(cr)
                  << '\n';
    }

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

    constexpr std::array<guint8, 8> yuy2_data{
        10, 100, 20, 150,
        30, 110, 40, 160
    };

    GstBuffer* buffer = gst_buffer_new_allocate(nullptr, yuy2_data.size(), nullptr);

    if (buffer == nullptr) {
        std::cerr << "Failed to allocate buffer\n";
        return 1;
    }

    GstMapInfo map = GST_MAP_INFO_INIT;

    if (gst_buffer_map(buffer, &map, GST_MAP_WRITE) == FALSE) {
        std::cerr << "Failed to map buffer for writing\n";
        gst_buffer_unref(buffer);
        return 1;
    }

    if (map.size < yuy2_data.size()) {
        std::cerr << "Mapped buffer is too small\n";
        gst_buffer_unmap(buffer, &map);
        gst_buffer_unref(buffer);
        return 1;
    }

    std::copy(yuy2_data.begin(), yuy2_data.end(), map.data);
    gst_buffer_unmap(buffer, &map);

    if (gst_buffer_map(buffer, &map, GST_MAP_READ) == FALSE) {
        std::cerr << "Failed to map buffer for reading\n";
        gst_buffer_unref(buffer);
        return 1;
    }

    const bool valid = map.size == yuy2_data.size() &&
        std::equal(yuy2_data.begin(), yuy2_data.end(), map.data) &&
        print_yuy2_pairs(map.data, map.size);

    gst_buffer_unmap(buffer, &map);
    gst_buffer_unref(buffer);

    if (!valid) {
        std::cerr << "Invalid YUY2 payload\n";
        return 1;
    }

    std::cout << "YUY2 byte layout: PASS\n";
    return 0;
}
