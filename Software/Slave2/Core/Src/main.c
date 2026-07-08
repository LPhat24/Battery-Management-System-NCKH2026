/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "math.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

#define CAN_ID_SLAVE1_TX        0x101U
#define CAN_ID_SLAVE2_TX        0x102U
#define CAN_ID_SLAVE3_TX        0x103U
#define CAN_ID_SLAVE1_RX        0x200U
#define CAN_ID_SLAVE2_RX        0x201U
#define CAN_ID_SLAVE3_RX        0x202U

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;

CAN_HandleTypeDef hcan;

TIM_HandleTypeDef htim2;

/* USER CODE BEGIN PV */

volatile uint8_t balance_mask = 0x00; 

volatile uint16_t adc_val[6];
volatile uint8_t  adc_ready = 0;
volatile uint8_t  tim_flag  = 0;

float    temperature  = 0.0f;
uint32_t ntc_ema      = 0;
uint8_t  ntc_ema_init = 0;   

uint16_t adc_buf[6];

uint16_t voltage_mV[5];
uint16_t tapVolt[5];


/* moving-average buffers removed (no filtering) */

uint8_t bal_state[5] = {0};
uint8_t temp_cutoff  = 0;

CAN_TxHeaderTypeDef TxHeader;
uint32_t            TxMailbox;
uint8_t             TxData[8];

volatile uint32_t tx_count = 0;
volatile uint32_t can_error = 0;
volatile uint8_t  balance_enable = 1;

CAN_FilterTypeDef sFilterConfig;
CAN_RxHeaderTypeDef RxHeader;
uint8_t RxData[8];

/* EMA buffers removed (no filtering) */
uint16_t voltage_mV_f[5]  = {0};  

// Per-cell calibration factors (real / measured).
// These were computed from a one-shot calibration and can be adjusted.
// Cell order: 1..5
// Updated cell 1 calibration: actual 4011mV / measured 4015mV
// Computed factor = 4011.0 / 4015.0 = 0.99900348
// Updated cell 2 calibration: actual 3992mV / measured 3975mV
// Computed factor = 3992.0 / 3975.0 = 1.004278
/* Per-cell calibration factors (real / measured) - cell1 updated from measurement */
/* Reset calibration factors to 1.0 for re-calibration start */
float calib_factor[5] = {1.0342536f, 1.0135508f, 1.3042642f, 0.9725500f, 1.1813139f};
/* calibration/results storage */

/* ADC / divider constants */
#define ADC_MAX_VALUE        4095u
#define ADC_CHANNEL_COUNT    6u
#define CELL_COUNT           5u
/* number of samples to average per channel */
#define AVG_SAMPLE_COUNT     500u
/* downsample factor: only accumulate every SAMPLE_SKIPth ADC-ready event */
/* increase to reduce effective sampling rate */
#define SAMPLE_SKIP          4u
/* simple accumulators for AVG_SAMPLE_COUNT averaging */
static uint32_t avg_sum[CELL_COUNT] = {0};
static uint16_t avg_count[CELL_COUNT] = {0};
static uint16_t last_avg_adc[CELL_COUNT] = {0};
static uint16_t sample_skip_idx = 0;
/* measured VREF on board in millivolts (3.3V rail actual) */
static const uint32_t VREF_MV = 3300u; /* adjusted to 3.300V */

/* Divider resistor values in ohms for each tap (R1 = top resistor, R2 = bottom to GND)
  Tap 1 = top of cell1, Tap 2 = top of cell2, ... Tap5 = top of cell5
  Values provided by hardware design */
static const uint32_t divider_R1[CELL_COUNT] = {100000u, 200000u, 300000u, 500000u, 860000u};
static const uint32_t divider_R2[CELL_COUNT] = {100000u, 100000u, 100000u, 100000u, 100000u};

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_ADC1_Init(void);
static void MX_TIM2_Init(void);
static void MX_CAN_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* Convert raw ADC value for a given tap index to tap voltage in mV */
static uint16_t adc_raw_to_tap_mv(uint16_t raw_adc, uint8_t tap_index)
{
  /* convert ADC code to measured millivolts
     use measured VREF to reduce systematic error */
  float v_adc_mv = ((float)raw_adc / (float)ADC_MAX_VALUE) * (float)VREF_MV;

  /* divider multiplier = (R1 + R2) / R2 */
  float mult = ((float)divider_R1[tap_index] + (float)divider_R2[tap_index]) / (float)divider_R2[tap_index];

  float tap_mv = v_adc_mv * mult;
  if (tap_mv < 0.0f) tap_mv = 0.0f;
  if (tap_mv > 65535.0f) tap_mv = 65535.0f;
  return (uint16_t)(tap_mv + 0.5f);
}
      
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc)
{
    if (hadc->Instance == ADC1)
    {
        adc_ready = 1;
    }
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM2)
        tim_flag = 1;
}

