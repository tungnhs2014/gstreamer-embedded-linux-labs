#include <gst/gst.h>

#include <iostream>

/**
 * @brief Acquires a buffer without blocking.
 */
static GstFlowReturn acquire_no_wait(GstBufferPool* pool, GstBuffer** buffer)
{
    GstBufferPoolAcquireParams params{};
    params.flags = GST_BUFFER_POOL_ACQUIRE_FLAG_DONTWAIT;

    return gst_buffer_pool_acquire_buffer(pool, buffer, &params);
}

/** @brief Holds every pool buffer, observes exhaustion, then retries after release. */
static bool verify_pool_reuse(GstBufferPool* pool)
{
    GstBuffer* first = nullptr;
    GstBuffer* second = nullptr;
    GstBuffer* third = nullptr;
    bool reused = false;

    const bool acquired_two = acquire_no_wait(pool, &first) == GST_FLOW_OK &&
                              acquire_no_wait(pool, &second) == GST_FLOW_OK;
    if (acquired_two)
    {
        const GstFlowReturn exhausted = acquire_no_wait(pool, &third);
        std::cout << "Third acquire while two buffers are retained: "
                  << gst_flow_get_name(exhausted) << '\n';
        if (exhausted == GST_FLOW_EOS)
        {
            // Removing the final reference returns this buffer to the active pool.
            gst_buffer_unref(first);
            first = nullptr;
            const GstFlowReturn retried = acquire_no_wait(pool, &third);
            std::cout << "Acquire after releasing one buffer: " << gst_flow_get_name(retried)
                      << '\n';
            reused = retried == GST_FLOW_OK && third != nullptr;
        }
    }

    if (third)
        gst_buffer_unref(third);
    if (second)
        gst_buffer_unref(second);
    if (first)
        gst_buffer_unref(first);
    return acquired_two && reused;
}

int main(int argc, char* argv[])
{
    GError* error = nullptr;

    if (gst_init_check(&argc, &argv, &error) == FALSE)
    {
        if (error != nullptr)
        {
            g_error_free(error);
        }

        return 1;
    }

    GstBufferPool* pool = gst_buffer_pool_new();

    if (pool == nullptr)
    {
        std::cerr << "Failed to create buffer pool\n";
        return 1;
    }

    GstStructure* config = gst_buffer_pool_get_config(pool);
    gst_buffer_pool_config_set_params(config, nullptr, 256, 2, 2);

    if (gst_buffer_pool_set_config(pool, config) == FALSE)
    {
        std::cerr << "Failed to configure buffer pool\n";
        gst_object_unref(pool);
        return 1;
    }

    if (gst_buffer_pool_set_active(pool, TRUE) == FALSE)
    {
        std::cerr << "Failed to activate buffer pool\n";
        gst_object_unref(pool);
        return 1;
    }

    const bool reused = verify_pool_reuse(pool);
    const bool deactivated = gst_buffer_pool_set_active(pool, FALSE) != FALSE;
    gst_object_unref(pool);
    if (!reused || !deactivated)
    {
        std::cerr << "Pool exhaustion or reuse check failed\n";
        return 1;
    }
    std::cout << "Buffer pool lifecycle: PASS\n";
    return 0;
}
