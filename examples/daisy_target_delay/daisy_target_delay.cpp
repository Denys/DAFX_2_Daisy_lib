#include "pedal_harness/digital_delay_node.hpp"
#include "pedal_harness/runtime_control.hpp"
#include "pedal_harness/static_serial_graph.hpp"

#if defined(PHH_DAISY_BOARD_POD)
#include "daisy_pod.h"
#elif defined(PHH_DAISY_BOARD_FIELD)
#include "daisy_field.h"
#else
#error "Define exactly one of PHH_DAISY_BOARD_POD or PHH_DAISY_BOARD_FIELD"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#ifndef PHH_AUDIO_BLOCK_SIZE
#define PHH_AUDIO_BLOCK_SIZE 48
#endif

#ifndef PHH_ENABLE_PROFILING
#define PHH_ENABLE_PROFILING 1
#endif

namespace {

using phh::DigitalDelayNode;

constexpr std::size_t kAudioBlockFrames = PHH_AUDIO_BLOCK_SIZE;
constexpr float kTargetSampleRateHz = 48000.0F;
constexpr std::size_t kMaxDelaySamples = 2U * 48000U;
constexpr std::size_t kArenaBytes = 1024U * 1024U;
constexpr std::size_t kProfileSamples = 4096U;
constexpr float kMaxHarnessFeedback = 0.95F;

static_assert(kAudioBlockFrames > 0U, "Audio block size must be non-zero");

#if defined(PHH_DAISY_BOARD_POD)
using TargetBoard = daisy::DaisyPod;
TargetBoard g_hardware;
constexpr const char* kBoardName = "DaisyPod";
#elif defined(PHH_DAISY_BOARD_FIELD)
using TargetBoard = daisy::DaisyField;
TargetBoard g_hardware;
constexpr const char* kBoardName = "DaisyField";
#endif

using ParameterArray = std::array<float, DigitalDelayNode::ParameterCount>;
using ControlFrame =
    phh::RealtimeControlFrame<DigitalDelayNode::ParameterCount>;

// The portable delay owns its history through StaticArena. Keep the backing
// store in external SDRAM so long-delay traffic is explicit and inspectable.
__attribute__((section(".sdram_bss")))
std::uint8_t g_arena_storage[kArenaBytes];

DigitalDelayNode g_delay(kMaxDelaySamples);
phh::StaticSerialGraph<1U> g_graph;
phh::DiagnosticsCounters g_diagnostics{};

float g_sample_rate_hz = kTargetSampleRateHz;
std::size_t g_arena_used_bytes = 0U;

phh::RealtimeControlMailbox<DigitalDelayNode::ParameterCount> g_control_mailbox;
ControlFrame g_audio_control{};
phh::ToggleGestureState g_gesture_state{};
std::atomic<std::uint32_t> g_control_snapshot_misses{0U};

#if PHH_ENABLE_PROFILING
struct ProfileSummary {
    std::uint32_t average_cycles = 0U;
    std::uint32_t p999_cycles = 0U;
    std::uint32_t max_cycles = 0U;
    std::uint32_t overrun_count = 0U;
    std::uint32_t sample_count = 0U;
};

class ProfileCapture final {
  public:
    void Init(std::uint32_t callback_budget_cycles) noexcept {
        callback_budget_cycles_ = callback_budget_cycles;
        count_ = 0U;
        overrun_count_ = 0U;
        ready_.store(false, std::memory_order_release);
    }

    void Record(std::uint32_t cycles) noexcept {
        if(ready_.load(std::memory_order_relaxed)) {
            return;
        }

        if(cycles > callback_budget_cycles_) {
            overrun_count_ += 1U;
        }
        samples_[count_] = cycles;
        count_ += 1U;
        if(count_ == samples_.size()) {
            ready_.store(true, std::memory_order_release);
        }
    }

