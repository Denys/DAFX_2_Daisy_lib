#include "pedal_harness/fractional_delay.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;
bool g_valid = true;

std::string_view Name(phh::InterpolationPolicy policy) {
    return policy == phh::InterpolationPolicy::Linear ? "linear"
                                                       : "cubic_lagrange";
}

void EmitOptional(const std::optional<double>& value) {
    if(value.has_value()) {
        std::cout << *value;
    }
}

void EmitOptionalInt(const std::optional<int>& value) {
    if(value.has_value()) {
        std::cout << *value;
    }
}

void EmitRow(std::string_view metric,
             std::string_view policy,
             std::optional<double> frequency,
             std::optional<double> fraction,
             std::optional<double> mod_depth,
             std::optional<int> passes,
             std::optional<double> feedback_gain,
             double value,
             std::string_view unit) {
    if(!std::isfinite(value)) {
        g_valid = false;
    }

    std::cout << metric << ',' << policy << ',';
    EmitOptional(frequency);
    std::cout << ',';
    EmitOptional(fraction);
    std::cout << ',';
    EmitOptional(mod_depth);
    std::cout << ',';
    EmitOptionalInt(passes);
    std::cout << ',';
    EmitOptional(feedback_gain);
    std::cout << ',' << value << ',' << unit << '\n';
}

std::complex<double> Response(phh::InterpolationPolicy policy,
                              double normalized_frequency,
                              double fraction) {
    const double omega = 2.0 * kPi * normalized_frequency;
    if(policy == phh::InterpolationPolicy::Linear) {
        return (1.0 - fraction)
               + fraction * std::exp(std::complex<double>(0.0, omega));
    }

    const std::array<double, 4> coefficients{
        -fraction * (fraction - 1.0) * (fraction - 2.0) / 6.0,
        (fraction + 1.0) * (fraction - 1.0) * (fraction - 2.0) / 2.0,
        -(fraction + 1.0) * fraction * (fraction - 2.0) / 2.0,
        (fraction + 1.0) * fraction * (fraction - 1.0) / 6.0,
    };
    const std::array<int, 4> offsets{-1, 0, 1, 2};

    std::complex<double> response{0.0, 0.0};
    for(std::size_t index = 0; index < coefficients.size(); ++index) {
        response += coefficients[index]
                    * std::exp(std::complex<double>(
                        0.0,
                        omega * static_cast<double>(offsets[index])));
    }
    return response;
}

std::complex<double> MeasuredResponse(phh::InterpolationPolicy policy,
                                      std::size_t frequency_bin,
                                      double fraction) {
    constexpr std::size_t kCount = 4096U;
    const double normalized_frequency =
        static_cast<double>(frequency_bin) / static_cast<double>(kCount);
    const double omega = 2.0 * kPi * normalized_frequency;

    std::vector<float> buffer(kCount);
    for(std::size_t index = 0; index < kCount; ++index) {
        buffer[index] = static_cast<float>(
            std::cos(omega * static_cast<double>(index)));
    }

    const phh::CircularFractionalReader reader(buffer.data(), buffer.size());
    std::complex<double> input_bin{0.0, 0.0};
    std::complex<double> output_bin{0.0, 0.0};
    for(std::size_t index = 0; index < kCount; ++index) {
        const auto basis = std::exp(std::complex<double>(
            0.0, -omega * static_cast<double>(index)));
        input_bin += static_cast<double>(buffer[index]) * basis;
        output_bin += static_cast<double>(reader.Read(
                          static_cast<std::ptrdiff_t>(index),
                          static_cast<float>(fraction),
                          policy))
                      * basis;
    }
    return output_bin / input_bin;
}

double PhaseError(double measured, double expected) {
    return std::remainder(measured - expected, 2.0 * kPi);
}

