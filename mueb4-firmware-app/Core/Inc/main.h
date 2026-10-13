/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f0xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define ART_NET_SYNC_LOST 3999
#define PRESCALER 45000 - 1
#define SECOND 1000 - 1
#define E131_NETWORK_DATA_LOSS_TIMEOUT 2500 - 1
#define PANEL_12V_LEFT_Pin GPIO_PIN_13
#define PANEL_12V_LEFT_GPIO_Port GPIOC
#define PANEL_ADC_3V3_LEFT_Pin GPIO_PIN_0
#define PANEL_ADC_3V3_LEFT_GPIO_Port GPIOA
#define PANEL_ADC_3V3_RIGHT_Pin GPIO_PIN_1
#define PANEL_ADC_3V3_RIGHT_GPIO_Port GPIOA
#define PANEL_TX_LEFT_Pin GPIO_PIN_2
#define PANEL_TX_LEFT_GPIO_Port GPIOA
#define PANEL_RX_LEFT_Pin GPIO_PIN_3
#define PANEL_RX_LEFT_GPIO_Port GPIOA
#define SPI1_NSS_Pin GPIO_PIN_4
#define SPI1_NSS_GPIO_Port GPIOA
#define W5500_INTN_Pin GPIO_PIN_0
#define W5500_INTN_GPIO_Port GPIOB
#define W5500_RSTN_Pin GPIO_PIN_1
#define W5500_RSTN_GPIO_Port GPIOB
#define USER_INPUT_BUTTON_Pin GPIO_PIN_2
#define USER_INPUT_BUTTON_GPIO_Port GPIOB
#define USER_INPUT_BUTTON_EXTI_IRQn EXTI2_3_IRQn
#define LED_JOKER_Pin GPIO_PIN_12
#define LED_JOKER_GPIO_Port GPIOB
#define LED_SERVER_Pin GPIO_PIN_13
#define LED_SERVER_GPIO_Port GPIOB
#define LED_DHCP_Pin GPIO_PIN_14
#define LED_DHCP_GPIO_Port GPIOB
#define LED_HEART_Pin GPIO_PIN_15
#define LED_HEART_GPIO_Port GPIOB
#define PANEL_TX_RIGHT_Pin GPIO_PIN_9
#define PANEL_TX_RIGHT_GPIO_Port GPIOA
#define PANEL_RX_RIGHT_Pin GPIO_PIN_10
#define PANEL_RX_RIGHT_GPIO_Port GPIOA
#define PANEL_12V_RIGHT_Pin GPIO_PIN_11
#define PANEL_12V_RIGHT_GPIO_Port GPIOA
#define PANEL_3V3_RIGHT_Pin GPIO_PIN_8
#define PANEL_3V3_RIGHT_GPIO_Port GPIOB
#define PANEL_3V3_LEFT_Pin GPIO_PIN_9
#define PANEL_3V3_LEFT_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
