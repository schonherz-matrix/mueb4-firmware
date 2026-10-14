/**
 * @file
 * @author Ádám Kiss
 * @author Zsombor Bodnár
 */

#ifndef MUEB4_FIRMWARE_CORE_INC_PANEL_H_
#define MUEB4_FIRMWARE_CORE_INC_PANEL_H_

#include <array>
#include <cstdint>
#include <span>

#include "main.h"

/// Manages all panel related functionality.
class Panel final {
 public:
  /// #Panel state.
  enum class State : std::uint8_t {
    kDisabled = 0x00u,  ///< Panel is disabled
    kPowerOff = 0x01u,  ///< 12v, 3v3 are off and capacitors are discharging
    kVcc3v3On = 0x02u,  ///< 3v3 is on
    kVcc12vOn = 0x03u   ///< 12v is on
  };

  /**
   * Stores which side the panel is on viewed from outside.
   * @see #SwapWindows
   */
  enum class Side { LEFT, RIGHT };

  /// Stores pixel amount on one panel.
  static constexpr std::uint8_t kPixelCount{4u};

  /// Stores pixels size in bytes.
  static constexpr std::uint8_t kColorDataSize{kPixelCount * 3u};

  /// Stores the color values of the panel pixels.
  using ColorData = std::array<std::uint8_t, kColorDataSize>;

  /// Stores white balance data size.
  static constexpr std::uint8_t kWhiteBalanceDataSize{45u};

  /// Stores white balance configuration data.
  using WhiteBalanceData = std::array<std::uint8_t, kWhiteBalanceDataSize>;

  static constexpr std::uint8_t kInitCommand{0xF0u};

  static constexpr std::uint8_t kConfigCommand{0xE0u};

  Panel(const Panel&) = delete;
  Panel& operator=(const Panel&) = delete;

  /**
   * Returns left #Panel instance independently of #swapped_.
   * @see #LeftPanel
   * @return Left #Panel instance
   */
  static Panel& left_panel();

  /**
   * Returns right #Panel instance independently of #swapped_.
   * @see #RightPanel
   * @return Right #Panel instance
   */
  static Panel& right_panel();

  /**
   * This function returns a #Panel instance using #swapped_.
   * Can be used to get the correct panel even if the cables are swapped.
   * @warning You should always use this function if the position of the panel.
   * matters
   * @see #Side
   * @see network#SwapWindows
   * @param side Which panel to get viewed from the outside
   * @return Panel instance viewed from the outside
   */
  static Panel& GetPanel(Side side);

  static Panel& GetPanel(const UART_HandleTypeDef* huartx);

  /// Panel class' loop
  void Step();

  /// Handles internal animation events flagged by interrupts.
  static void StepInternalAnimation();

  /**
   * Increase #tick_1s_ by 1 and request an internal animation step.
   * Called from interrupt.
   * @see ::TIM6_IRQHandler
   */
  static void TimeHandler();

  /**
   * Used for GPIO USER_INPUT_BUTTON, called from interrupt.
   * @see ::EXTI2_3_IRQHandler
   */
  static void RequestInternalAnimationToggle();

  static void ToggleInternalAnimation();

  static void SetInternalAnimation(bool on);

  static bool internal_animation_enabled();

  /**
   * Swap panels.
   * @see #swapped_ network#kSwapPanels
   */
  static void Swap();

  /**
   * Blanks the panel.
   * @see Network#kBlank
   */
  void Blank();

  void Disable();

  void SendColorData();

  /// Send white balance data to panel.
  void SendWhiteBalance(const WhiteBalanceData& white_balance);

  Panel& SetColorData(std::span<const std::uint8_t> data, bool _8bit);

  /// Called from UART RX interrupt, Step() handles the heartbeat.
  void Heartbeat();

  [[nodiscard]] State state() const;

  static std::array<std::uint8_t, 2> GetPanelStates();

 private:
  /// ADC conversion timeout in ms, a conversion takes a few µs.
  static constexpr std::uint32_t kAdcTimeout{10u};

  explicit Panel(Side side);
  ~Panel() = default;

  /// Internal animation's next frame
  static void SendInternalAnimationFrame();

  /// Checks the received heartbeat and restarts reception.
  void HandleHeartbeat();

  static std::array<std::uint32_t, 2u> adc_;

  /// Stores if the internal animation is enabled.
  static volatile bool internal_animation_enabled_;

  /// Set by the 1s timer interrupt.
  static volatile bool internal_animation_step_requested_;

  /// Set by the user button interrupt.
  static volatile bool internal_animation_toggle_requested_;

  /// Stores if left and right panels are swapped.
  static bool swapped_;

  std::array<std::uint8_t, kWhiteBalanceDataSize + 1u> white_balance_data_{
      kConfigCommand};

  /// DMA TX buffer.
  std::array<std::uint8_t, kColorDataSize + 1u> color_data_{kInitCommand};

  GPIO_TypeDef* const gpio_3v3_port_;
  GPIO_TypeDef* const gpio_12v_port_;
  UART_HandleTypeDef* const huartx_;

  /// Stores state of panel @see #State.
  State state_{State::kPowerOff};

  const Side side_;

  const std::uint16_t gpio_3v3_pin_;

  const std::uint16_t gpio_12v_pin_;

  /**
   * Counts the seconds.
   * Used to countdown the timeout in panel probing.
   */
  volatile std::uint8_t tick_1s_{0u};

  std::uint8_t heartbeat_{0u};

  /// Set by UART RX interrupt when #heartbeat_ is received.
  volatile bool heartbeat_received_{false};

  /// Stores if we can communicate with the panel.
  volatile bool active_{false};
};

#endif  // MUEB4_FIRMWARE_CORE_INC_PANEL_H_
