#include <gst/gst.h>

#include <cstring>
#include <iostream>

/**
 * @brief RAII wrapper for a GstBuffer mapping.
 *
 * This class does not own the GstBuffer. The buffer must remain valid for the
 * complete lifetime of this object.
 */
class BufferMap final {
  public:
    BufferMap(GstBuffer* buffer, GstMapFlags flags) : buffer(buffer), flags(flags)
    {
        if (buffer != nullptr) {
            mapped = gst_buffer_map(buffer, &map, flags) != FALSE;
        }
    }

    ~BufferMap() noexcept
    {
        if (mapped) {
            gst_buffer_unmap(buffer, &map);
        }
    }

    BufferMap(const BufferMap&) = delete;
    BufferMap& operator=(const BufferMap&) = delete;
    BufferMap(BufferMap&&) = delete;
    BufferMap& operator=(BufferMap&&) = delete;

    [[nodiscard]] bool valid() const noexcept
    {
        return mapped;
    }

    [[nodiscard]] const guint8* data() const noexcept
    {
        return mapped ? map.data : nullptr;
    }

    [[nodiscard]] gsize size() const noexcept
    {
        return mapped ? map.size : 0;
    }

  private:
    GstBuffer* buffer = nullptr;
    GstMapInfo map = GST_MAP_INFO_INIT;
    GstMapFlags flags = GST_MAP_READ;
    bool mapped = false;
};

/**
 * @brief Inspects a borrowed buffer passing through a source pad.
 */
static GstPadProbeReturn on_buffer_probe(GstPad*, GstPadProbeInfo* info, gpointer)
{
    GstBuffer* buffer = gst_pad_probe_info_get_buffer(info);

    if (buffer == nullptr) {
        return GST_PAD_PROBE_OK;
    }

    // Borrowed buffer: inspect it, but do not unref it.
    BufferMap mapped(buffer, GST_MAP_READ);

    if (!mapped.valid()) {
        std::cerr << "Failed to map probed buffer\n";
        return GST_PAD_PROBE_OK;
    }

    std::cout << "Probe buffer size: " << mapped.size() << '\n';

    if (mapped.size() > 0) {
        std::cout << "Probe first byte : 0x" << std::hex
                  << static_cast<unsigned>(mapped.data()[0]) << std::dec << '\n';
    }

    return GST_PAD_PROBE_OK;
}

/**
 * @brief Fills a buffer before transferring it to gst_pad_push().
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

    GstPad* src_pad = gst_pad_new("src", GST_PAD_SRC);

    if (src_pad == nullptr) {
        return 1;
    }

    if (gst_pad_set_active(src_pad, TRUE) == FALSE) {
        gst_object_unref(src_pad);
        return 1;
    }

    const gulong probe_id =
        gst_pad_add_probe(src_pad, GST_PAD_PROBE_TYPE_BUFFER, on_buffer_probe, nullptr, nullptr);

    if (probe_id == 0) {
        std::cerr << "Failed to install pad probe\n";
        gst_pad_set_active(src_pad, FALSE);
        gst_object_unref(src_pad);
        return 1;
    }

    GstBuffer* buffer = gst_buffer_new_allocate(nullptr, 16, nullptr);

    if (buffer == nullptr || !fill_buffer(buffer, 0x7E)) {
        std::cerr << "Failed to prepare buffer\n";

        if (buffer != nullptr) {
            gst_buffer_unref(buffer);
        }

        gst_pad_remove_probe(src_pad, probe_id);
        gst_pad_set_active(src_pad, FALSE);
        gst_object_unref(src_pad);
        return 1;
    }

    const GstFlowReturn result = gst_pad_push(src_pad, buffer);

    gst_pad_remove_probe(src_pad, probe_id);
    gst_pad_set_active(src_pad, FALSE);
    gst_object_unref(src_pad);

    if (result != GST_FLOW_NOT_LINKED) {
        std::cerr << "Expected controlled NOT_LINKED result\n";
        return 1;
    }

    std::cout << "Pad probe borrowed-buffer handling: PASS\n";
    return 0;
}