void calcCellVolt(void)
{
  /* compute tap voltages directly from raw ADC (mV) */
  for (uint8_t i = 0; i < CELL_COUNT; i++) {
    /* use averaged ADC value when available (otherwise 0 until filled) */
    uint16_t raw_adc = last_avg_adc[i];
    tapVolt[i] = adc_raw_to_tap_mv(raw_adc, i);
  }

  /* compute individual cell voltages by differencing consecutive taps */
  voltage_mV[0] = tapVolt[0];
  for (uint8_t i = 1; i < CELL_COUNT; i++) {
    if (tapVolt[i] > tapVolt[i - 1])
      voltage_mV[i] = tapVolt[i] - tapVolt[i - 1];
    else
      voltage_mV[i] = 0u;
  }

  /* apply calibration factors (currently 1.0) and saturate */
  for (uint8_t i = 0; i < CELL_COUNT; i++) {
    float scaled = (float)voltage_mV[i] * calib_factor[i];
    if (scaled < 0.0f) scaled = 0.0f;
    if (scaled > 65535.0f) scaled = 65535.0f;
    voltage_mV[i] = (uint16_t)(scaled + 0.5f);
  }
}

void filterVoltage(void)
{
  /* No filtering: apply only the 0..500mV deadzone rule */
  for (int i = 0; i < CELL_COUNT; i++) {
    if (voltage_mV[i] < 500u)
      voltage_mV_f[i] = 0u;
    else
      voltage_mV_f[i] = voltage_mV[i];
  }
}

float NTC_GetTemperature(uint16_t adc_value)
{
    if(adc_value == 0 || adc_value >= 4095)
    {
        return -273.15f; 
    }

    float voltage = (float)adc_value * 3.3f / 4095.0f;
    float r_ntc = 10000.0f * (3.3f - voltage) / voltage;

    float A = 0.001129148f;
    float B = 0.000234125f;
    float C = 0.0000000876741f;

    float lnR = logf(r_ntc); 
    float temp_kelvin = 1.0f / (A + (B * lnR) + (C * lnR * lnR * lnR));
    
    return temp_kelvin - 273.15f; 
}

void calcTemperature(void)
{
    uint16_t raw_ntc = adc_buf[5];
    if (raw_ntc == 0) return;
	
    static uint16_t median_buf[5] = {0};
    static uint8_t  median_idx    = 0;
    static uint8_t  median_full   = 0;

    median_buf[median_idx] = raw_ntc;
    median_idx = (median_idx + 1) % 5;
    if (median_idx == 0) median_full = 1;

    uint16_t sorted[5];
    uint8_t  count = median_full ? 5 : median_idx;
    for (int i = 0; i < count; i++)
        sorted[i] = median_buf[i];
    for (int i = 0; i < count - 1; i++)
        for (int j = 0; j < count - i - 1; j++)
            if (sorted[j] > sorted[j+1]) {
                uint16_t tmp = sorted[j];
                sorted[j]    = sorted[j+1];
                sorted[j+1]  = tmp;
            }
    uint16_t median_ntc = sorted[count / 2];

    if (!ntc_ema_init) {
        ntc_ema      = (uint32_t)median_ntc << 4;
        ntc_ema_init = 1;
        temperature  = NTC_GetTemperature(median_ntc);
        return;
    }

    uint16_t prev = (uint16_t)(ntc_ema >> 4);

		if (median_ntc < prev - 10) {
				ntc_ema = ntc_ema - (ntc_ema >> 2) + median_ntc; 
		} else {
				ntc_ema = ntc_ema - (ntc_ema >> 4) + median_ntc;
		}
    uint16_t filtered_ntc = (uint16_t)(ntc_ema >> 4);
    temperature = NTC_GetTemperature(filtered_ntc);
}

