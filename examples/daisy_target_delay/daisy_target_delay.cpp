#include "pedal_harness/delay_firmware_runtime.hpp"

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

using phh::DelayFirmwareRuntime;
using phh::DigitalDelayNode;

constexpr std::size_t kAudioBlockFrames = PHH_AUDIO_BLOCK_SIZE;
constexpr float kTargetSampleRateHz = 48000.0F;
constexpr std::size_t kMaxDelaySamples = 2U * 48000U;
constexpr std::size_t kArenaBytes = 1024U * 1024U;
constexpr std::size_t kProfileSamples = 4096U;
constexpr float kMaxHarnessFeedback = 0.95F;

static_assert(kAudioBlockFrames > 0U, "Audio block size must be non-zero");
static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
              "32-bit atomics must be lock-free in the audio path");

#if defined(PHH_DAISY_BOARD_POD)
using TargetBoard = daisy::DaisyPod;
TargetBoard g_hardware;
constexpr const char* kBoardName = "DaisyPod";
#elif defined(PHH_DAISY_BOARD_FIELD)
using TargetBoard = daisy::DaisyField;
TargetBoard g_hardware;
constexpr const char* kBoardName = "DaisyField";
#endif

// The portable delay owns its history through StaticArena. Keep the backing
// store in external SDRAM so long-delay traffic is explicit and inspectable.
__attribute__((section(".sdram_bss")))
std::uint8_t g_arena_storage[kArenaBytes];

DelayFirmwareRuntime g_runtime(kMaxDelaySamples);
phh::DiagnosticsCounters g_diagnostics{};

float g_sample_rate_hz = kTargetSampleRateHz;
std::size_t g_arena_used_bytes = 0U;

class HarnessGestureStateMachine final {
  public:
    void Process(bool interpolation_toggle_edge,
                 bool bypass_toggle_edge) noexcept {
        if(interpolation_toggle_edge) {
            use_cubic_ = !use_cubic_;
        }
        if(bypass_toggle_edge) {
            bypassed_ = !bypassed_;
        }
    }

    [[nodiscard]] bool UseCubic() const noexcept { return use_cubic_; }
    [[nodiscard]] bool Bypassed() const noexcept { return bypassed_; }

  private:
    bool use_cubic_ = false;
    bool bypassed_ = false;
};

HarnessGestureStateMachine g_gestures;

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

        // Audio callback stops writing while ready_ is true. Sorting and
        // reporting therefore happen outside the callback without races.
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

float Clamp01(float value) noexcept {
    if(value < 0.0F) {
        return 0.0F;
    }
    if(value > 1.0F) {
        return 1.0F;
    }
    return value;
}

