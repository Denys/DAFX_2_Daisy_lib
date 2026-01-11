// # CircularBuffer
// Template-based circular buffer for delay lines
//
// Provides a fixed-size circular buffer with read/write operations
// and linear interpolation for sub-sample delay access.
// Essential for delay-based effects (vibrato, chorus, reverb).
//
// ## Parameters
// - size: Buffer size in samples (must be power of 2 for efficiency)
//
// ## Example
// ~~~~
// CircularBuffer<float, 4096> delay;
// delay.Init();
// delay.Write(input_sample);
// float delayed = delay.Read(1000);  // 1000 sample delay
// float interpolated = delay.ReadInterpolated(500.5f);  // Sub-sample
// ~~~~
//
// ## References
// - DAFX 2nd Ed., Chapter 2, Section 2.4 (Delay Lines)
#pragma once
#ifndef DSY_CIRCULARBUFFER_H
#define DSY_CIRCULARBUFFER_H

#include <cmath>
#include <cstdint>
#include <cstring>


namespace daisysp {

/**
 * @brief Template circular buffer for delay lines
 * @tparam T Sample type (typically float)
 * @tparam MaxSize Maximum buffer size
 */
template <typename T, size_t MaxSize> class CircularBuffer {
public:
  CircularBuffer() : write_ptr_(0), size_(MaxSize) {}
  ~CircularBuffer() {}

  /**
   * @brief Initialize the buffer
   * @param size Actual size to use (must be <= MaxSize)
   */
  void Init(size_t size = MaxSize) {
    size_ = (size <= MaxSize) ? size : MaxSize;
    write_ptr_ = 0;
    Clear();
  }

  /**
   * @brief Clear the buffer (zero all samples)
   */
  void Clear() { std::memset(buffer_, 0, sizeof(buffer_)); }

  /**
   * @brief Write a sample to the buffer and advance write pointer
   * @param sample Sample to write
   */
  inline void Write(T sample) {
    buffer_[write_ptr_] = sample;
    write_ptr_ = (write_ptr_ + 1) % size_;
  }

  /**
   * @brief Read a sample at a fixed delay (no interpolation)
   * @param delay_samples Delay in samples (integer)
   * @return Delayed sample
   */
  inline T Read(size_t delay_samples) const {
    if (delay_samples >= size_)
      delay_samples = size_ - 1;

    size_t read_ptr = (write_ptr_ + size_ - delay_samples) % size_;
    return buffer_[read_ptr];
  }

  /**
   * @brief Read with linear interpolation for sub-sample delays
   * @param delay_samples Delay in samples (fractional)
   * @return Interpolated sample
   */
  inline T ReadInterpolated(float delay_samples) const {
    if (delay_samples < 0.0f)
      delay_samples = 0.0f;
    if (delay_samples >= static_cast<float>(size_ - 1))
      delay_samples = static_cast<float>(size_ - 2);

    size_t delay_int = static_cast<size_t>(delay_samples);
    float frac = delay_samples - static_cast<float>(delay_int);

    T sample1 = Read(delay_int);
    T sample2 = Read(delay_int + 1);

    return sample1 + frac * (sample2 - sample1);
  }

  /**
   * @brief Read with cubic interpolation for higher quality
   * @param delay_samples Delay in samples (fractional)
   * @return Interpolated sample (cubic Hermite)
   */
  inline T ReadCubic(float delay_samples) const {
    if (delay_samples < 1.0f)
      delay_samples = 1.0f;
    if (delay_samples >= static_cast<float>(size_ - 2))
      delay_samples = static_cast<float>(size_ - 3);

    size_t delay_int = static_cast<size_t>(delay_samples);
    float frac = delay_samples - static_cast<float>(delay_int);

    T y0 = Read(delay_int - 1);
    T y1 = Read(delay_int);
    T y2 = Read(delay_int + 1);
    T y3 = Read(delay_int + 2);

    // Cubic Hermite interpolation
    T c0 = y1;
    T c1 = 0.5f * (y2 - y0);
    T c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    T c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

    return ((c3 * frac + c2) * frac + c1) * frac + c0;
  }

  /**
   * @brief Read from a specific tap position
   * @param tap_index Tap index (0 = most recent)
   * @return Sample at tap
   */
  inline T Tap(size_t tap_index) const { return Read(tap_index); }

  /**
   * @brief Get the current write position
   * @return Write pointer index
   */
  inline size_t GetWritePtr() const { return write_ptr_; }

  /**
   * @brief Get the buffer size
   * @return Current buffer size
   */
  inline size_t GetSize() const { return size_; }

  /**
   * @brief Direct access to buffer (for advanced use)
   * @param index Buffer index
   * @return Reference to sample at index
   */
  inline T &operator[](size_t index) { return buffer_[index % size_]; }
  inline const T &operator[](size_t index) const {
    return buffer_[index % size_];
  }

private:
  T buffer_[MaxSize];
  size_t write_ptr_;
  size_t size_;
};

/**
 * @brief Runtime-sized circular buffer (heap allocated)
 * @tparam T Sample type
 */
template <typename T> class DynamicCircularBuffer {
public:
  DynamicCircularBuffer() : buffer_(nullptr), write_ptr_(0), size_(0) {}

  ~DynamicCircularBuffer() {
    if (buffer_ != nullptr) {
      delete[] buffer_;
      buffer_ = nullptr;
    }
  }

  /**
   * @brief Initialize with specified size
   * @param size Buffer size in samples
   */
  void Init(size_t size) {
    if (buffer_ != nullptr)
      delete[] buffer_;

    size_ = size;
    buffer_ = new T[size_];
    write_ptr_ = 0;
    Clear();
  }

  void Clear() {
    if (buffer_ != nullptr)
      std::memset(buffer_, 0, size_ * sizeof(T));
  }

  inline void Write(T sample) {
    buffer_[write_ptr_] = sample;
    write_ptr_ = (write_ptr_ + 1) % size_;
  }

  inline T Read(size_t delay_samples) const {
    if (delay_samples >= size_)
      delay_samples = size_ - 1;

    size_t read_ptr = (write_ptr_ + size_ - delay_samples) % size_;
    return buffer_[read_ptr];
  }

  inline T ReadInterpolated(float delay_samples) const {
    if (delay_samples < 0.0f)
      delay_samples = 0.0f;
    if (delay_samples >= static_cast<float>(size_ - 1))
      delay_samples = static_cast<float>(size_ - 2);

    size_t delay_int = static_cast<size_t>(delay_samples);
    float frac = delay_samples - static_cast<float>(delay_int);

    T sample1 = Read(delay_int);
    T sample2 = Read(delay_int + 1);

    return sample1 + frac * (sample2 - sample1);
  }

  inline size_t GetSize() const { return size_; }

private:
  T *buffer_;
  size_t write_ptr_;
  size_t size_;
};

} // namespace daisysp

#endif // DSY_CIRCULARBUFFER_H