double StaticRmsError(phh::InterpolationPolicy policy,
                      double normalized_frequency,
                      double fraction) {
    constexpr std::size_t kSize = 8192U;
    std::vector<float> buffer(kSize);
    for(std::size_t index = 0; index < kSize; ++index) {
        buffer[index] = static_cast<float>(std::sin(
            2.0 * kPi * normalized_frequency * static_cast<double>(index)));
    }

    const phh::CircularFractionalReader reader(buffer.data(), buffer.size());
    double energy = 0.0;
    constexpr std::size_t kSamples = 4096U;
    for(std::size_t index = 32U; index < 32U + kSamples; ++index) {
        const double ideal = std::sin(
            2.0 * kPi * normalized_frequency
            * (static_cast<double>(index) + fraction));
        const double actual = reader.Read(static_cast<std::ptrdiff_t>(index),
                                          static_cast<float>(fraction),
                                          policy);
        const double error = actual - ideal;
        energy += error * error;
    }
    return std::sqrt(energy / static_cast<double>(kSamples));
}

double BinMagnitudeAtBin(const std::vector<float>& signal,
                         std::size_t bin) {
    const double normalized_frequency =
        static_cast<double>(bin) / static_cast<double>(signal.size());
    std::complex<double> accumulator{0.0, 0.0};
    for(std::size_t index = 0; index < signal.size(); ++index) {
        const double phase = -2.0 * kPi * normalized_frequency
                             * static_cast<double>(index);
        accumulator += static_cast<double>(signal[index])
                       * std::exp(std::complex<double>(0.0, phase));
    }
    return std::abs(accumulator) / static_cast<double>(signal.size());
}

double DbRatio(double numerator, double denominator) {
    return 20.0
           * std::log10(std::max(
               numerator / std::max(denominator, 1.0e-30), 1.0e-15));
}

struct ModulationMetrics {
    std::array<double, 4> actual_upper{};
    std::array<double, 4> actual_lower{};
    std::array<double, 4> ideal_upper{};
    std::array<double, 4> ideal_lower{};
    std::array<double, 4> asymmetry{};
    double residual_rms = 0.0;
    double residual_max_spur_dbc = 0.0;
};

ModulationMetrics MeasureModulation(phh::InterpolationPolicy policy) {
    constexpr std::size_t kSize = 16384U;
    constexpr std::size_t kCarrierBin = 2048U;
    constexpr std::size_t kModulationBin = 16U;
    static_assert(kCarrierBin + 8U * kModulationBin < kSize / 2U);

    constexpr double kDepth = 0.45;
    constexpr double kBaseDelay = 64.5;
    const double carrier =
        static_cast<double>(kCarrierBin) / static_cast<double>(kSize);
    const double modulation_frequency =
        static_cast<double>(kModulationBin) / static_cast<double>(kSize);

    std::vector<float> buffer(kSize);
    std::vector<float> output(kSize);
    std::vector<float> ideal(kSize);
    std::vector<float> residual(kSize);
    for(std::size_t index = 0; index < kSize; ++index) {
        buffer[index] = static_cast<float>(
            std::sin(2.0 * kPi * carrier * static_cast<double>(index)));
    }

    const phh::CircularFractionalReader reader(buffer.data(), buffer.size());
    double residual_energy = 0.0;
    for(std::size_t index = 0; index < kSize; ++index) {
        const double modulation =
            kDepth * std::sin(2.0 * kPi * modulation_frequency
                              * static_cast<double>(index));
        const double read_position =
            static_cast<double>(index) - kBaseDelay - modulation;
        const double whole = std::floor(read_position);
        const auto base = static_cast<std::ptrdiff_t>(whole);
        const float fraction = static_cast<float>(read_position - whole);

        output[index] = reader.Read(base, fraction, policy);
        ideal[index] = static_cast<float>(
            std::sin(2.0 * kPi * carrier * read_position));
        residual[index] = output[index] - ideal[index];
        residual_energy += static_cast<double>(residual[index])
                           * static_cast<double>(residual[index]);
    }

    const double output_carrier = BinMagnitudeAtBin(output, kCarrierBin);
    const double ideal_carrier = BinMagnitudeAtBin(ideal, kCarrierBin);
    ModulationMetrics metrics{};
    for(std::size_t order = 1U; order <= 4U; ++order) {
        const std::size_t offset = order * kModulationBin;
        metrics.actual_upper[order - 1U] =
            DbRatio(BinMagnitudeAtBin(output, kCarrierBin + offset),
                    output_carrier);
        metrics.actual_lower[order - 1U] =
            DbRatio(BinMagnitudeAtBin(output, kCarrierBin - offset),
                    output_carrier);
        metrics.ideal_upper[order - 1U] =
            DbRatio(BinMagnitudeAtBin(ideal, kCarrierBin + offset),
                    ideal_carrier);
        metrics.ideal_lower[order - 1U] =
            DbRatio(BinMagnitudeAtBin(ideal, kCarrierBin - offset),
                    ideal_carrier);
        metrics.asymmetry[order - 1U] =
            metrics.actual_upper[order - 1U]
            - metrics.actual_lower[order - 1U];
    }

    metrics.residual_rms =
        std::sqrt(residual_energy / static_cast<double>(kSize));

    double max_spur = 0.0;
    for(std::size_t bin = kCarrierBin - 8U * kModulationBin;
        bin <= kCarrierBin + 8U * kModulationBin;
        ++bin) {
        bool expected_bin = (bin == kCarrierBin);
        for(std::size_t order = 1U; order <= 4U; ++order) {
            expected_bin = expected_bin
                           || bin == kCarrierBin - order * kModulationBin
                           || bin == kCarrierBin + order * kModulationBin;
        }
        if(!expected_bin) {
            max_spur =
                std::max(max_spur, BinMagnitudeAtBin(residual, bin));
        }
    }
    metrics.residual_max_spur_dbc = DbRatio(max_spur, output_carrier);
    return metrics;
}

