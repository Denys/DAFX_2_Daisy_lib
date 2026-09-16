// Regression tests for the memory-safety defects found in the port audit
// (docs/PORT_AUDIT_2026-09-16.md, work-order items 1-4).
//
// Each test names the defect it pins. Several of these paths are undefined
// behaviour before the fix rather than a wrong value, so the assertions here
// are a tripwire: the tests are written to be run under AddressSanitizer,
// UndefinedBehaviorSanitizer and LeakSanitizer as well as plain, and it is the
// sanitizer run that carries the real signal.

#include "effects/lp_iir_comb.h"
#include "effects/universal_comb.h"
#include "modulation/vibrato.h"
#include "spatial/crosstalk_canceller.h"
#include "utility/circularbuffer.h"

#include <cmath>
#include <gtest/gtest.h>
#include <limits>
#include <type_traits>

using namespace daisysp;

// ---------------------------------------------------------------------------
// 2.21 - delay_frac_ was initialised in neither constructor nor Init(), so
// ProcessFractional() computed a read position from an indeterminate float and
// then cast the NaN to size_t to index the delay buffer.
// ---------------------------------------------------------------------------

TEST(MemorySafetyCombs, LPIIRCombFractionalIsFiniteAfterInitOnly) {
  LPIIRComb<4096> comb;
  comb.Init(48000.0f);
  for (int i = 0; i < 256; ++i) {
    float out = comb.ProcessFractional(i == 0 ? 1.0f : 0.0f);
    ASSERT_TRUE(std::isfinite(out)) << "non-finite output at sample " << i;
  }
}

// Deterministic form of the same defect. Indeterminate storage often reads as
// zero, so "is the output finite" can pass by luck; this asserts the invariant
// the fix establishes - after Init(), the fractional delay is the integer one.
// Pre-fix on zeroed storage delay_frac_ was 0 against a delay of 100, so the
// two paths read different taps and this fails.
TEST(MemorySafetyCombs, LPIIRCombFractionalMatchesIntegerAfterInitAlone) {
  LPIIRComb<4096> integer_path, fractional_path;
  integer_path.Init(48000.0f);
  fractional_path.Init(48000.0f);
  for (int i = 0; i < 512; ++i) {
    const float x = (i == 0) ? 1.0f : 0.0f;
    ASSERT_FLOAT_EQ(integer_path.Process(x), fractional_path.ProcessFractional(x))
        << "integer and fractional paths diverge at sample " << i;
  }
}

TEST(MemorySafetyCombs, LPIIRCombFractionalIsFiniteOnDefaultConstruction) {
  LPIIRComb<4096> comb; // no Init() at all
  for (int i = 0; i < 64; ++i) {
    ASSERT_TRUE(std::isfinite(comb.ProcessFractional(0.25f)));
  }
}

TEST(MemorySafetyCombs, UniversalCombFractionalIsFiniteAfterInitOnly) {
  UniversalComb<2048> comb;
  comb.Init(48000.0f);
  for (int i = 0; i < 256; ++i) {
    float out = comb.ProcessFractional(i == 0 ? 1.0f : 0.0f);
    ASSERT_TRUE(std::isfinite(out)) << "non-finite output at sample " << i;
  }
}

// UniversalComb exposes the member directly, so the invariant can be asserted
// without going through the audio path. Pre-fix this read indeterminate
// storage; on a zeroed page that is 0.0f against a delay of 10.
TEST(MemorySafetyCombs, UniversalCombFractionalDelayTracksIntegerAfterInit) {
  UniversalComb<2048> comb;
  comb.Init(48000.0f);
  EXPECT_FLOAT_EQ(comb.GetDelayFractional(),
                  static_cast<float>(comb.GetDelay()));
}

TEST(MemorySafetyCombs, UniversalCombFractionalIsFiniteOnDefaultConstruction) {
  UniversalComb<2048> comb; // no Init() at all
  for (int i = 0; i < 64; ++i) {
    ASSERT_TRUE(std::isfinite(comb.ProcessFractional(0.25f)));
  }
}

TEST(MemorySafetyCombs, UniversalCombFractionalMatchesIntegerAtWholeDelays) {
  // The fractional path should agree with the integer path when the
  // fractional delay lands exactly on a sample. This pins delay_frac_ to the
  // value SetDelay() implies rather than merely to "something finite".
  UniversalComb<2048> a, b;
  a.Init(48000.0f);
  b.Init(48000.0f);
  a.SetDelay(64);
  b.SetDelay(64);
  b.SetDelayFractional(64.0f);
  for (int i = 0; i < 512; ++i) {
    const float x = (i == 0) ? 1.0f : 0.0f;
    EXPECT_FLOAT_EQ(a.Process(x), b.ProcessFractional(x))
        << "integer and fractional paths diverge at sample " << i;
  }
}

