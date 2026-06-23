#ifndef TENSORRT_ENGINE_H
#define TENSORRT_ENGINE_H

#include <string>
#include <NvInfer.h>
#include <cuda_runtime_api.h>

// Custom logger for TensorRT
class TRTLogger : public nvinfer1::ILogger {
public:
    void log(Severity severity, const char* msg) noexcept override;
};

class TensorRTEngine {
public:
    TensorRTEngine();
    ~TensorRTEngine();

    bool initialize(const std::string& engine_path);
    void infer();
    cudaError_t poll_inference();

    // Getters for the zero-copy pinned memory buffers
    float* get_input_buffer() const { return m_pinned_input; }
    float* get_output_buffer() const { return m_pinned_output; }

private:
    TRTLogger m_logger;
    nvinfer1::IRuntime* m_runtime;
    nvinfer1::ICudaEngine* m_engine;
    nvinfer1::IExecutionContext* m_context;

    // CUDA Stream for asynchronous execution (Non-blocking Stream 0)
    cudaStream_t m_stream;
    cudaEvent_t m_event;

    // Pinned memory buffers (CPU accessible, GPU maps them over PCIe)
    float* m_pinned_input;
    float* m_pinned_output;

    // Buffer pointers array for TensorRT execution
    void* m_device_bindings[2];
};

#endif // TENSORRT_ENGINE_H
