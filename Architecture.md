# Architecture: Ultra-Low Latency High-Frequency Trading Engine

This document provides a deep dive into the hardware-aware software architecture of the HFT Engine. By combining DPDK for kernel bypass and TensorRT for optimized AI execution, this system achieves deterministic sub-microsecond tick-to-trade latencies.

## High-Level Data Flow

```mermaid
graph TD
    subgraph Hardware Layer
        NIC[DPDK-compatible NIC]
        GPU[NVIDIA GPU]
        CPU[Isolated CPU Socket]
    end

    subgraph DPDK Network Ingress
        A[rte_eth_rx_burst] --> B[L1 Cache Prefetch]
        B --> C[ASIC Multicast Filtering]
        C --> D[ITCH Payload Parsing]
    end

    subgraph Lock-Free Transport
        D -- "NormalizedOrder (SPSC Ring)" --> E[rte_ring]
    end

    subgraph Strategy & ML Execution
        E --> F[Strategy Engine]
        F --> G[1.5KB Rolling Window]
        G -- "cudaMemcpyAsync" --> H[TensorRT LSTM Model]
        H -- "Prediction Output" --> I[Decision Matrix]
    end

    subgraph Order Execution Engine
        I -- "Generate OUCH Payload" --> J[Memory Pool Allocation]
        J --> K[rte_eth_tx_burst]
    end

    NIC -- "PCIe DMA" --> A
    H -. "PCIe Pinned Memory" .- GPU
    K -- "Raw Ethernet Frame" --> NIC

    classDef hardware fill:#2d3436,stroke:#b2bec3,stroke-width:2px,color:#dfe6e9;
    classDef ingress fill:#0984e3,stroke:#74b9ff,stroke-width:2px,color:#ffffff;
    classDef transport fill:#e17055,stroke:#fab1a0,stroke-width:2px,color:#ffffff;
    classDef strategy fill:#00b894,stroke:#55efc4,stroke-width:2px,color:#ffffff;
    classDef execution fill:#d63031,stroke:#ff7675,stroke-width:2px,color:#ffffff;

    class NIC,GPU,CPU hardware;
    class A,B,C,D ingress;
    class E transport;
    class F,G,H,I strategy;
    class J,K execution;
```

## Architectural Pillars

### 1. Kernel Bypass with DPDK
Traditional networking stacks in Linux introduce heavy latency due to context switching, IRQ handling, and multiple buffer copies. 
- **Zero-Copy DMA:** Incoming packets are directly DMA'd (Direct Memory Access) from the NIC into userspace `rte_mbuf` structures.
- **Polling over Interrupts:** The CPU core is 100% dedicated to a tight `while` loop continuously polling `rte_eth_rx_burst`.

### 2. Lock-Free Single-Producer Single-Consumer (SPSC) Queues
Mutexes and locks cause disastrous latency spikes. Thread handoff is accomplished using DPDK's `rte_ring`.
- **Cache-Line Aligned:** The rings are perfectly padded to prevent false sharing between L1 caches of different cores.
- **Pass by Value:** Lightweight `NormalizedOrder` structs are copied directly into the ring buffer, preventing complex memory lifetime issues and pointer chasing.

### 3. GPU/CPU PCIe Symbiosis
To prevent the GPU from blocking the critical path:
- **Pinned Host Memory:** We use `cudaHostAlloc` to allocate memory that is permanently mapped into both the CPU and GPU virtual address spaces.
- **Asynchronous Execution:** Inference is launched on a dedicated `cudaStream_t`. The CPU is immediately freed to process the next incoming market tick while the GPU crunches the numbers.

### 4. Hardware/ASIC Offloading
- **RTE Flow Filtering:** Extraneous market data is discarded at the hardware level using the NIC's ASIC before it ever traverses the PCIe bus to reach the CPU.

### 5. Deterministic Memory Management
- **No `malloc` or `new`:** All memory is pre-allocated at startup in `rte_mempool` structures. 
- During runtime, generating an order requires popping a pre-sized buffer from the pool, filling it, and blasting it out via `rte_eth_tx_burst`.