// ---------------------------------------------------------------------------
// 2.6 - Init(0) was accepted by both buffer classes. The next Write() then
// divided by zero in the fixed-size class, and in the dynamic class wrote
// through buffer_[0] of a zero-length allocation before reaching the modulo.
// ---------------------------------------------------------------------------

TEST(MemorySafetyBuffers, FixedSizeInitZeroIsSurvivable) {
  CircularBuffer<float, 128> cb;
  cb.Init(0);
  EXPECT_GE(cb.GetSize(), 1u);
  cb.Write(1.0f);
  EXPECT_TRUE(std::isfinite(cb.Read(0)));
}

TEST(MemorySafetyBuffers, DynamicInitZeroIsSurvivable) {
  DynamicCircularBuffer<float> db;
  db.Init(0);
  EXPECT_GE(db.GetSize(), 1u);
  db.Write(1.0f);
  EXPECT_TRUE(std::isfinite(db.Read(0)));
}

// 2.6 - the dynamic buffer owns a raw allocation with a destructor and had no
// rule of three, so a copy was a double free.
TEST(MemorySafetyBuffers, DynamicBufferIsNotCopyable) {
  static_assert(!std::is_copy_constructible<DynamicCircularBuffer<float>>(),
                "copying would give two owners of one allocation");
  static_assert(!std::is_copy_assignable<DynamicCircularBuffer<float>>(),
                "copy assignment would double-free and leak");
  SUCCEED();
}

TEST(MemorySafetyBuffers, DynamicBufferMoveTransfersOwnership) {
  DynamicCircularBuffer<float> src;
  src.Init(8);
  for (int i = 1; i <= 8; ++i) {
    src.Write(static_cast<float>(i));
  }

  DynamicCircularBuffer<float> dst(std::move(src));
  EXPECT_EQ(dst.GetSize(), 8u);
  EXPECT_EQ(src.GetSize(), 0u); // NOLINT - moved-from state is asserted here

  DynamicCircularBuffer<float> assigned;
  assigned.Init(4);
  assigned = std::move(dst);
  EXPECT_EQ(assigned.GetSize(), 8u);
}

// ---------------------------------------------------------------------------
// 2.15 - Vibrato. Four independent defects: the SetWidth heap overflow, the
// uninitialised delay_line_, the absent rule of three, and the Init() leak.
// ---------------------------------------------------------------------------

TEST(MemorySafetyVibrato, WidestDocumentedWidthDoesNotOverflow) {
  // vibrato.h documents 0.0001-0.1 s. Before the fix, any width larger than
  // the one in effect at Init() grew the logical delay length past the
  // allocation and Process() ran off the end of the heap block.
  Vibrato v;
  v.Init(48000.0f);
  v.SetWidth(0.1f);
  for (int i = 0; i < 8192; ++i) {
    float out = v.Process(0.5f * std::sin(0.01f * static_cast<float>(i)));
    ASSERT_TRUE(std::isfinite(out)) << "non-finite output at sample " << i;
  }
}

TEST(MemorySafetyVibrato, WidthIsClampedToTheAllocatedRange) {
  Vibrato v;
  v.Init(48000.0f);
  v.SetWidth(10.0f); // far outside the documented range
  EXPECT_FLOAT_EQ(v.GetWidth(), Vibrato::kMaxWidthSeconds);
  EXPECT_TRUE(std::isfinite(v.Process(0.5f)));

  v.SetWidth(-1.0f);
  EXPECT_FLOAT_EQ(v.GetWidth(), 0.0f);
  EXPECT_TRUE(std::isfinite(v.Process(0.5f)));
}

TEST(MemorySafetyVibrato, ProcessBeforeInitIsSafe) {
  Vibrato v; // delay_line_ was indeterminate here before the fix
  EXPECT_FLOAT_EQ(v.Process(1.0f), 0.0f);
}

TEST(MemorySafetyVibrato, DestroyingANeverInitialisedInstanceIsSafe) {
  { Vibrato v; } // the destructor used to delete[] an indeterminate pointer
  SUCCEED();
}

TEST(MemorySafetyVibrato, RepeatedInitDoesNotLeak) {
  // tests/test_vibrato.cpp DifferentSampleRates already walks this path.
  // LeakSanitizer reported one abandoned allocation per extra Init().
  Vibrato v;
  v.Init(48000.0f);
  v.Init(44100.0f);
  v.Init(96000.0f);
  v.Init(48000.0f);
  EXPECT_TRUE(std::isfinite(v.Process(0.5f)));
}

