/**
 * @file src/platform/audio.h
 * @brief Audio device boundary, independent of video and host services.
 */
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace platf {
  struct sink_t {
    // Play on host PC
    std::string host;

    // On macOS and Windows, it is not possible to create a virtual sink
    // Therefore, it is optional
    struct null_t {
      std::string stereo;
      std::string surround51;
      std::string surround71;
    };

    std::optional<null_t> null;
  };

  enum class capture_e : int {
    ok,  ///< Success
    reinit,  ///< Need to reinitialize
    timeout,  ///< Timeout
    interrupted,  ///< Capture was interrupted
    error  ///< Error
  };

  class mic_t {
  public:
    virtual capture_e sample(std::vector<float> &frame_buffer) = 0;

    virtual ~mic_t() = default;
  };

  class audio_control_t {
  public:
    virtual int set_sink(const std::string &sink) = 0;

    // Select a loopback endpoint without changing system routing, when supported.
    virtual int set_capture_sink([[maybe_unused]] const std::string &sink) {
      return -1;
    }

    virtual std::unique_ptr<mic_t> microphone(const std::uint8_t *mapping, int channels, std::uint32_t sample_rate, std::uint32_t frame_size, bool continuous, bool host_audio_enabled) = 0;

    /**
     * @brief Check if the audio sink is available in the system.
     * @param sink Sink to be checked.
     * @returns True if available, false otherwise.
     */
    virtual bool is_sink_available(const std::string &sink) = 0;

    virtual std::optional<sink_t> sink_info() = 0;

    /**
     * @brief Restores the host audio sink after streaming stops.
     * @note Most platforms restore a single sink. Windows overrides this to
     *       restore the endpoint captured for each audio role.
     */
    virtual int restore_sink(const std::string &sink) {
      return set_sink(sink);
    }

    /**
     * @brief Resets the default audio device away from virtual streaming speakers.
     * Implementations may continue trying in the background to restore the
     * preferred device after moving the default away from virtual speakers.
     * @param preferred_device Device identifier (platform-specific, e.g. on Windows the
     *        endpoint device_id) of the device that should be restored as default. If
     *        empty, implementations fall back to letting the OS pick whatever non-virtual
     *        device is currently present.
     */
    virtual void reset_default_device([[maybe_unused]] const std::string &preferred_device = {}) {}

    virtual ~audio_control_t() = default;
  };

  // Calliope remote microphone (Apollo fork).

  /// The longest Opus packet is 120 ms, which is 5760 samples at 48 kHz.
  constexpr std::size_t VIRTUAL_MIC_MAX_PACKET_SAMPLES = 5760;

  /**
   * @brief Supplies decoded 48 kHz mono float audio to a virtual microphone, one packet per call.
   * @param mono_out Receives the samples. Holds at least VIRTUAL_MIC_MAX_PACKET_SAMPLES.
   * @return The number of samples written, or 0 when nothing is ready to play.
   *
   * Called only from the virtual microphone's render thread.
   * The callback can run until the virtual microphone's destructor returns, so everything it
   * captures must outlive that destructor.
   */
  using virtual_mic_fill_t = std::function<std::size_t(float *mono_out, std::size_t capacity)>;

  /**
   * @brief A host capture device that plays audio supplied by the host.
   *
   * On Windows this is the Steam Streaming Microphone. Creating one makes it the default
   * capture device. Destroying it stops the render thread and restores the previous default.
   * Create and destroy the object on the same thread. That thread must not be a single-threaded
   * COM apartment on Windows.
   */
  class virtual_mic_t {
  public:
    /**
     * @return false after the device failed and could not be reopened.
     */
    virtual bool healthy() const = 0;

    /**
     * @brief The default capture device id that was active before this object took over.
     */
    virtual std::string previous_default_capture() const = 0;

    virtual ~virtual_mic_t() = default;
  };

  enum class virtual_mic_error_e {
    none,
    unsupported,  ///< This platform has no virtual microphone
    device_missing,  ///< The virtual microphone is not installed
    device_open_failed,  ///< The virtual microphone exists and cannot be opened
    // Appended rather than grouped with device_missing, so the existing values keep their numbers.
    device_hidden_in_session  ///< Installed on the PC, but not visible in the session the host runs in
  };

  /**
   * @brief Open the virtual microphone on the calling thread and start pulling audio from fill.
   * @param error_out Receives the reason when the result is null.
   *
   * Can block for several seconds when the driver is installed.
   */
  std::unique_ptr<virtual_mic_t> virtual_mic(virtual_mic_fill_t fill, virtual_mic_error_e &error_out);

  /**
   * @brief Make the given device the default capture device. Used after a crash during a mic session.
   */
  void restore_default_capture(const std::string &device_id);

}  // namespace platf
