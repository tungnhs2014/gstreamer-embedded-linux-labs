#include <gst/gst.h>

#include <iostream>

struct WatchContext
{
    GMainLoop* loop = nullptr;      // Owned by main until both sources are removed.
    GstElement* pipeline = nullptr; // Owned by main through pipeline shutdown.
    guint messages = 0;
    guint pipeline_changes = 0;
    bool eos = false;
    bool error = false;
    bool timed_out = false;
};

/** @brief Handles borrowed bus-watch messages on the GLib main-loop thread. */
static gboolean on_bus_message(GstBus*, GstMessage* message, gpointer user_data)
{
    auto* context = static_cast<WatchContext*>(user_data);
    ++context->messages;
    if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_STATE_CHANGED &&
        GST_MESSAGE_SRC(message) == GST_OBJECT(context->pipeline))
    {
        ++context->pipeline_changes;
    }
    else if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS)
    {
        context->eos = true;
        g_main_loop_quit(context->loop);
    }
    else if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR)
    {
        GError* error = nullptr;
        gst_message_parse_error(message, &error, nullptr);
        std::cerr << "Bus ERROR: " << (error ? error->message : "unknown") << '\n';
        g_clear_error(&error);
        context->error = true;
        g_main_loop_quit(context->loop);
    }
    return G_SOURCE_CONTINUE; // Main removes the watch before context destruction.
}

/** @brief Bounds the example if neither EOS nor ERROR arrives. */
static gboolean on_timeout(gpointer user_data)
{
    auto* context = static_cast<WatchContext*>(user_data);
    context->timed_out = true;
    g_main_loop_quit(context->loop);
    return G_SOURCE_REMOVE;
}

/** @brief Runs one bounded main-loop session and removes its sources before returning. */
static bool run_watch(GstBus* bus, WatchContext& context)
{
    const guint watch_id = gst_bus_add_watch(bus, on_bus_message, &context);
    const guint timeout_id = g_timeout_add_seconds(5, on_timeout, &context);
    const GstStateChangeReturn started =
        watch_id && timeout_id ? gst_element_set_state(context.pipeline, GST_STATE_PLAYING)
                               : GST_STATE_CHANGE_FAILURE;
    if (started != GST_STATE_CHANGE_FAILURE)
        g_main_loop_run(context.loop);

    const bool stopped =
        gst_element_set_state(context.pipeline, GST_STATE_NULL) != GST_STATE_CHANGE_FAILURE;
    if (watch_id)
        g_source_remove(watch_id);
    if (timeout_id && !context.timed_out)
        g_source_remove(timeout_id);
    return started != GST_STATE_CHANGE_FAILURE && stopped && !context.error && !context.timed_out &&
           context.eos && context.pipeline_changes >= 3;
}

int main(int argc, char* argv[])
{
    gst_init(&argc, &argv);
    GError* error = nullptr;
    GstElement* pipeline =
        gst_parse_launch("videotestsrc num-buffers=3 ! fakesink sync=false", &error);
    if (error || !pipeline)
    {
        if (error)
        {
            std::cerr << error->message << '\n';
            g_error_free(error);
        }
        if (pipeline)
            gst_object_unref(pipeline);
        return 1;
    }
    GstBus* bus = gst_element_get_bus(pipeline);
    GMainLoop* loop = g_main_loop_new(nullptr, FALSE);
    if (!bus || !loop)
    {
        if (bus)
            gst_object_unref(bus);
        if (loop)
            g_main_loop_unref(loop);
        gst_object_unref(pipeline);
        return 1;
    }

    WatchContext context{loop, pipeline};
    const bool passed = run_watch(bus, context);
    gst_object_unref(bus);
    g_main_loop_unref(loop);
    gst_object_unref(pipeline);

    std::cout << "Watch messages: " << context.messages
              << ", pipeline state changes: " << context.pipeline_changes
              << ", EOS: " << context.eos << '\n';
    if (!passed)
        return 1;
    std::cout << "Asynchronous bus watch: PASS\n";
    return 0;
}
