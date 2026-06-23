import torch
import torch.nn as nn
import tensorrt as trt
import os

# 1. Define Dummy PyTorch LSTM Model
class HFTModel(nn.Module):
    def __init__(self):
        super().__init__()
        # Input shape: (Batch=1, SeqLen=64, Features=6)
        self.lstm = nn.LSTM(input_size=6, hidden_size=32, num_layers=1, batch_first=True)
        self.fc = nn.Linear(32, 1)
        self.sigmoid = nn.Sigmoid()

    def forward(self, x):
        out, _ = self.lstm(x)
        out = self.fc(out[:, -1, :]) # Take the last tick's output
        return self.sigmoid(out)

def export_model():
    print("[1] Exporting PyTorch model to ONNX...")
    model = HFTModel().eval().cuda()
    
    # 1 Batch, 64 Ticks, 6 Features
    dummy_input = torch.randn(1, 64, 6, device="cuda")
    
    onnx_path = "lstm_model.onnx"
    torch.onnx.export(
        model, dummy_input, onnx_path,
        input_names=["input"], output_names=["output"],
        opset_version=17
    )

    # 2. Build TensorRT Engine with FP16
    print("[2] Building highly optimized TensorRT Engine (FP16)...")
    logger = trt.Logger(trt.Logger.INFO)
    builder = trt.Builder(logger)
    network = builder.create_network(1 << int(trt.NetworkDefinitionCreationFlag.EXPLICIT_BATCH))
    parser = trt.OnnxParser(network, logger)

    with open(onnx_path, "rb") as f:
        if not parser.parse(f.read()):
            for error in range(parser.num_errors):
                print(parser.get_error(error))
            return

    config = builder.create_builder_config()
    config.set_memory_pool_limit(trt.MemoryPoolType.WORKSPACE, 1 << 30) # 1GB memory limit
    
    # Enable Half-Precision (FP16) for massive throughput increases without quality loss
    if builder.platform_has_fast_fp16:
        config.set_flag(trt.BuilderFlag.FP16)
        print("[!] Hardware FP16 Optimization Enabled.")

    serialized_engine = builder.build_serialized_network(network, config)
    if serialized_engine is None:
        print("Failed to build TensorRT engine.")
        return

    with open("lstm_model.engine", "wb") as f:
        f.write(serialized_engine)

    print("[3] Successfully exported lstm_model.engine! Ready for deployment.")

if __name__ == "__main__":
    export_model()
