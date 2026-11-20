// Runs the streaming SwiftF0 on raw 16 kHz float32 audio and prints pitch and confidence per frame,
// to compare with the original ONNX model (see check.py).
#include "../../src/dsp/NeuralPitch.h"

#include <cstdio>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    FILE* f = std::fopen(argv[1], "rb");
    if (!f) return 1;
    std::vector<float> x;
    float v;
    while (std::fread(&v, 4, 1, f) == 1) x.push_back(v);
    std::fclose(f);
    for (int i = 0; i < adub::SwiftF0Stream::kDelay + 256; ++i) x.push_back(0.0f); // flush
    static adub::SwiftF0Stream net;
    adub::SwiftF0Stream::Frame fr;
    for (float s : x)
        if (net.push(s, fr)) std::printf("%lld %.4f %.5f\n", (long long) (fr.centre / 256), fr.hz, fr.confidence);
    return 0;
}