DelayFirmwareRuntime::ParameterArray DefaultParameters() noexcept {
    DelayFirmwareRuntime::ParameterArray parameters{};
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

DelayFirmwareRuntime::ParameterArray MapHarnessControls(
    float time_normalized,
    float feedback_normalized,
    float mix_normalized,
    bool use_cubic) noexcept {
    auto parameters = DefaultParameters();
    parameters[DigitalDelayNode::DelaySamples] =
        1.0F + Clamp01(time_normalized)
                   * static_cast<float>(kMaxDelaySamples - 1U);
    parameters[DigitalDelayNode::Feedback] =
        Clamp01(feedback_normalized) * kMaxHarnessFeedback;
    parameters[DigitalDelayNode::DryMix] = 1.0F - Clamp01(mix_normalized);
    parameters[DigitalDelayNode::WetMix] = Clamp01(mix_normalized);
    parameters[DigitalDelayNode::InterpolationMode] = use_cubic ? 1.0F : 0.0F;
    return parameters;
}

void ServiceControlDomain() noexcept {
    // Board scanning, gesture handling and parameter mapping all live outside
    // the audio callback. The resulting complete parameter set is published as
    // one bounded coherent block snapshot.
    g_hardware.ProcessAllControls();

    float time_normalized = 0.5F;
    float feedback_normalized = 0.35F / kMaxHarnessFeedback;
    float mix_normalized = 0.5F;
    bool interpolation_toggle_edge = false;
    bool bypass_toggle_edge = false;

#if defined(PHH_DAISY_BOARD_POD)
    time_normalized = Clamp01(g_hardware.GetKnobValue(TargetBoard::KNOB_1));
    feedback_normalized =
        Clamp01(g_hardware.GetKnobValue(TargetBoard::KNOB_2));
    interpolation_toggle_edge = g_hardware.button1.RisingEdge();
    bypass_toggle_edge = g_hardware.button2.RisingEdge();
#elif defined(PHH_DAISY_BOARD_FIELD)
    time_normalized = Clamp01(g_hardware.GetKnobValue(TargetBoard::KNOB_1));
    feedback_normalized =
        Clamp01(g_hardware.GetKnobValue(TargetBoard::KNOB_2));
    mix_normalized = Clamp01(g_hardware.GetKnobValue(TargetBoard::KNOB_3));
    interpolation_toggle_edge = g_hardware.sw[TargetBoard::SW_1].RisingEdge();
    bypass_toggle_edge = g_hardware.sw[TargetBoard::SW_2].RisingEdge();
#endif

    g_gestures.Process(interpolation_toggle_edge, bypass_toggle_edge);

    g_runtime.PublishParameters(MapHarnessControls(time_normalized,
                                                   feedback_normalized,
                                                   mix_normalized,
                                                   g_gestures.UseCubic()));
    g_runtime.SetBypass(
        g_gestures.Bypassed(),
        DelayFirmwareRuntime::BypassPolicy::PreserveTails);
}

void BypassCopy(daisy::AudioHandle::InputBuffer in,
                daisy::AudioHandle::OutputBuffer out,
                std::size_t frames) noexcept {
    for(std::size_t channel = 0U; channel < 2U; ++channel) {
        for(std::size_t frame = 0U; frame < frames; ++frame) {
            out[channel][frame] = in[channel][frame];
        }
    }
}

void AudioCallback(daisy::AudioHandle::InputBuffer in,
                   daisy::AudioHandle::OutputBuffer out,
                   std::size_t frames) {
#if PHH_ENABLE_PROFILING
    const std::uint32_t start_cycles = DWT->CYCCNT;
#endif

    phh::AudioBlock block{{in[0], in[1]}, {out[0], out[1]}, frames};
    if(!g_runtime.ProcessAudio(block, g_diagnostics)) {
        BypassCopy(in, out, frames);
    }

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

const char* AudioStateName(DelayFirmwareRuntime::AudioState state) noexcept {
    switch(state) {
        case DelayFirmwareRuntime::AudioState::Effect:
            return "effect";
        case DelayFirmwareRuntime::AudioState::BypassTails:
            return "bypass_tails";
        case DelayFirmwareRuntime::AudioState::FlushClearing:
            return "flush_clearing";
        case DelayFirmwareRuntime::AudioState::BypassDry:
            return "bypass_dry";
    }
    return "unknown";
}

void PrintBootRecord() noexcept {
    const auto arena_address = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(&g_arena_storage[0]));
    const auto arena_region = daisy::System::GetMemoryRegion(arena_address);

    daisy::DaisySeed::PrintLine(
        "PHH_TARGET board=%s fs=%lu block=%lu sysclk=%lu arena_used=%lu "
        "arena_capacity=%lu arena_region=%d max_delay=%lu profile=%d",
        kBoardName,
        static_cast<unsigned long>(g_sample_rate_hz),
        static_cast<unsigned long>(kAudioBlockFrames),
        static_cast<unsigned long>(daisy::System::GetSysClkFreq()),
        static_cast<unsigned long>(g_arena_used_bytes),
        static_cast<unsigned long>(kArenaBytes),
        static_cast<int>(arena_region),
        static_cast<unsigned long>(g_runtime.MaxDelaySamples()),
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
        "budget=%lu overruns=%lu snapshot_misses=%lu param_gen=%lu "
        "interp=%s audio_state=%s flush_remaining=%lu",
        kBoardName,
        static_cast<unsigned long>(summary.sample_count),
        static_cast<unsigned long>(summary.average_cycles),
        static_cast<unsigned long>(summary.p999_cycles),
        static_cast<unsigned long>(summary.max_cycles),
        static_cast<unsigned long>(g_callback_budget_cycles),
        static_cast<unsigned long>(summary.overrun_count),
        static_cast<unsigned long>(g_runtime.SnapshotMisses()),
        static_cast<unsigned long>(g_runtime.ActiveParameterGeneration()),
        g_gestures.UseCubic() ? "cubic" : "linear",
        AudioStateName(g_runtime.PublishedAudioState()),
        static_cast<unsigned long>(g_runtime.FlushFramesRemaining()));
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
    if(!g_runtime.Prepare({g_sample_rate_hz,
                           kAudioBlockFrames,
                           phh::ChannelLayout::Stereo},
                          arena,
                          DefaultParameters())) {
        FatalBlink();
    }
    g_arena_used_bytes = arena.Used();

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
        ServiceControlDomain();

#if PHH_ENABLE_PROFILING
        ServiceDiagnosticsDomain();
#endif

        // Tempo/MIDI, display/UI rendering and storage are intentionally not
        // active in this first DIGI graph. When introduced, they remain main-
        // loop services and must not migrate into AudioCallback.
        daisy::System::Delay(1U);
    }
}
