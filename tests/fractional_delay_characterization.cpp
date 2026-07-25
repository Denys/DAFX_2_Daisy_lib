#include "pedal_harness/fractional_delay.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;

std::string_view Name(phh::InterpolationPolicy policy) {
    return policy == phh::InterpolationPolicy::Linear ? "linear" : "cubic_lagrange";
}

std::complex<double> Response(phh::InterpolationPolicy policy,
                              double normalized_frequency,
                              double fraction) {
    const double omega = 2.0 * kPi * normalized_frequency;
    if(policy == phh::InterpolationPolicy::Linear) {
        const double c0 = 1.0 - fraction;
        const double c1 = fraction;
        return c0 + c1 * std::exp(std::complex<double>(0.0, omega));
    }

    const double mu = fraction;
    const std::array<double, 4> coefficients{
        -mu * (mu - 1.0) * (mu - 2.0) / 6.0,
        (mu + 1.0) * (mu - 1.0) * (mu - 2.0) / 2.0,
        -(mu + 1.0) * mu * (mu - 2.0) / 2.0,
        (mu + 1.0) * mu * (mu - 1.0) / 6.0,
    };
    const std::array<int, 4> offsets{-1, 0, 1, 2};

    std::complex<double> response{0.0, 0.0};
    for(std::size_t index = 0; index < coefficients.size(); ++index) {
        response += coefficients[index]
                    * std::exp(std::complex<double>(0.0,
                                                    omega * offsets[index]));
    }
    return response;
}

double StaticRmsError(phh::InterpolationPolicy policy,
                      double normalized_frequency,
                      double fraction) {
    constexpr std::size_t kSize = 8192U;
    std::vector<float> buffer(kSize);
    for(std::size_t index = 0; index < kSize; ++index) {
        buffer[index] = static_cast<float>(
            std::sin(2.0 * kPi * normalized_frequency * static_cast<double>(index)));
    }

    const phh::CircularFractionalReader reader(buffer.data(), buffer.size());
    double energy = 0.0;
    constexpr std::size_t kSamples = 4096U;
    for(std::size_t index = 32U; index < 32U + kSamples; ++index) {
        const double position = static_cast<double>(index) + fraction;
        const double ideal = std::sin(2.0 * kPi * normalized_frequency * position);
        const double actual = reader.Read(static_cast<float>(position), policy);
        const double error = actual - ideal;
        energy += error * error;
    }
    return std::sqrt(energy / static_cast<double>(kSamples));
}

double BinMagnitude(const std::vector<float>& signal,
                    double normalized_frequency) {
    std::complex<double> accumulator{0.0, 0.0};
    for(std::size_t index = 0; index < signal.size(); ++index) {
        const double phase = -2.0 * kPi * normalized_frequency
                             * static_cast<double>(index);
        accumulator += static_cast<double>(signal[index])
                       * std::exp(std::complex<double>(0.0, phase));
    }
    return std::abs(accumulator) / static_cast<double>(signal.size());
}

double ModulatedSidebandDb(phh::InterpolationPolicy policy) {
    constexpr std::size_t kSize = 65536U;
    constexpr double kCarrier = 0.125;
    constexpr double kModulation = 64.0 / static_cast<double>(kSize);
    constexpr double kBaseDelay = 64.5;
    constexpr double kDepth = 0.45;

    std::vector<float> buffer(kSize);
    std::vector<float> output(kSize);
    for(std::size_t index = 0; index < kSize; ++index) {
        buffer[index] = static_cast<float>(
            std::sin(2.0 * kPi * kCarrier * static_cast<double>(index)));
    }

    const phh::CircularFractionalReader reader(buffer.data(), buffer.size());
    for(std::size_t index = 0; index < kSize; ++index) {
        const double modulation =
            kDepth * std::sin(2.0 * kPi * kModulation * static_cast<double>(index));
        const double read_position = static_cast<double>(index) - kBaseDelay - modulation;
        output[index] = reader.Read(static_cast<float>(read_position), policy);
    }

    const double carrier = BinMagnitude(output, kCarrier);
    const double upper = BinMagnitude(output, kCarrier + kModulation);
    const double lower = BinMagnitude(output, kCarrier - kModulation);
    const double sideband = std::max(upper, lower);
    return 20.0 * std::log10(std::max(sideband / carrier, 1.0e-15));
}