    [[nodiscard]] bool Summarize(ProfileSummary& summary) noexcept {
        if(!ready_.load(std::memory_order_acquire)) {
            return false;
        }

        // The callback stops writing while ready_ is true. Sorting and
        // reporting therefore remain outside interrupt/audio context.
        std::sort(samples_.begin(), samples_.begin() + count_);

        std::uint64_t sum = 0U;
        for(std::size_t i = 0; i < count_; ++i) {
            sum += samples_[i];
        }

        const std::size_t nearest_rank =
            (999U * count_ + 999U) / 1000U;
        const std::size_t p999_index =
            nearest_rank == 0U ? 0U : nearest_rank - 1U;

        summary.average_cycles =
            static_cast<std::uint32_t>(sum / count_);
        summary.p999_cycles = samples_[p999_index];
        summary.max_cycles = samples_[count_ - 1U];
        summary.overrun_count = overrun_count_;
        summary.sample_count = static_cast<std::uint32_t>(count_);

        count_ = 0U;
        overrun_count_ = 0U;
        ready_.store(false, std::memory_order_release);
        return true;
    }

  private:
    std::array<std::uint32_t, kProfileSamples> samples_{};
    std::size_t count_ = 0U;
    std::uint32_t overrun_count_ = 0U;
    std::uint32_t callback_budget_cycles_ = 0U;
    std::atomic<bool> ready_{false};
};

ProfileCapture g_profile_capture;
std::uint32_t g_callback_budget_cycles = 0U;

void InitCycleCounter() noexcept {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    __DSB();
    __ISB();
}
#endif

struct HarnessControlInputs {
    float time_normalized = 0.5F;
    float feedback_normalized = 0.35F / kMaxHarnessFeedback;
    float mix_normalized = 0.5F;
    bool interpolation_toggle_edge = false;
    bool bypass_toggle_edge = false;
};

ParameterArray DefaultParameters() noexcept {
    ParameterArray parameters{};
    parameters[DigitalDelayNode::DelaySamples] = 24000.0F; // 500 ms at 48 kHz
    parameters[DigitalDelayNode::Feedback] = 0.35F;
    parameters[DigitalDelayNode::InputSend] = 1.0F;
    parameters[DigitalDelayNode::DryMix] = 0.5F;
    parameters[DigitalDelayNode::WetMix] = 0.5F;
    parameters[DigitalDelayNode::FeedbackLowCutHz] = 0.0F;
    parameters[DigitalDelayNode::FeedbackHighCutHz] =
        g_sample_rate_hz * 0.49F;
    parameters[DigitalDelayNode::InterpolationMode] = 0.0F;
    return parameters;
}

HarnessControlInputs ReadBoardControls() noexcept {
    g_hardware.ProcessAllControls();

    HarnessControlInputs inputs{};

#if defined(PHH_DAISY_BOARD_POD)
    inputs.time_normalized = phh::ClampNormalized(
        g_hardware.GetKnobValue(TargetBoard::KNOB_1), 0.5F);
    inputs.feedback_normalized = phh::ClampNormalized(
        g_hardware.GetKnobValue(TargetBoard::KNOB_2),
        0.35F / kMaxHarnessFeedback);
    inputs.interpolation_toggle_edge = g_hardware.button1.RisingEdge();
    inputs.bypass_toggle_edge = g_hardware.button2.RisingEdge();
#elif defined(PHH_DAISY_BOARD_FIELD)
    inputs.time_normalized = phh::ClampNormalized(
        g_hardware.GetKnobValue(TargetBoard::KNOB_1), 0.5F);
    inputs.feedback_normalized = phh::ClampNormalized(
        g_hardware.GetKnobValue(TargetBoard::KNOB_2),
        0.35F / kMaxHarnessFeedback);
    inputs.mix_normalized = phh::ClampNormalized(
        g_hardware.GetKnobValue(TargetBoard::KNOB_3), 0.5F);
    inputs.interpolation_toggle_edge =
        g_hardware.sw[TargetBoard::SW_1].RisingEdge();
    inputs.bypass_toggle_edge =
        g_hardware.sw[TargetBoard::SW_2].RisingEdge();
#endif

    return inputs;
}

ParameterArray MapControls(const HarnessControlInputs& inputs) noexcept {
    ParameterArray parameters = DefaultParameters();
    parameters[DigitalDelayNode::DelaySamples] =
        1.0F + inputs.time_normalized
                   * static_cast<float>(kMaxDelaySamples - 1U);
    parameters[DigitalDelayNode::Feedback] =
        inputs.feedback_normalized * kMaxHarnessFeedback;
    parameters[DigitalDelayNode::DryMix] = 1.0F - inputs.mix_normalized;
    parameters[DigitalDelayNode::WetMix] = inputs.mix_normalized;
    parameters[DigitalDelayNode::InterpolationMode] =
        g_gesture_state.cubic_interpolation ? 1.0F : 0.0F;
    return parameters;
}

void ServiceControlDomain() noexcept {
    const HarnessControlInputs inputs = ReadBoardControls();
    g_gesture_state.Update(inputs.interpolation_toggle_edge,
                           inputs.bypass_toggle_edge);
    const ParameterArray parameters = MapControls(inputs);
    g_control_mailbox.Publish(parameters, g_gesture_state.bypass_with_trails);
}

void ServiceUiDomain() noexcept {
    // Status-only UI for this bounded harness. Full display/menu work remains
    // deliberately absent; this service runs only in main/control context.
    g_hardware.seed.SetLed(!g_gesture_state.bypass_with_trails);
}

void BypassCopy(daisy::AudioHandle::InputBuffer in,
                daisy::AudioHandle::OutputBuffer out,
                std::size_t frames) noexcept {
    for(std::size_t channel = 0; channel < 2U; ++channel) {
        for(std::size_t frame = 0; frame < frames; ++frame) {
            out[channel][frame] = in[channel][frame];
        }
    }
}

ParameterArray EffectiveAudioParameters(const ControlFrame& control) noexcept {
    ParameterArray parameters = control.parameters;
    if(control.bypass_with_trails) {
        // Explicit harness policy: dry signal remains present, new input is no
        // longer injected into the delay, and the existing feedback tail keeps
        // processing/decaying. No claim of click-free transition is made.
        parameters[DigitalDelayNode::InputSend] = 0.0F;
        parameters[DigitalDelayNode::DryMix] = 1.0F;
    }
    return parameters;
}

void AudioCallback(daisy::AudioHandle::InputBuffer in,
                   daisy::AudioHandle::OutputBuffer out,
                   std::size_t frames) {
#if PHH_ENABLE_PROFILING
    const std::uint32_t start_cycles = DWT->CYCCNT;
#endif

    if(frames == 0U || frames > kAudioBlockFrames) {
        BypassCopy(in, out, frames);
#if PHH_ENABLE_PROFILING
        g_profile_capture.Record(DWT->CYCCNT - start_cycles);
#endif
        return;
    }

    ControlFrame candidate = g_audio_control;
    if(g_control_mailbox.ReadBounded(candidate)) {
        g_audio_control = candidate;
    } else {
        g_control_snapshot_misses.fetch_add(1U, std::memory_order_relaxed);
    }

    const ParameterArray effective_parameters =
        EffectiveAudioParameters(g_audio_control);
    const phh::ParameterSnapshot snapshot{effective_parameters.data(),
                                          effective_parameters.size(),
                                          g_audio_control.generation};
    phh::AudioBlock block{{in[0], in[1]}, {out[0], out[1]}, frames};
    g_graph.Process(block, &snapshot, g_diagnostics);

#if PHH_ENABLE_PROFILING
    g_profile_capture.Record(DWT->CYCCNT - start_cycles);
#endif
}

[[noreturn]] void FatalBlink() noexcept {
    while(true) {
        g_hardware.seed.SetLed(true);
        daisy::System::Delay(100U);
        g_hardware.seed.SetLed(false);
        daisy::System::Delay(100U);
    }
}

void PrintBootRecord() noexcept {
    const auto arena_address = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(&g_arena_storage[0]));
    const auto arena_region = daisy::System::GetMemoryRegion(arena_address);