double RunCpuTrial(phh::InterpolationPolicy policy,
                   std::size_t iterations) {
    constexpr std::size_t kSize = 4096U;
    std::vector<float> buffer(kSize);
    for(std::size_t index = 0; index < kSize; ++index) {
        buffer[index] = static_cast<float>(
            std::sin(0.013 * static_cast<double>(index)));
    }

    const phh::CircularFractionalReader reader(buffer.data(), buffer.size());
    volatile float sink = 0.0F;
    const auto begin = std::chrono::steady_clock::now();
    for(std::size_t index = 0; index < iterations; ++index) {
        sink = sink
               + reader.Read(static_cast<std::ptrdiff_t>(index % kSize),
                             0.371F,
                             policy);
    }
    const auto end = std::chrono::steady_clock::now();
    if(sink == -123456.0F) {
        std::cerr << sink;
    }
    return std::chrono::duration<double, std::nano>(end - begin).count()
           / static_cast<double>(iterations);
}

double Quantile(std::vector<double> values, double quantile) {
    std::sort(values.begin(), values.end());
    const double position =
        quantile * static_cast<double>(values.size() - 1U);
    const auto lower = static_cast<std::size_t>(std::floor(position));
    const auto upper = static_cast<std::size_t>(std::ceil(position));
    const double fraction = position - static_cast<double>(lower);
    return values[lower]
           + fraction * (values[upper] - values[lower]);
}

struct CpuStats {
    double median = 0.0;
    double iqr = 0.0;
};

std::array<CpuStats, 2> MeasureCpu() {
    constexpr std::size_t kWarmupIterations = 200000U;
    constexpr std::size_t kTrialIterations = 2000000U;
    constexpr std::size_t kTrials = 11U;

    (void)RunCpuTrial(phh::InterpolationPolicy::Linear, kWarmupIterations);
    (void)RunCpuTrial(phh::InterpolationPolicy::CubicLagrange,
                      kWarmupIterations);

    std::array<std::vector<double>, 2> samples;
    for(std::size_t trial = 0; trial < kTrials; ++trial) {
        const std::array<phh::InterpolationPolicy, 2> order =
            (trial % 2U == 0U)
                ? std::array<phh::InterpolationPolicy, 2>{
                      phh::InterpolationPolicy::Linear,
                      phh::InterpolationPolicy::CubicLagrange}
                : std::array<phh::InterpolationPolicy, 2>{
                      phh::InterpolationPolicy::CubicLagrange,
                      phh::InterpolationPolicy::Linear};
        for(const auto policy : order) {
            const std::size_t index =
                policy == phh::InterpolationPolicy::Linear ? 0U : 1U;
            samples[index].push_back(
                RunCpuTrial(policy, kTrialIterations));
        }
    }

    std::array<CpuStats, 2> statistics{};
    for(std::size_t index = 0; index < statistics.size(); ++index) {
        statistics[index].median = Quantile(samples[index], 0.5);
        statistics[index].iqr = Quantile(samples[index], 0.75)
                                - Quantile(samples[index], 0.25);
    }
    return statistics;
}