TEST(MemorySafetyVibrato, IsNotCopyable) {
  static_assert(!std::is_copy_constructible<Vibrato>(),
                "copying would give two owners of one delay line");
  static_assert(!std::is_copy_assignable<Vibrato>(),
                "copy assignment would double-free and leak");
  SUCCEED();
}

// ---------------------------------------------------------------------------
// 2.9 - CrosstalkCanceller. Generate() zeroes only GetLength() samples while
// the caller copies the full HRIR_LENGTH, so indeterminate stack floats
// reached the FFT and the inverse filters differed run to run.
//
// This is only partially observable from outside: indeterminate stack contents
// may happen to repeat, so a pass here is necessary but not sufficient. The
// guarantee comes from the zero-initialisation itself; this test exists to
// catch a regression that removes it *and* changes the result.
// ---------------------------------------------------------------------------

TEST(MemorySafetyCrosstalk, TwoInstancesAgreeSampleForSample) {
  auto run = [](float *out, size_t n) {
    CrosstalkCanceller<256> ctc;
    ctc.Init(48000.0f);
    ctc.SetSpeakerAngle(12.0f);
    for (size_t i = 0; i < n; ++i) {
      float l, r;
      const float x = std::sin(0.02f * static_cast<float>(i));
      ctc.Process(x, -x, &l, &r);
      out[i] = l;
    }
  };

  constexpr size_t kN = 1024;
  static float a[kN], b[kN];
  run(a, kN);
  run(b, kN);
  for (size_t i = 0; i < kN; ++i) {
    ASSERT_FLOAT_EQ(a[i], b[i]) << "output not reproducible at sample " << i;
    ASSERT_TRUE(std::isfinite(a[i]));
  }
}

// ---------------------------------------------------------------------------
// The class behind several of the defects above is a non-finite float reaching
// an integer index: the cast is undefined, in practice yielding INT_MIN or
// 2^63, and the buffer access that follows is out of bounds. These cover the
// degenerate public arguments that reach it. Each one raised an ASan
// heap-buffer-overflow or a UBSan out-of-bounds index before the guards.
// ---------------------------------------------------------------------------

TEST(MemorySafetyNonFinite, VibratoSurvivesZeroSampleRate) {
  Vibrato v;
  v.Init(0.0f); // makes mod_freq_samples_ infinite, then the tap NaN
  for (int i = 0; i < 4096; ++i) {
    ASSERT_TRUE(std::isfinite(v.Process(0.5f))) << "at sample " << i;
  }
}

TEST(MemorySafetyNonFinite, VibratoSurvivesNonFiniteFrequency) {
  Vibrato v;
  v.Init(48000.0f);
  v.SetFrequency(std::nanf(""));
  for (int i = 0; i < 4096; ++i) {
    ASSERT_TRUE(std::isfinite(v.Process(0.5f))) << "at sample " << i;
  }
}

TEST(MemorySafetyNonFinite, VibratoSurvivesNonFiniteWidth) {
  Vibrato v;
  v.Init(48000.0f);
  v.SetWidth(std::nanf(""));
  for (int i = 0; i < 4096; ++i) {
    ASSERT_TRUE(std::isfinite(v.Process(0.5f))) << "at sample " << i;
  }
}

TEST(MemorySafetyNonFinite, UniversalCombSurvivesNonFiniteFractionalDelay) {
  UniversalComb<2048> comb;
  comb.Init(48000.0f);
  comb.SetDelayFractional(std::nanf(""));
  EXPECT_TRUE(std::isfinite(comb.GetDelayFractional()));
  for (int i = 0; i < 256; ++i) {
    ASSERT_TRUE(std::isfinite(comb.ProcessFractional(0.5f))) << "at " << i;
  }
}

TEST(MemorySafetyNonFinite, CombsSurviveNonFiniteDelayInMilliseconds) {
  UniversalComb<2048> uc;
  uc.Init(48000.0f);
  uc.SetDelayMs(std::nanf(""));
  for (int i = 0; i < 256; ++i) {
    ASSERT_TRUE(std::isfinite(uc.ProcessFractional(0.5f))) << "at " << i;
  }

  LPIIRComb<4096> lp;
  lp.Init(48000.0f);
  lp.SetDelayMs(std::nanf(""));
  for (int i = 0; i < 256; ++i) {
    ASSERT_TRUE(std::isfinite(lp.ProcessFractional(0.5f))) << "at " << i;
  }
}

