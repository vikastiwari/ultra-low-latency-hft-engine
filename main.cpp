#include <iostream>
#include <rte_eal.h>
#include <rte_lcore.h>
#include <rte_launch.h>
#include "network_ingress.h"
#include "strategy_engine.h"
#include "order_execution.h"
#include "telemetry.h"
#include <vector>
#include <string>
#include <csignal>

volatile bool g_force_quit = false;

static void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        std::cout << "\n[Signal] Received shutdown signal. Gracefully stopping loops..." << std::endl;
        g_force_quit = true;
    }
}

// C-style wrapper for launching the DPDK receiver loop
extern "C" int network_thread_main(void* arg) {
    DPDKReceiver* receiver = static_cast<DPDKReceiver*>(arg);
    if (receiver) {
        receiver->start_polling();
    }
    return 0;
}

int main(int argc, char** argv) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // --- PCAP REPLAY ENGINE OPTION ---
    std::vector<char*> eal_args;
    std::string pcap_vdev = "--vdev=net_pcap0,rx_pcap=";
    
    bool pcap_mode = false;
    for (int i = 0; i < argc; ++i) {
        if (std::string(argv[i]) == "--pcap" && i + 1 < argc) {
            pcap_mode = true;
            pcap_vdev += argv[i + 1];
            eal_args.push_back(const_cast<char*>(pcap_vdev.c_str()));
            i++; // skip filename
        } else {
            eal_args.push_back(argv[i]);
        }
    }

    // 1. Initialize the DPDK Environment Abstraction Layer (EAL)
    int ret = rte_eal_init(eal_args.size(), eal_args.data());
    if (ret < 0) {
        std::cerr << "Error with EAL initialization" << std::endl;
        return -1;
    }

    // 2. Discover Logical Cores (Strict Thread Isolation + NUMA Pinning)
    unsigned int main_lcore = rte_get_main_lcore();

    // Resolve the NUMA node the primary NIC (Port 0) is attached to
    int target_numa = rte_eth_dev_socket_id(0);
    if (target_numa == SOCKET_ID_ANY) target_numa = 0;
    
    unsigned int rx_lcore = RTE_MAX_LCORE;
    unsigned int strategy_lcore = RTE_MAX_LCORE;
    unsigned int telemetry_lcore = RTE_MAX_LCORE;

    unsigned int lcore_id;
    RTE_LCORE_FOREACH_WORKER(lcore_id) {
        // Only assign lcores that live on the exact same CPU socket as our PCIe NIC
        if (rte_lcore_to_socket_id(lcore_id) == (unsigned int)target_numa) {
            if (rx_lcore == RTE_MAX_LCORE) {
                rx_lcore = lcore_id;
            } else if (strategy_lcore == RTE_MAX_LCORE) {
                strategy_lcore = lcore_id;
            } else if (telemetry_lcore == RTE_MAX_LCORE) {
                telemetry_lcore = lcore_id;
                break;
            }
        }
    }

    if (rx_lcore == RTE_MAX_LCORE || strategy_lcore == RTE_MAX_LCORE || telemetry_lcore == RTE_MAX_LCORE) {
        std::cerr << "Error: Insufficient isolated cores available on NUMA node " << target_numa << std::endl;
        return -1;
    }

    std::cout << "[Orchestrator] Main Lcore:     " << main_lcore << "\n"
              << "[Orchestrator] RX Lcore:       " << rx_lcore << "\n"
              << "[Orchestrator] Strategy Lcore: " << strategy_lcore << std::endl;

    // 3. Initialize DPDK Receiver
    DPDKReceiver receiver;
    // Port 0 is assumed to be our high-speed NIC
    if (!receiver.initialize(argc, argv, 0)) {
        std::cerr << "Failed to initialize DPDK Receiver" << std::endl;
        return -1;
    }

    // 4. Initialize Telemetry Engine
    TelemetryEngine telemetry;
    if (!telemetry.initialize()) {
        std::cerr << "Failed to initialize Telemetry Engine" << std::endl;
        return -1;
    }

    // 5. Initialize Order Execution Engine (TX)
    OrderExecutionEngine execution_engine(telemetry);
    if (!execution_engine.initialize(0, pcap_mode)) {
        std::cerr << "Failed to initialize Order Execution Engine" << std::endl;
        return -1;
    }

    // 6. Initialize Strategy Engine
    StrategyEngine strategy(receiver.get_market_data_ring(), execution_engine);

    // 7. Launch threads on isolated cores
    rte_eal_remote_launch(telemetry_thread_main, &telemetry, telemetry_lcore);
    rte_eal_remote_launch(strategy_thread_main, &strategy, strategy_lcore);
    rte_eal_remote_launch(network_thread_main, &receiver, rx_lcore);

    // 6. Block the main thread and wait for lcores to complete
    rte_eal_mp_wait_lcore();

    // Cleanup
    rte_eal_cleanup();
    return 0;
}
