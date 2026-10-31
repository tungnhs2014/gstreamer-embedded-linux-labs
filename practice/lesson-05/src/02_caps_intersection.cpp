#include <gst/gst.h>

#include <iostream>

int main(int argc, char* argv[])
{
    gst_init(&argc, &argv);

    GstCaps* first = gst_caps_from_string(
        "video/x-raw,format=(string){ I420, NV12 },width=(int)[ 320, 1280 ],height=480");
    GstCaps* second = gst_caps_from_string(
        "video/x-raw,format=(string){ NV12, I420 },width=(int)[ 640, 1920 ],height=480");
    GstCaps* audio = gst_caps_from_string("audio/x-raw,format=S16LE,rate=48000,channels=2");

    if (first == nullptr || second == nullptr || audio == nullptr)
    {
        std::cerr << "Failed to construct candidate caps\n";
        if (first)
            gst_caps_unref(first);
        if (second)
            gst_caps_unref(second);
        if (audio)
            gst_caps_unref(audio);
        return 1;
    }

    GstCaps* compatible = gst_caps_intersect(first, second);
    GstCaps* incompatible = gst_caps_intersect(first, audio);
    const bool intersection_ok = compatible != nullptr && incompatible != nullptr &&
                                 !gst_caps_is_empty(compatible) && !gst_caps_is_fixed(compatible) &&
                                 gst_caps_is_empty(incompatible);

    // gst_caps_fixate() consumes its argument. Keep compatible for the subset check.
    GstCaps* selected = intersection_ok ? gst_caps_fixate(gst_caps_copy(compatible)) : nullptr;
    const bool selected_ok = selected != nullptr && gst_caps_is_fixed(selected) &&
                             gst_caps_is_subset(selected, compatible);

    if (compatible != nullptr && selected != nullptr)
    {
        gchar* common_text = gst_caps_to_string(compatible);
        gchar* selected_text = gst_caps_to_string(selected);
        std::cout << "Compatible: " << common_text << '\n'
                  << "Fixed choice: " << selected_text << '\n'
                  << "Video/audio intersection: EMPTY\n";
        g_free(common_text);
        g_free(selected_text);
    }

    if (selected)
        gst_caps_unref(selected);
    if (compatible)
        gst_caps_unref(compatible);
    if (incompatible)
        gst_caps_unref(incompatible);
    gst_caps_unref(audio);
    gst_caps_unref(second);
    gst_caps_unref(first);

    if (!intersection_ok || !selected_ok)
        return 1;
    std::cout << "Intersection and fixation: PASS\n";
    return 0;
}
