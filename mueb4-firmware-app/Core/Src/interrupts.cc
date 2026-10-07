#include <dhcp.h>

#include "main.h"
#include "network.h"
#include "panel.h"
#include "tim.h"

extern volatile std::uint8_t bootloader_firmware_update_timeout;

extern "C" {
void HAL_GPIO_EXTI_Callback(std::uint16_t GPIO_Pin) {
  // User button
  if (GPIO_Pin == GPIO_PIN_2) {
    Panel::ToggleInternalAnimation();
  }
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
  // 1s timer
  if (htim == &htim6) {
    HAL_GPIO_TogglePin(LED_HEART_GPIO_Port, LED_HEART_Pin);
    DHCP_time_handler();
    Panel::TimeHandler();
    bootloader_firmware_update_timeout++;
  } else if (htim == &htim16) {
    Network::Instance().StreamTerminated();
  } else if (htim == &htim17 || htim == &htim14) {
    Network::Instance().SyncTimedOut();
  }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
  Panel::GetPanel(huart).Heartbeat();
}
}
