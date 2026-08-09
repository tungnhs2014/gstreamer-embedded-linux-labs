#include <gst/gst.h>

#include <iostream>

/**
 * @brief Converts a pad direction to a readable string.
 *
 * @param direction GStreamer pad direction.
 * @return Constant string describing the direction.
 */
static const char* pad_direction_to_string(GstPadDirection direction)
{
    switch (direction) {
    case GST_PAD_SRC:
        return "SRC";
    case GST_PAD_SINK:
        return "SINK";
    default:
        return "UNKNOWN";
    }
}

/**
 * @brief Converts pad presence to a readable string.
 *
 * @param presence GStreamer pad presence type.
 * @return Constant string describing the presence type.
 */
static const char* pad_presence_to_string(GstPadPresence presence)
{
    switch (presence) {
    case GST_PAD_ALWAYS:
        return "ALWAYS";
    case GST_PAD_SOMETIMES:
        return "SOMETIMES";
    case GST_PAD_REQUEST:
        return "REQUEST";
    default:
        return "UNKNOWN";
    }
}

/**
 * @brief Inspects an existing static pad and the template that created it.
 *
 * @param element Element that owns the pad.
 * @param pad_name Name of the static pad to inspect.
 * @return true when the pad and its template are valid.
 */
static bool inspect_static_pad(GstElement* element, const char* pad_name)
{
    // Full reference returned to the caller.
    GstPad* pad = gst_element_get_static_pad(element, pad_name);

    if (pad == nullptr) {
        std::cerr << "Static pad '" << pad_name << "' was not found\n";
        return false;
    }

    // Full reference to the template from which this actual pad was created.
    GstPadTemplate* pad_template = gst_pad_get_pad_template(pad);

    if (pad_template == nullptr) {
        std::cerr << "Pad '" << pad_name << "' has no pad template\n";
        gst_object_unref(pad);
        return false;
    }

    // Full reference to the capabilities declared by the pad template.
    GstCaps* template_caps = gst_pad_template_get_caps(pad_template);

    if (template_caps == nullptr) {
        std::cerr << "Failed to get template caps for pad '" << pad_name << "'\n";
        gst_object_unref(pad_template);
        gst_object_unref(pad);
        return false;
    }

    std::cout << "Pad name      : " << GST_PAD_NAME(pad) << '\n';
    std::cout << "Pad direction : " << pad_direction_to_string(gst_pad_get_direction(pad)) << '\n';
    std::cout << "Linked        : " << (gst_pad_is_linked(pad) ? "yes" : "no") << '\n';

    std::cout << "Template name : " << GST_PAD_TEMPLATE_NAME_TEMPLATE(pad_template) << '\n';
    std::cout << "Presence      : " << pad_presence_to_string(GST_PAD_TEMPLATE_PRESENCE(pad_template)) << '\n';
    std::cout << "Template caps : " << (gst_caps_is_any(template_caps) ? "ANY" : "specific caps") << "\n\n";

    gst_caps_unref(template_caps);
    gst_object_unref(pad_template);
    gst_object_unref(pad);

    return true;
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

    GstElement* identity = gst_element_factory_make("identity", "identity-0");

    if (identity == nullptr) {
        std::cerr << "Failed to create identity element\n";
        return 1;
    }

    const bool sink_ok = inspect_static_pad(identity, "sink");
    const bool src_ok = inspect_static_pad(identity, "src");

    gst_object_unref(identity);

    if (!sink_ok || !src_ok) {
        return 1;
    }

    std::cout << "Pad inspection: PASS\n";
    return 0;
}