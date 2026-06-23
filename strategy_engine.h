#ifndef STRATEGY_ENGINE_H
#define STRATEGY_ENGINE_H

#include <rte_ring.h>
#include <array>
#include "tensorrt_engine.h"
#include "order_execution.h"

class StrategyEngine {
public:
    StrategyEngine(struct rte_ring* ring, OrderExecutionEngine& execution_engine);
    ~StrategyEngine();

    /**
     * @brief The main polling loop for the strategy thread.
     */
    void run();

private:
    struct rte_ring* m_market_data_ring;
    TensorRTEngine m_trt_engine;
    OrderExecutionEngine& m_execution_engine;
    bool m_inference_in_flight;
    uint64_t m_current_ingress_tsc;

    // LSTM Rolling window for 64 ticks, 6 features each
    static constexpr size_t SEQ_LEN = 64;
    static constexpr size_t NUM_FEATURES = 6;
    
    // Circular array simulating a continuous buffer
    std::array<float, SEQ_LEN * NUM_FEATURES> m_rolling_window;
};

/**
 * @brief C-style entry point to be used with rte_eal_remote_launch.
 */
extern "C" int strategy_thread_main(void* arg);

#endif // STRATEGY_ENGINE_H
