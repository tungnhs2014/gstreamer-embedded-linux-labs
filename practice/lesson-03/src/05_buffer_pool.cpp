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

int main(int argc, char* argv[])
{
    GError* error = nullptr;

    if (gst_init_check(&argc, &argv, &error) == FALSE) {
        if (error != nullptr) {
            g_error_free(error);
        }

        return 1;
    }

    GstBufferPool* pool = gst_buffer_pool_new();

    if (pool == nullptr) {
        std::cerr << "Failed to create buffer pool\n";
        return 1;
    }

    GstStructure* config = gst_buffer_pool_get_config(pool);
    gst_buffer_pool_config_set_params(config, nullptr, 256, 2, 2);

    if (gst_buffer_pool_set_config(pool, config) == FALSE) {
        std::cerr << "Failed to configure buffer pool\n";
        gst_object_unref(pool);
        return 1;
    }

    if (gst_buffer_pool_set_active(pool, TRUE) == FALSE) {
        std::cerr << "Failed to activate buffer pool\n";
        gst_object_unref(pool);
        return 1;
    }

    GstBuffer* buffer_a = nullptr;
    GstBuffer* buffer_b = nullptr;
    GstBuffer* buffer_c = nullptr;

    if (acquire_no_wait(pool, &buffer_a) != GST_FLOW_OK ||
        acquire_no_wait(pool, &buffer_b) != GST_FLOW_OK) {
        std::cerr << "Failed to acquire initial buffers\n";

        if (buffer_a != nullptr) {
            gst_buffer_unref(buffer_a);
        }

        if (buffer_b != nullptr) {
            gst_buffer_unref(buffer_b);
        }

        gst_buffer_pool_set_active(pool, FALSE);
        gst_object_unref(pool);
        return 1;
    }

    const GstFlowReturn exhausted_result = acquire_no_wait(pool, &buffer_c);

    std::cout << "Third acquire while two buffers are retained: "
              << gst_flow_get_name(exhausted_result) << '\n';

    if (exhausted_result != GST_FLOW_EOS) {
        std::cerr << "Pool exhaustion check failed\n";
        gst_buffer_unref(buffer_b);
        gst_buffer_unref(buffer_a);
        gst_buffer_pool_set_active(pool, FALSE);
        gst_object_unref(pool);
        return 1;
    }

    // The last reference disappears, so buffer_a returns to the pool.
    gst_buffer_unref(buffer_a);
    buffer_a = nullptr;

    const GstFlowReturn retry_result = acquire_no_wait(pool, &buffer_c);

    if (retry_result != GST_FLOW_OK || buffer_c == nullptr) {
        std::cerr << "Failed to reacquire a returned buffer\n";
        gst_buffer_unref(buffer_b);
        gst_buffer_pool_set_active(pool, FALSE);
        gst_object_unref(pool);
        return 1;
    }

    std::cout << "Acquire after releasing one buffer: " << gst_flow_get_name(retry_result) << '\n';

    gst_buffer_unref(buffer_c);
    gst_buffer_unref(buffer_b);

    gst_buffer_pool_set_active(pool, FALSE);
    gst_object_unref(pool);

    std::cout << "Buffer pool lifecycle: PASS\n";
    return 0;
}