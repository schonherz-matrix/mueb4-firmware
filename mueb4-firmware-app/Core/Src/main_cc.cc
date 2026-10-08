/**
 * This file contains the c++ main loop
 * @file
 */

#include "network.h"
#include "panel.h"
#include "tim.h"

/**
 * C++ main loop.
 * Can be called from C.
 */
extern "C" [[noreturn]] void Main() {
  auto& network{Network::Instance()};
  auto& left_panel{Panel::left_panel()};
  auto& right_panel{Panel::right_panel()};

  HAL_TIM_Base_Start_IT(&htim6);

  while (true) {
    network.Step();
    Panel::StepInternalAnimation();
    left_panel.Step();
    right_panel.Step();
  }
}
