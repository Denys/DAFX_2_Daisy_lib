#include "pedal_harness/dsp_contract.hpp"
#include "pedal_harness/static_serial_graph.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace {

class GainNode final : public phh::DspNode {
  public:
    explicit GainNode(float gain) : gain_(gain) {}

    phh::NodeDescriptor Describe() const noexcept override {
        phh::NodeDescriptor descriptor{};
        descriptor.stable_id = "test.gain";
        descriptor.resources.persistent_bytes = sizeof(*this);
        return descriptor;
    }

    bool Prepare(const phh::PrepareSpec&, phh::StaticArena&) noexcept override {
        return true;
    }

    void Reset(phh::ResetReason) noexcept override {}

    void Process(const phh::AudioBlock& block,
                 const phh::ParameterSnapshot&,
                 phh::DiagnosticsCounters&) noexcept override {
        for(std::size_t channel = 0; channel < 2U; ++channel) {
            for(std::size_t frame = 0; frame < block.frames; ++frame) {
                block.out[channel][frame] = block.in[channel][frame] * gain_;
            }
        }
    }

  private:
    float gain_;
};

TEST(StaticSerialGraph, ProcessesTwoNodesWithoutHeap) {
    constexpr std::size_t kFrames = 48;
    std::array<std::uint8_t, 4096> arena_storage{};
    phh::StaticArena arena(arena_storage.data(), arena_storage.size());

    GainNode first(0.5F);
    GainNode second(0.5F);
    phh::StaticSerialGraph<4> graph;

    ASSERT_TRUE(graph.Add(first));
    ASSERT_TRUE(graph.Add(second));
    ASSERT_TRUE(graph.Prepare(
        phh::PrepareSpec{48000.0F, kFrames, phh::ChannelLayout::Stereo},
        arena));

    std::array<float, kFrames> input_left{};
    std::array<float, kFrames> input_right{};
    std::array<float, kFrames> output_left{};
    std::array<float, kFrames> output_right{};
    input_left[0] = 1.0F;
    input_right[0] = -1.0F;

    phh::AudioBlock block{{input_left.data(), input_right.data()},
                          {output_left.data(), output_right.data()},
                          kFrames};
    phh::DiagnosticsCounters diagnostics{};
    graph.Process(block, nullptr, diagnostics);

    EXPECT_FLOAT_EQ(output_left[0], 0.25F);
    EXPECT_FLOAT_EQ(output_right[0], -0.25F);
    EXPECT_EQ(diagnostics.processed_blocks, 1U);
    EXPECT_EQ(diagnostics.processed_samples, kFrames);
    EXPECT_EQ(diagnostics.non_finite_samples, 0U);
}

TEST(StaticSerialGraph, CopiesInputWhenGraphIsEmpty) {
    constexpr std::size_t kFrames = 4;
    std::array<std::uint8_t, 256> arena_storage{};
    phh::StaticArena arena(arena_storage.data(), arena_storage.size());
    phh::StaticSerialGraph<1> graph;

    ASSERT_TRUE(graph.Prepare(
        phh::PrepareSpec{48000.0F, kFrames, phh::ChannelLayout::Stereo},
        arena));

    std::array<float, kFrames> input_left{1.0F, 0.5F, 0.0F, -0.5F};
    std::array<float, kFrames> input_right{-1.0F, -0.5F, 0.0F, 0.5F};
    std::array<float, kFrames> output_left{};
    std::array<float, kFrames> output_right{};

    phh::AudioBlock block{{input_left.data(), input_right.data()},
                          {output_left.data(), output_right.data()},
                          kFrames};
    phh::DiagnosticsCounters diagnostics{};
    graph.Process(block, nullptr, diagnostics);

    EXPECT_EQ(output_left, input_left);
    EXPECT_EQ(output_right, input_right);
    EXPECT_EQ(diagnostics.processed_blocks, 1U);
}

TEST(StaticArena, RejectsOverflow) {
    std::array<std::uint8_t, 32> storage{};
    phh::StaticArena arena(storage.data(), storage.size());

    EXPECT_NE(arena.Allocate(16, 8), nullptr);
    EXPECT_EQ(arena.Allocate(64, 8), nullptr);
}

} // namespace
