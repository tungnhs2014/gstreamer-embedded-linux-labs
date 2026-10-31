#pragma once

#include <gst/gst.h>

#include <iostream>

struct BranchObservation
{
    guint first_count = 0;
    guint second_count = 0;
    bool eos = false;
    bool stopped = false;
};

/** @brief Counts borrowed buffers received by one branch. */
static void on_branch_handoff(GstElement*, GstBuffer*, GstPad*, gpointer user_data)
{
    ++*static_cast<guint*>(user_data);
}

/** @brief Observes both branches and stops streaming before callback state expires. */
static BranchObservation observe_branches(GstElement* pipeline, GstBus* bus, GstElement* first,
                                          GstElement* second, GstClockTime timeout)
{
    BranchObservation result{};
    const gulong first_handler =
        g_signal_connect(first, "handoff", G_CALLBACK(on_branch_handoff), &result.first_count);
    const gulong second_handler =
        g_signal_connect(second, "handoff", G_CALLBACK(on_branch_handoff), &result.second_count);

    const bool started =
        gst_element_set_state(pipeline, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE;
    GstMessage* message =
        started
            ? gst_bus_timed_pop_filtered(
                  bus, timeout, static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR))
            : nullptr;
    result.eos = message && GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS;
    if (message && GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR)
    {
        GError* error = nullptr;
        gst_message_parse_error(message, &error, nullptr);
        std::cerr << "Branch pipeline error: " << (error ? error->message : "unknown") << '\n';
        g_clear_error(&error);
    }
    if (message)
        gst_message_unref(message);
    result.stopped = gst_element_set_state(pipeline, GST_STATE_NULL) != GST_STATE_CHANGE_FAILURE;
    g_signal_handler_disconnect(first, first_handler);
    g_signal_handler_disconnect(second, second_handler);
    return result;
}
