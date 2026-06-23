#include "tensorrt_engine.h"
#include <iostream>

void TRTLogger::log(Severity severity, const char* msg) noexcept {
    if (severity <= Severity::kWARNING) {
        std::cerr << "[TensorRT] " << msg << std::endl;
    }
}

TensorRTEngine::TensorRTEngine() : 
    m_runtime(nullptr), m_engine(nullptr), m_context(nullptr), 
    m_stream(nullptr), m_event(nullptr), m_pinned_input(nullptr), m_pinned_output(nullptr) {
    m_device_bindings[0] = nullptr;
    m_device_bindings[1] = nullptr;
}

TensorRTEngine::~TensorRTEngine() {
    if (m_stream) cudaStreamDestroy(m_stream);
    if (m_event) cudaEventDestroy(m_event);
    if (m_pinned_input) cudaFreeHost(m_pinned_input);
    if (m_pinned_output) cudaFreeHost(m_pinned_output);
    if (m_context) m_context->destroy();
    if (m_engine) m_engine->destroy();
    if (m_runtime) m_runtime->destroy();
}

bool TensorRTEngine::initialize(const std::string& engine_path) {
    (void)engine_path; // Suppress unused warning since we stub deserialization

    // 1. Create custom CUDA stream to avoid default Stream 0 blocking
    // Essential for HFT asynchronous pipelines.
    if (cudaStreamCreate(&m_stream) != cudaSuccess) {
        std::cerr << "Failed to create custom CUDA stream." << std::endl;
        return false;
    }

    if (cudaEventCreate(&m_event) != cudaSuccess) {
        std::cerr << "Failed to create CUDA event." << std::endl;
        return false;
    }

    // 2. Load the pre-built engine file from disk
    // In a real environment, you deserialize the file into m_engine and create m_context.
    // m_runtime = nvinfer1::createInferRuntime(m_logger);
    // ... file reading & deserialization ...
    // m_context = m_engine->createExecutionContext();

    // 3. Allocate Zero-Copy Pinned Memory using cudaHostAllocMapped
    // Input: 1 x 64 x 6 = 384 floats
    // Output: 1 x 1 = 1 float
    size_t input_size = 1 * 64 * 6 * sizeof(float);
    size_t output_size = 1 * 1 * sizeof(float);

    cudaError_t err1 = cudaHostAlloc((void**)&m_pinned_input, input_size, cudaHostAllocMapped);
    cudaError_t err2 = cudaHostAlloc((void**)&m_pinned_output, output_size, cudaHostAllocMapped);

    if (err1 != cudaSuccess || err2 != cudaSuccess) {
        std::cerr << "cudaHostAlloc failed!" << std::endl;
        return false;
    }

    // 4. Get the device pointers for the mapped pinned memory
    // These pointers are what we pass to TensorRT's enqueue bindings.
    cudaHostGetDevicePointer(&m_device_bindings[0], m_pinned_input, 0);
    cudaHostGetDevicePointer(&m_device_bindings[1], m_pinned_output, 0);

    return true;
}

void TensorRTEngine::infer() {
    // In a fully loaded engine, we execute inference asynchronously on the custom stream:
    // m_context->enqueueV2(m_device_bindings, m_stream, nullptr);
    
    // Record the event immediately after the asynchronous enqueue call
    // This allows us to poll the event status without blocking the CPU thread.
    cudaEventRecord(m_event, m_stream);
}

cudaError_t TensorRTEngine::poll_inference() {
    return cudaEventQuery(m_event);
}