void EmitStaticErrors() {
    for(const auto policy : {phh::InterpolationPolicy::Linear,
                             phh::InterpolationPolicy::CubicLagrange}) {
        for(const double frequency : {0.05, 0.15, 0.30, 0.40, 0.45}) {
            for(const double fraction : {0.125, 0.25, 0.5, 0.75, 0.875}) {
                EmitRow("static_rms_error",
                        Name(policy),
                        frequency,
                        fraction,
                        std::nullopt,
                        std::nullopt,
                        std::nullopt,
                        StaticRmsError(policy, frequency, fraction),
                        "FS_rms");
            }
        }
    }
}

void EmitMeasuredGainErrors() {
    constexpr double kCount = 4096.0;
    for(const auto policy : {phh::InterpolationPolicy::Linear,
                             phh::InterpolationPolicy::CubicLagrange}) {
        for(const std::size_t bin : {205U, 614U, 1229U, 1638U}) {
            for(const double fraction : {0.125, 0.5, 0.875}) {
                const double frequency = static_cast<double>(bin) / kCount;
                const auto analytical = Response(policy, frequency, fraction);
                const auto measured = MeasuredResponse(policy, bin, fraction);
                EmitRow("measured_gain_magnitude_error",
                        Name(policy),
                        frequency,
                        fraction,
                        std::nullopt,
                        std::nullopt,
                        std::nullopt,
                        std::abs(std::abs(measured)
                                 - std::abs(analytical)),
                        "absolute");
                EmitRow("measured_gain_phase_error",
                        Name(policy),
                        frequency,
                        fraction,
                        std::nullopt,
                        std::nullopt,
                        std::nullopt,
                        std::abs(PhaseError(std::arg(measured),
                                            std::arg(analytical))),
                        "rad");
            }
        }
    }
}

void EmitCascadePredictions() {
    for(const auto policy : {phh::InterpolationPolicy::Linear,
                             phh::InterpolationPolicy::CubicLagrange}) {
        for(const double frequency : {0.25, 0.35, 0.40, 0.45}) {
            for(const double fraction : {0.25, 0.5, 0.75}) {
                for(const double feedback_gain : {1.0, 0.75}) {
                    const double magnitude =
                        std::abs(Response(policy, frequency, fraction));
                    for(const int passes : {1, 8, 32, 64}) {
                        const double db =
                            20.0 * static_cast<double>(passes)
                            * std::log10(std::max(
                                feedback_gain * magnitude, 1.0e-15));
                        EmitRow(
                            feedback_gain == 1.0
                                ? "predicted_static_cascade_loss_db"
                                : "predicted_feedback_cascade_loss_db",
                            Name(policy),
                            frequency,
                            fraction,
                            std::nullopt,
                            passes,
                            feedback_gain,
                            db,
                            "dB");
                    }
                }
            }
        }
    }
}

