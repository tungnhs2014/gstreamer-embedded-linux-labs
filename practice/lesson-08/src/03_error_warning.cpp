#include <gst/gst.h>

#include <iostream>
#include <string>
#include <unistd.h>

/** @brief Requires a real resource error from filesrc for a missing file. */
static bool observe_missing_file_error(GstElement* pipeline, GstElement* file, GstBus* bus,
                                       const char* missing_path)
{
    g_object_set(file, "location", missing_path, nullptr);
    const GstStateChangeReturn started = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    std::cout << "Start result: " << gst_element_state_change_return_get_name(started) << '\n';
    GstMessage* message = gst_bus_timed_pop_filtered(
        bus, 5 * GST_SECOND, static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));
    bool resource_error = false;
    if (message && GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR)
    {
        GError* error = nullptr;
        gchar* debug = nullptr;
        gst_message_parse_error(message, &error, &debug);
        resource_error = error && error->domain == GST_RESOURCE_ERROR;
        std::cout << "Real filesrc ERROR: " << (error ? error->message : "unknown") << '\n'
                  << "Debug: " << (debug ? debug : "none") << '\n';
        g_clear_error(&error);
        g_free(debug);
    }
    if (message)
        gst_message_unref(message);
    return resource_error;
}

/** @brief Parses a locally constructed WARNING; no element posted this message. */
static bool parse_example_warning(GstElement* pipeline)
{
    GstMessage* warning = gst_message_new_warning(
        GST_OBJECT(pipeline),
        g_error_new_literal(GST_CORE_ERROR, GST_CORE_ERROR_FAILED, "synthetic parser example"),
        "local warning parsing exercise");
    if (!warning)
        return false;
    GError* parsed = nullptr;
    gchar* debug = nullptr;
    gst_message_parse_warning(warning, &parsed, &debug);
    const bool valid = parsed && g_strcmp0(parsed->message, "synthetic parser example") == 0;
    std::cout << "Locally constructed WARNING: " << (parsed ? parsed->message : "missing") << '\n';
    g_clear_error(&parsed);
    g_free(debug);
    gst_message_unref(warning);
    return valid;
}

int main(int argc, char* argv[])
{
    gst_init(&argc, &argv);
    GError* parse_error = nullptr;
    GstElement* pipeline = gst_parse_launch("filesrc name=file ! fakesink", &parse_error);
    if (parse_error || !pipeline)
    {
        if (parse_error)
        {
            std::cerr << parse_error->message << '\n';
            g_error_free(parse_error);
        }
        if (pipeline)
            gst_object_unref(pipeline);
        return 1;
    }
    GstElement* file = gst_bin_get_by_name(GST_BIN(pipeline), "file");
    GstBus* bus = gst_element_get_bus(pipeline);
    if (!file || !bus)
    {
        if (file)
            gst_object_unref(file);
        if (bus)
            gst_object_unref(bus);
        gst_object_unref(pipeline);
        return 1;
    }

    const std::string missing = "/tmp/gst-learning-labs-missing-" + std::to_string(getpid());
    if (access(missing.c_str(), F_OK) == 0)
    {
        std::cerr << "Expected missing input path exists\n";
        gst_object_unref(bus);
        gst_object_unref(file);
        gst_object_unref(pipeline);
        return 1;
    }
    const bool real_error = observe_missing_file_error(pipeline, file, bus, missing.c_str());
    const bool warning_parsed = parse_example_warning(pipeline);

    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(bus);
    gst_object_unref(file);
    gst_object_unref(pipeline);
    if (!real_error || !warning_parsed)
        return 1;
    std::cout << "Error and warning parsing: PASS\n";
    return 0;
}
