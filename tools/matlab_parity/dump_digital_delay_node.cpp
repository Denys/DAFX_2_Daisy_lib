// Parity dump for the MATLAB DIGI model: runs phh::DigitalDelayNode on a mono float32
// stimulus (channel 0; channel 1 gets zeros) and writes channel 0 as float32.
// Usage: dump_digital_delay_node <in.f32> <out.f32> <D> <fb> <send> <dry> <wet>
//                                <lowHz> <highHz> <blockSize>
#include "pedal_harness/digital_delay_node.hpp"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {
constexpr std::size_t kMaxDelaySamples = 120000; // 2.5 s at 48 kHz, as the model's MaxDelayMs
constexpr float kSampleRate = 48000.0F;
} // namespace

int main(int argc, char** argv) {
    if(argc != 11) {
        std::fprintf(stderr,
                     "usage: %s <in.f32> <out.f32> <D> <fb> <send> <dry> <wet> "
                     "<lowHz> <highHz> <blockSize>\n",
                     argv[0]);
        return 2;
    }

    std::FILE* in_file = std::fopen(argv[1], "rb");
    if(in_file == nullptr) {
        std::fprintf(stderr, "cannot open %s\n", argv[1]);
        return 1;
    }
    std::vector<float> input;
    float sample = 0.0F;
    while(std::fread(&sample, sizeof(float), 1, in_file) == 1) {
        input.push_back(sample);
    }
    std::fclose(in_file);

    float values[phh::DigitalDelayNode::ParameterCount];
    for(int i = 0; i < 7; ++i) {
        values[i] = std::strtof(argv[3 + i], nullptr);
    }
    const std::size_t block_size = std::strtoul(argv[10], nullptr, 10);
    if(block_size == 0U) {
        std::fprintf(stderr, "blockSize must be > 0\n");
        return 2;
    }

    phh::DigitalDelayNode node(kMaxDelaySamples);
    std::vector<unsigned char> storage(
        node.Describe().resources.persistent_bytes + 64U);
    phh::StaticArena arena(storage.data(), storage.size());
    if(!node.Prepare({kSampleRate, block_size, phh::ChannelLayout::Stereo}, arena)) {
        std::fprintf(stderr, "Prepare failed\n");
        return 1;
    }

    const phh::ParameterSnapshot snapshot{values, phh::DigitalDelayNode::ParameterCount, 1U};
    phh::DiagnosticsCounters diagnostics{};
    std::vector<float> output(input.size(), 0.0F);
    std::vector<float> zeros(block_size, 0.0F);
    std::vector<float> discard(block_size, 0.0F);

    for(std::size_t start = 0; start < input.size(); start += block_size) {
        const std::size_t frames =
            (input.size() - start < block_size) ? input.size() - start : block_size;
        phh::AudioBlock block{};
        block.in[0] = input.data() + start;
        block.in[1] = zeros.data();
        block.out[0] = output.data() + start;
        block.out[1] = discard.data();
        block.frames = frames;
        node.Process(block, snapshot, diagnostics);
    }

    std::FILE* out_file = std::fopen(argv[2], "wb");
    if(out_file == nullptr
       || std::fwrite(output.data(), sizeof(float), output.size(), out_file)
              != output.size()) {
        std::fprintf(stderr, "cannot write %s\n", argv[2]);
        return 1;
    }
    std::fclose(out_file);
    std::printf("wrote %zu samples, non_finite_samples %llu\n", output.size(),
                static_cast<unsigned long long>(diagnostics.non_finite_samples));
    return 0;
}
