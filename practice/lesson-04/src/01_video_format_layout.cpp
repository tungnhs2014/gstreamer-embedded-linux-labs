#include <gst/gst.h>
#include <gst/video/video.h>

#include <iostream>

/**
 * @brief Prints the default GStreamer memory layout for one raw video format.
 *
 * @param format Raw video format.
 * @param width Frame width in pixels.
 * @param height Frame height in pixels.
 * @return true if the video layout is valid.
 */
static bool print_video_layout(GstVideoFormat format, guint width, guint height)
{
    GstVideoInfo info{};

    if (gst_video_info_set_format(&info, format, width, height) == FALSE) {
        std::cerr << "Failed to create video layout\n";
        return false;
    }

    std::cout << "Format : " << GST_VIDEO_INFO_NAME(&info) << '\n';
    std::cout << "Size   : " << GST_VIDEO_INFO_SIZE(&info) << " bytes\n";
    std::cout << "Planes : " << GST_VIDEO_INFO_N_PLANES(&info) << '\n';

    for (guint plane = 0; plane < GST_VIDEO_INFO_N_PLANES(&info); ++plane) {
        std::cout << "  Plane " << plane
                  << " offset=" << GST_VIDEO_INFO_PLANE_OFFSET(&info, plane)
                  << " stride=" << GST_VIDEO_INFO_PLANE_STRIDE(&info, plane)
                  << '\n';
    }

    std::cout << '\n';
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

    constexpr guint width = 640;
    constexpr guint height = 480;

    if (!print_video_layout(GST_VIDEO_FORMAT_YUY2, width, height) ||
        !print_video_layout(GST_VIDEO_FORMAT_I420, width, height) ||
        !print_video_layout(GST_VIDEO_FORMAT_NV12, width, height)) {
        return 1;
    }

    std::cout << "Raw video layout comparison: PASS\n";
    return 0;
}