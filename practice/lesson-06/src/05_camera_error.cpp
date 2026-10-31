#include <gst/gst.h>

#include <cerrno>
#include <iostream>
#include <string>
#include <unistd.h>

/**
 * @brief Stops the pipeline before releasing its owned child and bus references.
 * @return true if the NULL transition was accepted; references are released either way.
 */
static bool stop_and_release(GstElement* pipeline, GstElement* camera, GstElement* mode,
                             GstBus* bus)
{
    const bool stopped =
        gst_element_set_state(pipeline, GST_STATE_NULL) != GST_STATE_CHANGE_FAILURE;
    gst_object_unref(bus);
    gst_object_unref(mode);
    gst_object_unref(camera);
    gst_object_unref(pipeline);
    return stopped;
}

enum class ModeCheck
{
    proven_unsupported,
    device_not_ready,
    unsupported_not_proven,
};

/** @brief Requires an open camera and advertised caps that exclude the request. */
static ModeCheck check_unsupported_mode(GstElement* pipeline, GstElement* camera, GstBus* bus,
                                        const GstCaps* requested)
{
    const GstStateChangeReturn request = gst_element_set_state(pipeline, GST_STATE_READY);
    GstState reached = GST_STATE_VOID_PENDING;
    const GstStateChangeReturn result =
        request == GST_STATE_CHANGE_FAILURE
            ? request
            : gst_element_get_state(pipeline, &reached, nullptr, 5 * GST_SECOND);
    if (result != GST_STATE_CHANGE_SUCCESS || reached != GST_STATE_READY)
    {
        std::cerr << "Camera did not reach READY; requested mode was not tested\n";
        GstMessage* message = gst_bus_timed_pop_filtered(bus, GST_SECOND, GST_MESSAGE_ERROR);
        if (message)
        {
            GError* error = nullptr;
            gst_message_parse_error(message, &error, nullptr);
            std::cerr << "Device error: " << (error ? error->message : "unknown") << '\n';
            g_clear_error(&error);
            gst_message_unref(message);
        }
        return ModeCheck::device_not_ready;
    }

    GstPad* source_pad = gst_element_get_static_pad(camera, "src");
    GstCaps* offered = source_pad ? gst_pad_query_caps(source_pad, nullptr) : nullptr;
    const bool unsupported = offered && !gst_caps_is_any(offered) && !gst_caps_is_empty(offered) &&
                             !gst_caps_can_intersect(offered, requested);
    if (offered)
        gst_caps_unref(offered);
    if (source_pad)
        gst_object_unref(source_pad);
    if (!unsupported)
    {
        std::cerr << "Requested caps are not proven outside advertised camera caps\n";
        return ModeCheck::unsupported_not_proven;
    }
    return ModeCheck::proven_unsupported;
}

/** @brief Parses and classifies the real camera error posted on the Bus. */
static bool is_expected_error(GstMessage* message, GstElement* camera, bool mode_test)
{
    if (!message || GST_MESSAGE_TYPE(message) != GST_MESSAGE_ERROR)
    {
        std::cerr << "Expected an actual V4L2 ERROR message; got EOS or timeout\n";
        return false;
    }

    GError* error = nullptr;
    gchar* debug = nullptr;
    gst_message_parse_error(message, &error, &debug);
    bool matches = false;
    if (error && mode_test)
    {
        matches =
            (error->domain == GST_RESOURCE_ERROR && error->code == GST_RESOURCE_ERROR_SETTINGS) ||
            (error->domain == GST_CORE_ERROR &&
             (error->code == GST_CORE_ERROR_NEGOTIATION || error->code == GST_CORE_ERROR_CAPS)) ||
            (error->domain == GST_STREAM_ERROR && (error->code == GST_STREAM_ERROR_FORMAT ||
                                                   error->code == GST_STREAM_ERROR_WRONG_TYPE ||
                                                   (error->code == GST_STREAM_ERROR_FAILED &&
                                                    debug && g_strrstr(debug, "not-negotiated"))));
    }
    else if (error)
    {
        matches = error->domain == GST_RESOURCE_ERROR &&
                  error->code == GST_RESOURCE_ERROR_NOT_FOUND &&
                  GST_MESSAGE_SRC(message) == GST_OBJECT(camera);
    }
    std::cout << "Device/mode error: " << (error ? error->message : "unknown") << '\n'
              << "Debug: " << (debug ? debug : "none") << '\n';
    g_clear_error(&error);
    g_free(debug);
    return matches;
}

int main(int argc, char* argv[])
{
    gst_init(&argc, &argv);
    if (argc > 3)
    {
        std::cerr << "Usage: " << argv[0] << " [missing device] | " << argv[0]
                  << " /dev/videoX 'unsupported caps'\n";
        return 2;
    }

    const bool mode_test = argc == 3;
    const std::string missing_device = "/dev/gst-learning-labs-missing-" + std::to_string(getpid());
    const char* device = argc >= 2 ? argv[1] : missing_device.c_str();
    if (!mode_test)
    {
        if (access(device, F_OK) == 0 || errno != ENOENT)
        {
            std::cerr << "Missing-device test requires a nonexistent path\n";
            return 2;
        }
    }
    GstCaps* requested = gst_caps_from_string(argc == 3 ? argv[2] : "video/x-raw,format=YUY2");
    if (!requested || gst_caps_is_empty(requested) || gst_caps_is_any(requested))
    {
        std::cerr << "Invalid requested caps\n";
        if (requested)
            gst_caps_unref(requested);
        return 2;
    }

    GError* parse_error = nullptr;
    GstElement* pipeline = gst_parse_launch(
        "v4l2src name=camera num-buffers=3 ! capsfilter name=mode ! fakesink", &parse_error);
    if (parse_error || !pipeline)
    {
        if (parse_error)
        {
            std::cerr << parse_error->message << '\n';
            g_error_free(parse_error);
        }
        if (pipeline)
            gst_object_unref(pipeline);
        gst_caps_unref(requested);
        return 1;
    }
    GstElement* camera = gst_bin_get_by_name(GST_BIN(pipeline), "camera");
    GstElement* mode = gst_bin_get_by_name(GST_BIN(pipeline), "mode");
    GstBus* bus = gst_element_get_bus(pipeline);
    if (!camera || !mode || !bus)
    {
        if (camera)
            gst_object_unref(camera);
        if (mode)
            gst_object_unref(mode);
        if (bus)
            gst_object_unref(bus);
        gst_object_unref(pipeline);
        gst_caps_unref(requested);
        return 1;
    }

    g_object_set(camera, "device", device, nullptr);
    if (mode_test)
    {
        const ModeCheck check = check_unsupported_mode(pipeline, camera, bus, requested);
        if (check != ModeCheck::proven_unsupported)
        {
            stop_and_release(pipeline, camera, mode, bus);
            gst_caps_unref(requested);
            return check == ModeCheck::device_not_ready ? 1 : 2;
        }
    }
    g_object_set(mode, "caps", requested, nullptr);
    gst_caps_unref(requested);
    const GstStateChangeReturn state = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    std::cout << "Start result: " << gst_element_state_change_return_get_name(state) << '\n';
    GstMessage* message = gst_bus_timed_pop_filtered(
        bus, 10 * GST_SECOND, static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));
    const bool actual_error = is_expected_error(message, camera, mode_test);
    if (message)
        gst_message_unref(message);
    const bool stopped = stop_and_release(pipeline, camera, mode, bus);

    if (!actual_error || !stopped)
        return 1;
    std::cout << (mode_test ? "Unsupported camera mode: PASS\n"
                            : "Controlled missing-device error: PASS\n");
    return 0;
}
