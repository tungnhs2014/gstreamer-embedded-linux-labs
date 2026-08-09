#include <gst/gst.h>

#include <iostream>

/**
 * @brief Prints the plugin and factory metadata for an element factory.
 *
 * @param plugin Plugin that provides the factory.
 * @param factory Element factory registered by the plugin.
 */
static void print_factory_info(GstPlugin* plugin, GstElementFactory* factory)
{
    const gchar* plugin_filename = gst_plugin_get_filename(plugin);
    const gchar* long_name = gst_element_factory_get_metadata(factory, GST_ELEMENT_METADATA_LONGNAME);

    std::cout << "Plugin name   : " << gst_plugin_get_name(plugin) << '\n';
    std::cout << "Plugin version: " << gst_plugin_get_version(plugin) << '\n';
    std::cout << "Plugin file   : " << (plugin_filename != nullptr ? plugin_filename : "unknown") << '\n';
    std::cout << "Factory name  : " << gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory)) << '\n';
    std::cout << "Long name     : " << (long_name != nullptr ? long_name : "unknown") << '\n';
}

int main(int argc, char* argv[])
{
    GError* error = nullptr;

    if (gst_init_check(&argc, &argv, &error) == FALSE) {
        std::cerr << "Failed to initialize GStreamer";

        if (error != nullptr) {
            std::cerr << ": " << error->message;
            g_error_free(error);
        }

        std::cerr << '\n';
        return 1;
    }

    // Borrowed singleton reference. Do not unref.
    GstRegistry* registry = gst_registry_get();

    // Full reference. Must be unreffed after use.
    GstPluginFeature* feature = gst_registry_find_feature(
        registry, "videotestsrc", GST_TYPE_ELEMENT_FACTORY);

    if (feature == nullptr) {
        std::cerr << "Factory 'videotestsrc' was not found in the registry\n";
        return 1;
    }

    // Type-safe view of the same GstPluginFeature object. No new reference.
    GstElementFactory* factory = GST_ELEMENT_FACTORY(feature);

    // Full reference to the plugin that provides this feature.
    GstPlugin* plugin = gst_plugin_feature_get_plugin(feature);

    if (plugin == nullptr) {
        std::cerr << "Factory is not associated with a plugin\n";
        gst_object_unref(feature);
        return 1;
    }

    print_factory_info(plugin, factory);

    // Create an actual GstElement instance from the factory.
    GstElement* source = gst_element_factory_create(factory, "test-source");

    if (source == nullptr) {
        std::cerr << "Failed to create element instance\n";
        gst_object_unref(plugin);
        gst_object_unref(feature);
        return 1;
    }

    std::cout << "Instance name : " << GST_ELEMENT_NAME(source) << '\n';

    gst_object_unref(source);
    gst_object_unref(plugin);
    gst_object_unref(feature);

    return 0;
}