void control_balance(void)
{
    const uint16_t control_PIN[5] = {
        GPIO_PIN_8, GPIO_PIN_9, GPIO_PIN_10,
        GPIO_PIN_11, GPIO_PIN_12
    };

    if (!temp_cutoff && temperature > 60.0f)
        temp_cutoff = 1;
    else if (temp_cutoff && temperature < 55.0f)
        temp_cutoff = 0;

    if (temp_cutoff || !balance_enable) {
        for (int i = 0; i < 5; i++) bal_state[i] = 0;
        HAL_GPIO_WritePin(GPIOA,
            GPIO_PIN_8|GPIO_PIN_9|GPIO_PIN_10|
            GPIO_PIN_11|GPIO_PIN_12, GPIO_PIN_RESET);
        return;
    }

    for (int i = 0; i < 5; i++) {
        uint8_t master_cmd = (balance_mask >> i) & 0x01;

        if (master_cmd)
            bal_state[i] = 1;
        else
            bal_state[i] = 0;

        HAL_GPIO_WritePin(GPIOA, control_PIN[i],
            bal_state[i] ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }
}

void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &RxHeader, RxData);

    // Slave 2 command ID
    if (RxHeader.StdId == CAN_ID_SLAVE2_RX) { 
        balance_enable = RxData[0] & 0x01;
        balance_mask   = RxData[1]; 
    }
    HAL_CAN_ActivateNotification(hcan, CAN_IT_RX_FIFO0_MSG_PENDING);
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_ADC1_Init();
  MX_TIM2_Init();
  MX_CAN_Init();
  /* USER CODE BEGIN 2 */

	HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc_val, 6);
	HAL_TIM_Base_Start_IT(&htim2);
	
	HAL_CAN_Start(&hcan);
	TxHeader.DLC   = 8;
	TxHeader.IDE   = CAN_ID_STD;
	TxHeader.RTR   = CAN_RTR_DATA;
  TxHeader.StdId = CAN_ID_SLAVE2_TX;
	
	sFilterConfig.FilterActivation     = CAN_FILTER_ENABLE;
	sFilterConfig.FilterFIFOAssignment = CAN_FILTER_FIFO0;
	sFilterConfig.FilterMode           = CAN_FILTERMODE_IDMASK;
  sFilterConfig.FilterIdHigh         = CAN_ID_SLAVE2_RX << 5;
	sFilterConfig.FilterIdLow          = 0;
	sFilterConfig.FilterMaskIdHigh     = 0x7FF << 5;
	sFilterConfig.FilterMaskIdLow      = 0;
	sFilterConfig.FilterScale          = CAN_FILTERSCALE_32BIT;
	sFilterConfig.FilterBank           = 0;
	HAL_CAN_ConfigFilter(&hcan, &sFilterConfig);
	HAL_CAN_ActivateNotification(&hcan, CAN_IT_RX_FIFO0_MSG_PENDING);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
   if (adc_ready) {
    __disable_irq();
    for (int i = 0; i < 6; i++)
        adc_buf[i] = adc_val[i];
    adc_ready = 0;
    __enable_irq();
  /* downsample: only accumulate every SAMPLE_SKIPth ADC-ready event */
  sample_skip_idx = (sample_skip_idx + 1) % SAMPLE_SKIP;
  if (sample_skip_idx == 0) {
    for (uint8_t i = 0; i < CELL_COUNT; i++) {
      avg_sum[i] += adc_buf[i];
      avg_count[i]++;
      if (avg_count[i] >= AVG_SAMPLE_COUNT) {
        last_avg_adc[i] = (uint16_t)(avg_sum[i] / AVG_SAMPLE_COUNT);
        avg_sum[i] = 0;
        avg_count[i] = 0;
      }
    }
  }

  }

  if (tim_flag) {  
    tim_flag = 0;

    calcTemperature();
    calcCellVolt();
    filterVoltage();
    control_balance();

    /* Two-frame CAN: Frame A contains 4 cells (u16 each), Frame B contains cell5 (u16),
       temperature in tenths (int16), mosfet mask (u8) and seq (u8) */
    static uint8_t seq = 0;
    uint8_t TxA[8];
    uint8_t TxB[6];

    /* Prepare cell values (mV) - use filtered calibrated values */
    uint16_t c[5];
    for (int i = 0; i < 5; i++) {
      c[i] = voltage_mV_f[i];
    }

    /* Frame A: cells 1..4 as little-endian uint16 */
    for (int i = 0; i < 4; i++) {
      TxA[2*i]   = (uint8_t)(c[i] & 0xFF);
      TxA[2*i+1] = (uint8_t)((c[i] >> 8) & 0xFF);
    }

    /* Transmit Frame A */
    TxHeader.DLC = 8;
    if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) == 0) {
      HAL_CAN_AbortTxRequest(&hcan, CAN_TX_MAILBOX0);
      HAL_CAN_AbortTxRequest(&hcan, CAN_TX_MAILBOX1);
      HAL_CAN_AbortTxRequest(&hcan, CAN_TX_MAILBOX2);
    }
    if (HAL_CAN_AddTxMessage(&hcan, &TxHeader, TxA, &TxMailbox) == HAL_OK)
      tx_count++;

    /* Frame B: cell5, temperature in tenths (int16), mask, seq */
    int16_t temp_tenths = 0;
    if (!isnan(temperature) && temperature > -1000.0f && temperature < 1000.0f)
      temp_tenths = (int16_t)(temperature * 10.0f + (temperature >= 0 ? 0.5f : -0.5f));

    TxB[0] = (uint8_t)(c[4] & 0xFF);
    TxB[1] = (uint8_t)((c[4] >> 8) & 0xFF);
    TxB[2] = (uint8_t)(temp_tenths & 0xFF);
    TxB[3] = (uint8_t)((temp_tenths >> 8) & 0xFF);
    uint8_t mosfet = 0;
    for (int i = 0; i < 5; i++) mosfet |= (bal_state[i] & 0x01) << i;
    TxB[4] = mosfet;
    TxB[5] = seq++;

    TxHeader.DLC = 6;
    if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) == 0) {
      HAL_CAN_AbortTxRequest(&hcan, CAN_TX_MAILBOX0);
      HAL_CAN_AbortTxRequest(&hcan, CAN_TX_MAILBOX1);
      HAL_CAN_AbortTxRequest(&hcan, CAN_TX_MAILBOX2);
    }
    if (HAL_CAN_AddTxMessage(&hcan, &TxHeader, TxB, &TxMailbox) == HAL_OK)
      tx_count++;

    can_error = HAL_CAN_GetError(&hcan);
    HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);

    if (can_error & HAL_CAN_ERROR_BOF) {
      HAL_CAN_Stop(&hcan);
      HAL_CAN_Start(&hcan);
    }
}
}
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Common config
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ScanConvMode = ADC_SCAN_ENABLE;
  hadc1.Init.ContinuousConvMode = ENABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 6;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_1;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_2;
  sConfig.Rank = ADC_REGULAR_RANK_2;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_3;
  sConfig.Rank = ADC_REGULAR_RANK_3;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_4;
  sConfig.Rank = ADC_REGULAR_RANK_4;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_5;
  sConfig.Rank = ADC_REGULAR_RANK_5;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_6;
  sConfig.Rank = ADC_REGULAR_RANK_6;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief CAN Initialization Function
  * @param None
  * @retval None
  */