    daisy::DaisySeed::PrintLine(
        "PHH_TARGET board=%s fs=%lu block=%lu sysclk=%lu arena_used=%lu "
        "arena_capacity=%lu arena_region=%d max_delay=%lu profile=%d "
        "bypass_policy=preserve_trails reset_policy=audio_stopped_only",
        kBoardName,
        static_cast<unsigned long>(g_sample_rate_hz),
        static_cast<unsigned long>(kAudioBlockFrames),
        static_cast<unsigned long>(daisy::System::GetSysClkFreq()),
        static_cast<unsigned long>(g_arena_used_bytes),
        static_cast<unsigned long>(kArenaBytes),
        static_cast<int>(arena_region),
        static_cast<unsigned long>(kMaxDelaySamples),
        static_cast<int>(PHH_ENABLE_PROFILING));
}

#if PHH_ENABLE_PROFILING
void ServiceDiagnosticsDomain() noexcept {
    ProfileSummary summary{};
    if(!g_profile_capture.Summarize(summary)) {
        return;
    }

    daisy::DaisySeed::PrintLine(
        "PHH_PROFILE board=%s samples=%lu avg=%lu p999=%lu max=%lu "
        "budget=%lu overruns=%lu snapshot_misses=%lu generation=%lu "
        "interp=%s bypass=%s",
        kBoardName,
        static_cast<unsigned long>(summary.sample_count),
        static_cast<unsigned long>(summary.average_cycles),
        static_cast<unsigned long>(summary.p999_cycles),
        static_cast<unsigned long>(summary.max_cycles),
        static_cast<unsigned long>(g_callback_budget_cycles),
        static_cast<unsigned long>(summary.overrun_count),
        static_cast<unsigned long>(
            g_control_snapshot_misses.load(std::memory_order_relaxed)),
        static_cast<unsigned long>(g_audio_control.generation),
        g_gesture_state.cubic_interpolation ? "cubic" : "linear",
        g_gesture_state.bypass_with_trails ? "trails" : "effect");
}
#endif

} // namespace

