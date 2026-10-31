#include <gst/gst.h>

#include <iostream>

/**
 * @brief Creates an element and transfers its ownership to a bin.
 */
static GstElement* create_and_add_element(GstBin* bin, const char* factory_name,
                                          const char* element_name)
{
    GstElement* element = gst_element_factory_make(factory_name, element_name);

    if (element == nullptr)
    {
        std::cerr << "Failed to create element '" << element_name << "'\n";
        return nullptr;
    }

    gst_object_ref_sink(element);

    if (gst_bin_add(bin, element) == FALSE)
    {
        std::cerr << "Failed to add element '" << element_name << "'\n";
        gst_object_unref(element);
        return nullptr;
    }

    gst_object_unref(element);
    return element; // Borrowed from bin while the bin remains alive.
}

/**
 * @brief Exposes one child pad through a ghost pad on the bin.
 */
static bool add_ghost_pad(GstElement* bin, const char* ghost_name, GstElement* child,
                          const char* child_pad_name)
{
    GstPad* target_pad = gst_element_get_static_pad(child, child_pad_name);

    if (target_pad == nullptr)
    {
        std::cerr << "Failed to get target pad '" << child_pad_name << "'\n";
        return false;
    }

    GstPad* ghost_pad = gst_ghost_pad_new(ghost_name, target_pad);
    gst_object_unref(target_pad);

    if (ghost_pad == nullptr)
    {
        std::cerr << "Failed to create ghost pad '" << ghost_name << "'\n";
        return false;
    }

    // Keep our own reference because an add failure may consume a floating pad.
    gst_object_ref_sink(ghost_pad);

    if (gst_element_add_pad(bin, ghost_pad) == FALSE)
    {
        std::cerr << "Failed to add ghost pad '" << ghost_name << "'\n";
        gst_object_unref(ghost_pad);
        return false;
    }

    gst_object_unref(ghost_pad);
    return true;
}

/**
 * @brief Creates a reusable processing bin with sink and src ghost pads.
 *
 * @return New floating GstBin element, or nullptr on failure.
 */
static GstElement* create_processing_bin()
{
    GstElement* bin = gst_bin_new("processing-bin");

    if (bin == nullptr)
    {
        return nullptr;
    }

    GstElement* converter =
        create_and_add_element(GST_BIN(bin), "videoconvert", "internal-converter");

    if (converter == nullptr)
    {
        gst_object_unref(bin);
        return nullptr;
    }

    GstElement* identity = create_and_add_element(GST_BIN(bin), "identity", "internal-identity");

    if (identity == nullptr)
    {
        gst_object_unref(bin);
        return nullptr;
    }

    if (gst_element_link(converter, identity) == FALSE)
    {
        std::cerr << "Failed to link processing-bin internals\n";
        gst_object_unref(bin);
        return nullptr;
    }

    if (!add_ghost_pad(bin, "sink", converter, "sink") ||
        !add_ghost_pad(bin, "src", identity, "src"))
    {
        gst_object_unref(bin);
        return nullptr;
    }

    return bin;
}

/**
 * @brief Verifies that a bin pad is a ghost pad with a valid target.
 */
static bool verify_ghost_pad(GstElement* bin, const char* pad_name)
{
    GstPad* pad = gst_element_get_static_pad(bin, pad_name);

    if (pad == nullptr || !GST_IS_GHOST_PAD(pad))
    {
        std::cerr << "'" << pad_name << "' is not a valid ghost pad\n";

        if (pad != nullptr)
        {
            gst_object_unref(pad);
        }

        return false;
    }

    GstPad* target = gst_ghost_pad_get_target(GST_GHOST_PAD(pad));

    if (target == nullptr)
    {
        std::cerr << "Ghost pad '" << pad_name << "' has no target\n";
        gst_object_unref(pad);
        return false;
    }

    gchar* target_name = gst_object_get_name(GST_OBJECT(target));
    std::cout << "Ghost pad '" << pad_name << "' -> target '" << target_name << "'\n";
    g_free(target_name);

    gst_object_unref(target);
    gst_object_unref(pad);

    return true;
}

int main(int argc, char* argv[])
{
    GError* error = nullptr;

    if (gst_init_check(&argc, &argv, &error) == FALSE)
    {
        std::cerr << "Failed to initialize GStreamer";

        if (error != nullptr)
        {
            std::cerr << ": " << error->message;
            g_error_free(error);
        }

        std::cerr << '\n';
        return 1;
    }

    GstElement* pipeline = gst_pipeline_new("ghost-pad-pipeline");

    if (pipeline == nullptr)
    {
        std::cerr << "Failed to create pipeline\n";
        return 1;
    }

    GstElement* source = create_and_add_element(GST_BIN(pipeline), "videotestsrc", "test-source");
    GstElement* sink = create_and_add_element(GST_BIN(pipeline), "fakesink", "test-sink");
    GstElement* processing_bin = create_processing_bin();

    if (source == nullptr || sink == nullptr || processing_bin == nullptr)
    {
        if (processing_bin != nullptr)
        {
            gst_object_unref(processing_bin);
        }

        gst_object_unref(pipeline);
        return 1;
    }

    gst_object_ref_sink(processing_bin);

    if (gst_bin_add(GST_BIN(pipeline), processing_bin) == FALSE)
    {
        std::cerr << "Failed to add processing-bin to pipeline\n";
        gst_object_unref(processing_bin);
        gst_object_unref(pipeline);
        return 1;
    }

    gst_object_unref(processing_bin); // Pipeline now owns the bin.

    if (!verify_ghost_pad(processing_bin, "sink") || !verify_ghost_pad(processing_bin, "src"))
    {
        gst_object_unref(pipeline);
        return 1;
    }

    if (gst_element_link(source, processing_bin) == FALSE ||
        gst_element_link(processing_bin, sink) == FALSE)
    {
        std::cerr << "Failed to link through processing-bin\n";
        gst_object_unref(pipeline);
        return 1;
    }

    std::cout << "External graph linked through ghost pads\n";

    gst_object_unref(pipeline);

    std::cout << "Ghost pad topology: PASS\n";
    return 0;
}
