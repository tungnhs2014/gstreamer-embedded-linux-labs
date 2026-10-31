#include <gst/gst.h>

#include <iostream>

int main(int argc, char* argv[])
{
    gst_init(&argc, &argv);

    GstCaps* fixed =
        gst_caps_from_string("video/x-raw,format=I420,width=640,height=480,framerate=30/1");
    GstCaps* possible = gst_caps_from_string(
        "video/x-raw,format=(string){ I420, NV12 },width=(int)[ 320, 1280 ],height=480");
    GstCaps* any = gst_caps_new_any();
    GstCaps* empty = gst_caps_new_empty();

    if (fixed == nullptr || possible == nullptr || any == nullptr || empty == nullptr)
    {
        std::cerr << "Failed to construct caps\n";
        if (fixed)
            gst_caps_unref(fixed);
        if (possible)
            gst_caps_unref(possible);
        if (any)
            gst_caps_unref(any);
        if (empty)
            gst_caps_unref(empty);
        return 1;
    }

    const GstStructure* structure = gst_caps_get_structure(fixed, 0); // Borrowed from fixed.
    gint width = 0;
    const bool valid = gst_caps_is_fixed(fixed) && !gst_caps_is_fixed(possible) &&
                       gst_caps_is_any(any) && gst_caps_is_empty(empty) &&
                       g_strcmp0(gst_structure_get_name(structure), "video/x-raw") == 0 &&
                       g_strcmp0(gst_structure_get_string(structure, "format"), "I420") == 0 &&
                       gst_structure_get_int(structure, "width", &width) && width == 640;

    gchar* possible_text = gst_caps_to_string(possible);
    std::cout << "Fixed: video/x-raw I420 640x480\n"
              << "Possible: " << possible_text << '\n'
              << "ANY accepts any type; EMPTY accepts none\n";
    g_free(possible_text);

    gst_caps_unref(empty);
    gst_caps_unref(any);
    gst_caps_unref(possible);
    gst_caps_unref(fixed);

    if (!valid)
        return 1;
    std::cout << "Caps fundamentals: PASS\n";
    return 0;
}