int main(void) {
    g_hardware.Init(true);
    g_hardware.SetAudioSampleRate(
        daisy::SaiHandle::Config::SampleRate::SAI_48KHZ);
    g_hardware.SetAudioBlockSize(kAudioBlockFrames);
    g_hardware.StartAdc();

    g_sample_rate_hz = g_hardware.AudioSampleRate();

    phh::StaticArena arena(g_arena_storage, sizeof(g_arena_storage));
    if(!g_graph.Add(g_delay)
       || !g_graph.Prepare({g_sample_rate_hz,
                            kAudioBlockFrames,
                            phh::ChannelLayout::Stereo},
                           arena)) {
        FatalBlink();
    }
    g_arena_used_bytes = arena.Used();

    // Reset/large state mutation is explicitly outside the callback. Any
    // future preset/mode reset must stop audio before calling graph.Reset().
    g_graph.Reset(phh::ResetReason::PowerOn);

    g_audio_control.parameters = DefaultParameters();
    g_control_mailbox.Publish(g_audio_control.parameters, false);

#if PHH_ENABLE_PROFILING
    InitCycleCounter();
    const std::uint64_t budget =
        static_cast<std::uint64_t>(daisy::System::GetSysClkFreq())
        * static_cast<std::uint64_t>(kAudioBlockFrames)
        / static_cast<std::uint64_t>(g_sample_rate_hz);
    g_callback_budget_cycles = static_cast<std::uint32_t>(budget);
    g_profile_capture.Init(g_callback_budget_cycles);
#endif

    daisy::DaisySeed::StartLog(false);
    PrintBootRecord();
    g_hardware.StartAudio(AudioCallback);

    while(true) {
        // Explicit non-audio domains for this increment. Tempo/MIDI and
        // storage are not relevant to the DIGI profiling graph and remain
        // inactive rather than being smuggled into the callback.
        ServiceControlDomain();
        ServiceUiDomain();
#if PHH_ENABLE_PROFILING
        ServiceDiagnosticsDomain();
#endif
        daisy::System::Delay(1U);
    }
}
