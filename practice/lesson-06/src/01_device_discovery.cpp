#include <gst/gst.h>

#include <iostream>

int main(int argc, char* argv[])
{
    gst_init(&argc, &argv);
    GstDeviceMonitor* monitor = gst_device_monitor_new();
    if (monitor == nullptr)
        return 1;

    const guint filter_id = gst_device_monitor_add_filter(monitor, "Video/Source", nullptr);
    if (filter_id == 0 || !gst_device_monitor_start(monitor))
    {
        std::cerr << "No video-source device provider could be started\n";
        gst_object_unref(monitor);
        return 1;
    }

    // In 1.28.6 this waits for the asynchronous initial provider scan.
    GList* devices = gst_device_monitor_get_devices(monitor); // Full references.
    guint count = 0;
    for (GList* node = devices; node != nullptr; node = node->next)
    {
        auto* device = GST_DEVICE(node->data);
        gchar* name = gst_device_get_display_name(device);
        gchar* device_class = gst_device_get_device_class(device);
        GstCaps* caps = gst_device_get_caps(device);
        std::cout << "Device " << ++count << ": " << name << " [" << device_class << "]\n"
                  << "  Advertised caps: " << (caps ? gst_caps_get_size(caps) : 0)
                  << " structures\n";
        if (caps)
            gst_caps_unref(caps);
        g_free(device_class);
        g_free(name);
    }
    for (GList* node = devices; node != nullptr; node = node->next)
        gst_object_unref(node->data);
    g_list_free(devices);
    gst_device_monitor_stop(monitor);
    gst_object_unref(monitor);

    if (count == 0)
        std::cout << "No Video/Source device reported by GstDeviceMonitor\n";
    std::cout << "Discovery completed; capture usability is not yet proven: PASS\n";
    return 0;
}