void EmitModulationMetrics() {
    for(const auto policy : {phh::InterpolationPolicy::Linear,
                             phh::InterpolationPolicy::CubicLagrange}) {
        const auto metrics = MeasureModulation(policy);
        for(std::size_t order = 1U; order <= 4U; ++order) {
            const std::string suffix = std::to_string(order) + "_db";
            EmitRow("mod_actual_upper_k" + suffix,
                    Name(policy),
                    0.125,
                    std::nullopt,
                    0.45,
                    std::nullopt,
                    std::nullopt,
                    metrics.actual_upper[order - 1U],
                    "dBc");
            EmitRow("mod_actual_lower_k" + suffix,
                    Name(policy),
                    0.125,
                    std::nullopt,
                    0.45,
                    std::nullopt,
                    std::nullopt,
                    metrics.actual_lower[order - 1U],
                    "dBc");
            EmitRow("mod_ideal_upper_k" + suffix,
                    Name(policy),
                    0.125,
                    std::nullopt,
                    0.45,
                    std::nullopt,
                    std::nullopt,
                    metrics.ideal_upper[order - 1U],
                    "dBc");
            EmitRow("mod_ideal_lower_k" + suffix,
                    Name(policy),
                    0.125,
                    std::nullopt,
                    0.45,
                    std::nullopt,
                    std::nullopt,
                    metrics.ideal_lower[order - 1U],
                    "dBc");
            EmitRow("mod_deviation_upper_k" + suffix,
                    Name(policy),
                    0.125,
                    std::nullopt,
                    0.45,
                    std::nullopt,
                    std::nullopt,
                    metrics.actual_upper[order - 1U]
                        - metrics.ideal_upper[order - 1U],
                    "dB");
            EmitRow("mod_deviation_lower_k" + suffix,
                    Name(policy),
                    0.125,
                    std::nullopt,
                    0.45,
                    std::nullopt,
                    std::nullopt,
                    metrics.actual_lower[order - 1U]
                        - metrics.ideal_lower[order - 1U],
                    "dB");
            EmitRow("mod_asymmetry_k" + suffix,
                    Name(policy),
                    0.125,
                    std::nullopt,
                    0.45,
                    std::nullopt,
                    std::nullopt,
                    metrics.asymmetry[order - 1U],
                    "dB");
        }

        EmitRow("mod_residual_rms",
                Name(policy),
                0.125,
                std::nullopt,
                0.45,
                std::nullopt,
                std::nullopt,
                metrics.residual_rms,
                "FS_rms");
        EmitRow("mod_residual_max_local_spur",
                Name(policy),
                0.125,
                std::nullopt,
                0.45,
                std::nullopt,
                std::nullopt,
                metrics.residual_max_spur_dbc,
                "dBc");
    }
}

void EmitCpuMetrics() {
    const auto statistics = MeasureCpu();
    EmitRow("host_full_reader_time_median",
            "linear",
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            statistics[0].median,
            "ns_per_call");
    EmitRow("host_full_reader_time_iqr",
            "linear",
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            statistics[0].iqr,
            "ns_per_call");
    EmitRow("host_full_reader_time_median",
            "cubic_lagrange",
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            statistics[1].median,
            "ns_per_call");
    EmitRow("host_full_reader_time_iqr",
            "cubic_lagrange",
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            statistics[1].iqr,
            "ns_per_call");
    EmitRow("host_full_reader_ratio",
            "comparison",
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            statistics[1].median / statistics[0].median,
            "ratio");
}

void EmitBoundaryMetrics() {
    const std::array<float, 8> buffer{0.0F, 1.0F, 2.0F, 3.0F,
                                      4.0F, 5.0F, 6.0F, 7.0F};
    const phh::CircularFractionalReader reader(buffer.data(), buffer.size());
    for(const auto policy : {phh::InterpolationPolicy::Linear,
                             phh::InterpolationPolicy::CubicLagrange}) {
        const double expected_error = std::abs(
            static_cast<double>(reader.Read(7, 0.5F, policy)) - 3.5);
        const double period_error = std::abs(static_cast<double>(
            reader.Read(-25, 0.25F, policy)
            - reader.Read(7, 0.25F, policy)));
        EmitRow("boundary_expected_error",
                Name(policy),
                std::nullopt,
                0.5,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                expected_error,
                "FS_abs");
        EmitRow("boundary_period_error",
                Name(policy),
                std::nullopt,
                0.25,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                period_error,
                "FS_abs");
    }
}

} // namespace

int main() {
    std::cout << std::setprecision(12);
    std::cout << "metric,policy,frequency_norm,fraction,mod_depth,passes,"
                 "feedback_gain,value,unit\n";
    EmitStaticErrors();
    EmitMeasuredGainErrors();
    EmitCascadePredictions();
    EmitModulationMetrics();
    EmitCpuMetrics();
    EmitBoundaryMetrics();
    return g_valid ? 0 : 1;
}