double NanosecondsPerRead(phh::InterpolationPolicy policy) {
    constexpr std::size_t kSize = 4096U;
    constexpr std::size_t kIterations = 4000000U;
    std::vector<float> buffer(kSize);
    for(std::size_t index = 0; index < kSize; ++index) {
        buffer[index] = static_cast<float>(std::sin(0.013 * static_cast<double>(index)));
    }
    const phh::CircularFractionalReader reader(buffer.data(), buffer.size());

    volatile float sink = 0.0F;
    const auto begin = std::chrono::steady_clock::now();
    for(std::size_t index = 0; index < kIterations; ++index) {
        const float position = static_cast<float>(index % kSize) + 0.371F;
        sink = sink + reader.Read(position, policy);
    }
    const auto end = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration<double, std::nano>(end - begin).count();
    if(sink == -123456.0F) {
        std::cerr << sink;
    }
    return elapsed / static_cast<double>(kIterations);
}

void EmitStaticError() {
    for(const auto policy : {phh::InterpolationPolicy::Linear,
                             phh::InterpolationPolicy::CubicLagrange}) {
        for(const double frequency : {0.05, 0.15, 0.30, 0.40, 0.45}) {
            for(const double fraction : {0.125, 0.25, 0.5, 0.75, 0.875}) {
                std::cout << "static_error," << Name(policy) << ',' << frequency
                          << ',' << fraction << ','
                          << StaticRmsError(policy, frequency, fraction) << "\n";
            }
        }
    }
}

void EmitRecirculationLoss() {
    for(const auto policy : {phh::InterpolationPolicy::Linear,
                             phh::InterpolationPolicy::CubicLagrange}) {
        for(const double frequency : {0.25, 0.35, 0.40, 0.45}) {
            for(const double fraction : {0.25, 0.5, 0.75}) {
                const double magnitude = std::abs(Response(policy, frequency, fraction));
                for(const int repeats : {1, 8, 32, 64}) {
                    const double db = 20.0 * static_cast<double>(repeats)
                                      * std::log10(std::max(magnitude, 1.0e-15));
                    std::cout << "recirculation_loss_db," << Name(policy) << ','
                              << frequency << ',' << fraction << ',' << repeats
                              << ',' << db << "\n";
                }
            }
        }
    }
}

void EmitSidebandsAndCpu() {
    for(const auto policy : {phh::InterpolationPolicy::Linear,
                             phh::InterpolationPolicy::CubicLagrange}) {
        std::cout << "modulated_sideband_dbc," << Name(policy)
                  << ",0.125,0.45," << ModulatedSidebandDb(policy) << "\n";
        std::cout << "cpu_ns_per_read," << Name(policy) << ",0,0,"
                  << NanosecondsPerRead(policy) << "\n";
    }
}

void EmitBoundaryChecks() {
    const std::array<float, 8> buffer{0.0F, 1.0F, 2.0F, 3.0F,
                                      4.0F, 5.0F, 6.0F, 7.0F};
    const phh::CircularFractionalReader reader(buffer.data(), buffer.size());
    for(const auto policy : {phh::InterpolationPolicy::Linear,
                             phh::InterpolationPolicy::CubicLagrange}) {
        const double mismatch = std::abs(
            static_cast<double>(reader.Read(-0.25F, policy)
                                - reader.Read(7.75F, policy)));
        std::cout << "boundary_wrap_error," << Name(policy)
                  << ",8,-0.25," << mismatch << "\n";
    }
}

} // namespace

int main() {
    std::cout << std::setprecision(10);
    std::cout << "metric,policy,x,y,value\n";
    EmitStaticError();
    EmitRecirculationLoss();
    EmitSidebandsAndCpu();
    EmitBoundaryChecks();
    return 0;
}