// ---------------------------------------------------------------------------
// Second review round on this PR. Four findings, each reproduced before being
// fixed; three of them were faults introduced or left open by the first round
// of fixes rather than by the original code.
// ---------------------------------------------------------------------------

// Init() used to assign delay_frac_ = delay_samples_, which truncated a delay
// configured through SetDelayFractional(). The constructor already removes the
// indeterminacy that assignment was added for.
TEST(MemorySafetyCombs, InitPreservesAConfiguredFractionalDelay) {
  UniversalComb<2048> comb;
  comb.Init(48000.0f);
  comb.SetDelayFractional(64.75f);
  ASSERT_FLOAT_EQ(comb.GetDelayFractional(), 64.75f);

  comb.Init(48000.0f); // reset state, change nothing configured
  EXPECT_FLOAT_EQ(comb.GetDelayFractional(), 64.75f);
}

TEST(MemorySafetyCombs, InitPreservesADelaySetInMilliseconds) {
  LPIIRComb<4096> comb;
  comb.Init(48000.0f);
  comb.SetDelayMs(1.5f); // 72 samples at 48 kHz
  const size_t configured = comb.GetDelay();
  ASSERT_EQ(configured, 72u);

  comb.Init(48000.0f);
  EXPECT_EQ(comb.GetDelay(), configured);
}

// The nominal default delay (10 for UniversalComb, 100 for LPIIRComb) does not
// fit a small instantiation. ProcessFractional() then computed a negative read
// position, which is finite - so the finiteness guard accepted it - and casting
// it to size_t is undefined before an out-of-bounds read:
//   universal_comb.h: runtime error: -6 is outside the range of
//       representable values of type 'long unsigned int'
TEST(MemorySafetyCombs, SmallUniversalCombClampsItsDefaultDelay) {
  UniversalComb<4> comb;
  comb.Init(48000.0f);
  EXPECT_LE(comb.GetDelay(), 3u);
  EXPECT_LE(comb.GetDelayFractional(), 3.0f);
  for (int i = 0; i < 64; ++i) {
    ASSERT_TRUE(std::isfinite(comb.ProcessFractional(0.5f))) << "at " << i;
  }
}

TEST(MemorySafetyCombs, SmallLPIIRCombClampsItsDefaultDelay) {
  LPIIRComb<8> comb;
  comb.Init(48000.0f);
  EXPECT_LE(comb.GetDelay(), 7u);
  for (int i = 0; i < 64; ++i) {
    ASSERT_TRUE(std::isfinite(comb.ProcessFractional(0.5f))) << "at " << i;
  }
}

// SetDelayMs() converted to size_t before validating. Casting NaN, an infinity
// or a negative is undefined in the setter itself, which no guard in
// ProcessFractional() can repair. Run this under -fsanitize=float-cast-overflow.
TEST(MemorySafetyNonFinite, SetDelayMsValidatesBeforeConverting) {
  const float bad[] = {std::nanf(""), -1.0f, -0.0f / 0.0f,
                       std::numeric_limits<float>::infinity(),
                       -std::numeric_limits<float>::infinity(), 1.0e9f};
  for (float ms : bad) {
    UniversalComb<2048> uc;
    uc.Init(48000.0f);
    uc.SetDelayMs(ms);
    EXPECT_LT(uc.GetDelay(), 2048u);
    EXPECT_TRUE(std::isfinite(uc.GetDelayFractional()));
    EXPECT_TRUE(std::isfinite(uc.ProcessFractional(0.5f)));

    LPIIRComb<4096> lp;
    lp.Init(48000.0f);
    lp.SetDelayMs(ms);
    EXPECT_LT(lp.GetDelay(), 4096u);
    EXPECT_TRUE(std::isfinite(lp.ProcessFractional(0.5f)));
  }
}

// The delay buffers were left indeterminate by the constructors, so the
// default-construction tests above asserted a safety the code did not provide:
// ProcessFractional() read uninitialised floats whatever the index did.
TEST(MemorySafetyCombs, DefaultConstructedCombsHaveAZeroedDelayLine) {
  UniversalComb<64> uc; // no Init()
  for (int i = 0; i < 256; ++i) {
    ASSERT_TRUE(std::isfinite(uc.ProcessFractional(0.0f))) << "at " << i;
  }
  LPIIRComb<128> lp; // no Init()
  for (int i = 0; i < 256; ++i) {
    ASSERT_TRUE(std::isfinite(lp.ProcessFractional(0.0f))) << "at " << i;
  }
}
