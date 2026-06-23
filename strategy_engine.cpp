#include "strategy_engine.h"
#include "network_ingress.h"
#include <iostream>
#include <rte_lcore.h>
#include <rte_branch_prediction.h>

extern volatile bool g_force_quit;

StrategyEngine::StrategyEngine(struct rte_ring* ring, OrderExecutionEngine& exec_engine) 
    : m_market_data_ring(ring), m_execution_engine(exec_engine), m_inference_in_flight(false), m_current_ingress_tsc(0) {
    // Zero out the rolling window
    m_rolling_window.fill(0.0f);
}

StrategyEngine::~StrategyEngine() {}

void StrategyEngine::run() {
    std::cout << "[Strategy Engine] Started on lcore " << rte_lcore_id() << std::endl;

    // Initialize TensorRT Engine (Allocates Zero-Copy mapped memory)
    if (!m_trt_engine.initialize("lstm_model.engine")) {
        std::cerr << "[Strategy Engine] Failed to initialize TensorRT Engine!" << std::endl;
        return;
    }
    
    // Get the PCIe mapped, CPU addressable pointer
    float* trt_input_ptr = m_trt_engine.get_input_buffer();

    while (!g_force_quit) {
        NormalizedOrder order;
        
        // rte_ring_sc_dequeue_elem safely pulls by value directly into our struct.
        int ret = rte_ring_sc_dequeue_elem(m_market_data_ring, &order, sizeof(NormalizedOrder));
        
        if (likely(ret == 0)) {
            __builtin_memmove(m_rolling_window.data(), 
                              m_rolling_window.data() + NUM_FEATURES, 
                              (SEQ_LEN - 1) * NUM_FEATURES * sizeof(float));

            size_t last_idx = (SEQ_LEN - 1) * NUM_FEATURES;
            m_rolling_window[last_idx + 0] = static_cast<float>(order.price);
            m_rolling_window[last_idx + 1] = static_cast<float>(order.shares);
            m_rolling_window[last_idx + 2] = order.side == 'B' ? 1.0f : -1.0f;
            m_rolling_window[last_idx + 3] = static_cast<float>(order.timestamp % 1000000);
            m_rolling_window[last_idx + 4] = 0.0f;
            m_rolling_window[last_idx + 5] = 0.0f;

            __builtin_memcpy(trt_input_ptr, m_rolling_window.data(), SEQ_LEN * NUM_FEATURES * sizeof(float));

            // Store the timestamp of the triggering order
            m_current_ingress_tsc = order.ingress_tsc;

            // Trigger inference
            m_trt_engine.infer();
            m_inference_in_flight = true;
        } else {
            // Ring is empty. If we have an inference in flight, poll the GPU without blocking.
            if (unlikely(m_inference_in_flight)) {
                cudaError_t status = m_trt_engine.poll_inference();
                if (status == cudaSuccess) {
                    // GPU is done!
                    m_inference_in_flight = false;
                    float prediction = m_trt_engine.get_output_buffer()[0];
                    
                    // If the probability exceeds our threshold, instantly fire a trade
                    if (prediction > 0.85f) {
                        // Example logic: fire a buy order for 100 shares at the last known price
                        m_execution_engine.fire_order('B', 100, 
                            static_cast<uint32_t>(m_rolling_window[(SEQ_LEN - 1) * NUM_FEATURES + 0]), 
                            m_current_ingress_tsc);
                    }
                } else if (status != cudaErrorNotReady) {
                    // Handle actual CUDA errors
                    m_inference_in_flight = false;
                }
            }
        }
    }
}

extern "C" int strategy_thread_main(void* arg) {
    StrategyEngine* engine = static_cast<StrategyEngine*>(arg);
    if (engine) {
        engine->run();
    }
    return 0;
}