static void MX_CAN_Init(void)
{

  /* USER CODE BEGIN CAN_Init 0 */

  /* USER CODE END CAN_Init 0 */

  /* USER CODE BEGIN CAN_Init 1 */

  /* USER CODE END CAN_Init 1 */
  hcan.Instance = CAN1;
  hcan.Init.Prescaler = 18;
  hcan.Init.Mode = CAN_MODE_NORMAL;
  hcan.Init.SyncJumpWidth = CAN_SJW_1TQ;
  hcan.Init.TimeSeg1 = CAN_BS1_13TQ;
  hcan.Init.TimeSeg2 = CAN_BS2_2TQ;
  hcan.Init.TimeTriggeredMode = DISABLE;
  hcan.Init.AutoBusOff = ENABLE;
  hcan.Init.AutoWakeUp = DISABLE;
  hcan.Init.AutoRetransmission = ENABLE;
  hcan.Init.ReceiveFifoLocked = DISABLE;
  hcan.Init.TransmitFifoPriority = DISABLE;
  if (HAL_CAN_Init(&hcan) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN CAN_Init 2 */

  /* USER CODE END CAN_Init 2 */

}

/**
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 7199;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 199;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Channel1_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, CONTROL_1_Pin|CONTROL_2_Pin|CONTROL_3_Pin|CONTROL_4_Pin
                          |CONTROL_5_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : PC13 */
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : CONTROL_1_Pin CONTROL_2_Pin CONTROL_3_Pin CONTROL_4_Pin
                           CONTROL_5_Pin */
  GPIO_InitStruct.Pin = CONTROL_1_Pin|CONTROL_2_Pin|CONTROL_3_Pin|CONTROL_4_Pin
                          |CONTROL_5_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
