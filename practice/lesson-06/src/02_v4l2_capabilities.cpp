#include <linux/videodev2.h>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <sys/ioctl.h>
#include <unistd.h>

/** @brief Converts a V4L2 fourcc value to the four printable format bytes. */
static void print_fourcc(__u32 value)
{
    for (int shift = 0; shift < 32; shift += 8)
        std::cout << static_cast<char>((value >> shift) & 0xff);
}

/** @brief Prints every advertised interval for one format and size; false means incomplete
 * inspection. */
static bool print_intervals(int fd, __u32 pixel_format, __u32 width, __u32 height)
{
    constexpr __u32 max_intervals = 16;
    std::cout << "  " << width << 'x' << height << " intervals:";
    for (__u32 index = 0; index <= max_intervals; ++index)
    {
        v4l2_frmivalenum interval{};
        interval.index = index;
        interval.pixel_format = pixel_format;
        interval.width = width;
        interval.height = height;
        if (ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &interval) < 0)
        {
            if (errno == EINVAL && index > 0)
            {
                std::cout << '\n';
                return true;
            }
            std::cout << " unavailable\n";
            std::cerr << "VIDIOC_ENUM_FRAMEINTERVALS at index " << index << ": "
                      << std::strerror(errno) << '\n';
            return false;
        }
        if (index == max_intervals)
        {
            std::cout << '\n';
            std::cerr << "Frame interval listing exceeds " << max_intervals << " entries\n";
            return false;
        }
        if (interval.type == V4L2_FRMIVAL_TYPE_DISCRETE)
        {
            std::cout << ' ' << interval.discrete.numerator << '/' << interval.discrete.denominator
                      << 's';
            continue;
        }
        if (interval.type == V4L2_FRMIVAL_TYPE_STEPWISE ||
            interval.type == V4L2_FRMIVAL_TYPE_CONTINUOUS)
        {
            if (index != 0)
            {
                std::cerr << "Ranged frame intervals were not returned at index zero\n";
                return false;
            }
            std::cout << " range " << interval.stepwise.min.numerator << '/'
                      << interval.stepwise.min.denominator << " to "
                      << interval.stepwise.max.numerator << '/' << interval.stepwise.max.denominator
                      << 's';
            if (interval.type == V4L2_FRMIVAL_TYPE_STEPWISE)
                std::cout << " (step " << interval.stepwise.step.numerator << '/'
                          << interval.stepwise.step.denominator << "s)";
            std::cout << '\n';
            return true;
        }
        std::cerr << "Unknown frame interval type: " << interval.type << '\n';
        return false;
    }
    return false;
}

/** @brief Prints every advertised size for one format and checks its intervals. */
static bool print_sizes(int fd, __u32 pixel_format)
{
    constexpr __u32 max_sizes = 32;
    for (__u32 index = 0; index <= max_sizes; ++index)
    {
        v4l2_frmsizeenum size{};
        size.pixel_format = pixel_format;
        size.index = index;
        if (ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &size) < 0)
        {
            if (errno == EINVAL && index > 0)
                return true;
            std::cerr << "VIDIOC_ENUM_FRAMESIZES at index " << index << ": " << std::strerror(errno)
                      << '\n';
            return false;
        }
        if (index == max_sizes)
        {
            std::cerr << "Frame size listing exceeds " << max_sizes << " entries\n";
            return false;
        }
        if (size.type == V4L2_FRMSIZE_TYPE_DISCRETE)
        {
            if (!print_intervals(fd, pixel_format, size.discrete.width, size.discrete.height))
                return false;
            continue;
        }
        if (size.type != V4L2_FRMSIZE_TYPE_STEPWISE && size.type != V4L2_FRMSIZE_TYPE_CONTINUOUS)
        {
            std::cerr << "Unknown frame size type: " << size.type << '\n';
            return false;
        }
        if (index != 0)
        {
            std::cerr << "Ranged frame sizes were not returned at index zero\n";
            return false;
        }
        std::cout << "  Size range " << size.stepwise.min_width << 'x' << size.stepwise.min_height
                  << " to " << size.stepwise.max_width << 'x' << size.stepwise.max_height;
        if (size.type == V4L2_FRMSIZE_TYPE_STEPWISE)
            std::cout << " (step " << size.stepwise.step_width << 'x' << size.stepwise.step_height
                      << ')';
        std::cout << '\n';
        return print_intervals(fd, pixel_format, size.stepwise.min_width, size.stepwise.min_height);
    }
    return false;
}

/** @brief Prints all formats and requires a complete size/interval listing for each. */
static bool print_formats(int fd, __u32 type)
{
    constexpr __u32 max_formats = 64;
    for (__u32 index = 0; index <= max_formats; ++index)
    {
        v4l2_fmtdesc format{};
        format.type = type;
        format.index = index;
        if (ioctl(fd, VIDIOC_ENUM_FMT, &format) < 0)
        {
            if (errno == EINVAL && index > 0)
                return true;
            std::cerr << "VIDIOC_ENUM_FMT at index " << index << ": " << std::strerror(errno)
                      << '\n';
            return false;
        }
        if (index == max_formats)
        {
            std::cerr << "Pixel format listing exceeds " << max_formats << " entries\n";
            return false;
        }
        std::cout << "Format ";
        print_fourcc(format.pixelformat);
        std::cout << ": " << format.description << '\n';
        if (!print_sizes(fd, format.pixelformat))
            return false;
    }
    return false;
}

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        std::cerr << "Usage: " << argv[0] << " /dev/videoX\n";
        return 2;
    }

    const int fd = open(argv[1], O_RDONLY | O_NONBLOCK);
    if (fd < 0)
    {
        std::cerr << "Cannot open " << argv[1] << ": " << std::strerror(errno) << '\n';
        return 1;
    }

    v4l2_capability capability{};
    if (ioctl(fd, VIDIOC_QUERYCAP, &capability) < 0)
    {
        std::cerr << "VIDIOC_QUERYCAP failed: " << std::strerror(errno) << '\n';
        close(fd);
        return 1;
    }

    const __u32 caps = (capability.capabilities & V4L2_CAP_DEVICE_CAPS) ? capability.device_caps
                                                                        : capability.capabilities;
    const __u32 type =
        (caps & V4L2_CAP_VIDEO_CAPTURE)
            ? V4L2_BUF_TYPE_VIDEO_CAPTURE
            : ((caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE) ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : 0);
    if (type == 0)
    {
        std::cerr << "Node is not a video capture device\n";
        close(fd);
        return 1;
    }

    std::cout << "Driver: " << capability.driver << "\nCard: " << capability.card << '\n';
    const bool inspection_ok = print_formats(fd, type);
    close(fd);
    if (!inspection_ok)
        return 1;
    std::cout << "V4L2 capability inspection: PASS\n";
    return 0;
}
