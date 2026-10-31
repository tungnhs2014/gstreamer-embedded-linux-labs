#include <gst/gst.h>

#include <iostream>

/**
 * @brief Creates an element and adds it to a bin.
 *
 * Ownership:
 * - Before gst_bin_add(): this function owns the newly created element.
 * - After successful gst_bin_add(): the bin owns the element.
 *
 * @param bin Parent bin that will own the element.
 * @param factory_name Element factory name.
 * @param element_name Element instance name.
 * @return Borrowed pointer owned by @p bin, or nullptr on failure.
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

    // Keep an owned reference across gst_bin_add(), whose failure paths differ
    // in how they handle a floating reference.
    gst_object_ref_sink(element);

    if (gst_bin_add(bin, element) == FALSE)
    {
        std::cerr << "Failed to add element '" << element_name << "' to bin\n";
        gst_object_unref(element);
        return nullptr;
    }

    gst_object_unref(element);
    return element; // Borrowed from bin while the bin remains alive.
}

/**
 * @brief Verifies that an element belongs to the expected parent.
 *
 * @param element Child element to inspect.
 * @param expected_parent Expected parent object.
 * @return true if the actual parent matches the expected parent.
 */
static bool verify_parent(GstElement* element, GstObject* expected_parent)
{
    GstObject* parent = gst_object_get_parent(GST_OBJECT(element));

    if (parent == nullptr)
    {
        std::cerr << "Element '" << GST_ELEMENT_NAME(element) << "' has no parent\n";
        return false;
    }

    const bool matches = parent == expected_parent;

    std::cout << "Element        : " << GST_ELEMENT_NAME(element) << '\n';
    std::cout << "Parent         : " << GST_OBJECT_NAME(parent) << '\n';
    std::cout << "Parent matches : " << (matches ? "yes" : "no") << "\n\n";

    gst_object_unref(parent);

    return matches;
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

    GstElement* pipeline = gst_pipeline_new("lesson-02-pipeline");

    if (pipeline == nullptr)
    {
        std::cerr << "Failed to create pipeline\n";
        return 1;
    }

    std::cout << "Pipeline name  : " << GST_ELEMENT_NAME(pipeline) << '\n';
    std::cout << "Is GstElement  : " << (GST_IS_ELEMENT(pipeline) ? "yes" : "no") << '\n';
    std::cout << "Is GstBin      : " << (GST_IS_BIN(pipeline) ? "yes" : "no") << '\n';
    std::cout << "Is GstPipeline : " << (GST_IS_PIPELINE(pipeline) ? "yes" : "no") << "\n\n";

    GstElement* source = create_and_add_element(GST_BIN(pipeline), "videotestsrc", "test-source");

    if (source == nullptr)
    {
        gst_object_unref(pipeline);
        return 1;
    }

    if (!verify_parent(source, GST_OBJECT(pipeline)))
    {
        gst_object_unref(pipeline);
        return 1;
    }

    std::cout << "Pipeline owns the child element\n";

    // Releasing the pipeline also releases children owned by the pipeline.
    gst_object_unref(pipeline);

    return 0;
}
