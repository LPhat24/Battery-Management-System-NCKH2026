/* USER CODE BEGIN Header */
/** LE THANH PHAT
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
#include "i2c-lcd.h"
#include "stdint.h"
#include "stdio.h"
#include <stdbool.h>
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

#define MASTER_SLAVE_COUNT      3U

/* Masks for packed CAN fields (used by CAN_UnpackData)
 * CELL_MASK: 9 bits (0..8)
 * TEMP_MASK: 8 bits
 * MOS_MASK : 5 bits
 */
#define CELL_MASK 0x1FFULL
#define TEMP_MASK 0xFFULL
#define MOS_MASK  0x1FULL

/* Telemetry constants to avoid magic numbers */
#define TELEMETRY_BUF_SIZE 200
#define TOTAL_CELLS_COUNT TOTAL_CELLS

/* Current sensor removed */
/* ADC helpers for debug */
#define ADC_MAX_VALUE 4095U
#define VREF_MV 3300U
/* Current sensor constants (WCS1700) */
#define CURRENT_SENSOR_SENS_V_PER_A 0.02376f
#define CURRENT_SENSOR_ZERO_DEFAULT_MV (VREF_MV / 2U)
#define CURRENT_SENSOR_DEADBAND_A 0.1f
#define CURRENT_ADC_INDEX 4
#define CURRENT_CALIB_SAMPLES 500
/* Fast setting filter: one update every main-loop cycle, approximately 80 ms time constant. */
#define SETTINGS_FILTER_SHIFT 3U
#define SETTINGS_DEADBAND_ADC 2U
#define CELL_FILTER_SHIFT     2U

/* Setting output ranges */
#define ICHG_MAX_MA 11000U
#define VDIS_MAX_MV 4200U
#define VCHG_MAX_MV 4300U
#define IDIS_MAX_MA 41000U

/* SOC estimation constants (per-cell OCV linear mapping) */
#define SOC_OCV_MIN_MV_PER_CELL 3000U
#define SOC_OCV_MAX_MV_PER_CELL 4200U

typedef struct {
	uint8_t ButtonLeft;
	uint8_t ButtonRight;
	uint8_t SW_LabVIEW;
	uint8_t SW_Balancing;
	uint8_t SW_LCDMode;
	uint8_t SW_Load;
	uint8_t CurrentDigital;
} DigitalInput;

typedef struct {
	uint16_t Raw_I_Discharge_Max;
	uint16_t Raw_V_Discharge_Min;
	uint16_t Raw_I_Charge_Max;
	uint16_t Raw_V_Charge_Max;
	uint16_t ChargerVoltage;
} AnalogInput;

typedef enum {
	LCD_PAGE_1 = 0,
	LCD_PAGE_2,
	LCD_PAGE_3,
	LCD_PAGE_4,
	LCD_PAGE_MAX
} LCD_Page_t;

uint8_t lcd_page = 0;
uint8_t lcd_update_flag = 1;

uint8_t Button_Left_Old = 0;
uint8_t Button_Right_Old = 0;

typedef enum {
    PRECHARGE_IDLE,
    PRECHARGE_ACTIVE,
    PRECHARGE_DONE
} PrechargeState_t;


/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;

CAN_HandleTypeDef hcan;

I2C_HandleTypeDef hi2c1;

TIM_HandleTypeDef htim3;

UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */

volatile uint8_t    slave_connected[3] = {0};
volatile uint8_t    new_can_data       =  0;
CAN_TxHeaderTypeDef TxHeader_Bal;
uint32_t            TxMailbox_Bal;
uint8_t             TxData_Bal[8]      = {0};

uint16_t ADC_RawData[6];
DigitalInput Digital_In;
/* Debug variables for current sensor ADC (PA4) */
volatile uint16_t debug_current_adc_raw = 0;
/* Averaged measured millivolts for PA4 (signed to allow offset corrections) */
volatile int32_t debug_current_adc_mv = 0;

/* Computed zero (mV) for current sensor; can be updated by auto-calibration */
volatile int32_t current_zero_mv = CURRENT_SENSOR_ZERO_DEFAULT_MV;

/* Filtered ADC values for user-adjustable settings (0..ADC_MAX_VALUE). */
static uint16_t filtered_adc_ichg = 0;
static uint16_t filtered_adc_vdis = 0;
static uint16_t filtered_adc_vchg = 0;
static uint16_t filtered_adc_idis = 0;
static uint8_t settings_filter_initialized = 0u;

/* Runtime adjustable millivolt offset to match a reference measurement.
 * Default computed from a recent bench measurement: actual 1509mV vs measured 1451mV -> +58mV
 * Keep as adjustable `volatile` so you can change it from the debugger at runtime.
 */
#define DEFAULT_CURRENT_ADC_MV_OFFSET ((int32_t)1509 - (int32_t)1451)
volatile int32_t debug_current_adc_mv_offset = DEFAULT_CURRENT_ADC_MV_OFFSET;
AnalogInput Analog_In;

#define TOTAL_CELLS 15

uint16_t all_cell_voltage_mV[TOTAL_CELLS] = {0};
static uint8_t cell_filter_initialized[TOTAL_CELLS] = {0};


CAN_FilterTypeDef sFilterConfig;

char uart_tx_debug_buf[200];
volatile uint16_t uart_tx_debug_len = 0;

volatile float    slave_temp[3]        = {0};
volatile float    temperature_max      = 0.0f;
volatile int16_t  temperature_max_tenths = 0;
volatile uint8_t  rx_mosfet_status[15] = {0};
volatile uint32_t master_rx_count      = 0;
volatile uint8_t  master_rx_slave1     = 0;
volatile uint8_t  master_rx_slave2     = 0;
volatile uint8_t  master_rx_slave3     = 0;

CAN_TxHeaderTypeDef TxHeader_Cmd;
uint32_t            TxMailbox_Cmd;
uint8_t             TxData_Cmd[8]      = {0};

volatile uint32_t slave_last_tick[3] = {0};
#define SLAVE_TIMEOUT_MS 3000

/* Last computed current (tenths of A). Inspect this in debugger for final value. */
/* Current sensor removed: no runtime variables */

/* Temporary buffers to assemble 2-frame CAN messages per slave */
volatile uint8_t got_frameA[3] = {0};
volatile uint8_t got_frameB[3] = {0};
uint16_t tmp_frameA_cells[3][4];
uint16_t tmp_frameB_cell5[3];
int16_t  tmp_frameB_temp_tenths[3];
uint8_t  tmp_frameB_mask[3];

#define CELL_VALID_MIN_MV 500U
#define CELL_VALID_MAX_MV 5000U

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_ADC1_Init(void);
static void MX_CAN_Init(void);
static void MX_I2C1_Init(void);
static void MX_TIM3_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */

/* Current sensor debug calculation: read ADC at PA4 and compute mV */
void Current_Sensor_Calc(void);

/* Calibration helpers: set offset directly or calibrate using an observed reference mV */
void Set_Current_ADC_MV_Offset(int32_t offset_mv);
void Calibrate_Current_ADC_MV(uint32_t actual_mv);

/* Current sensor API */
float CurrentSensor_GetAmps(void);
int32_t CurrentSensor_Get_mA(void);
void CurrentSensor_AutoCalibrate(void);

/* Safety control for charge/discharge based on settings and measurements */
void Safety_Control(void);


void Master_Balance_Control(void);

static void Update_Temperature_Max(void);
static void LCD_WriteLine(uint8_t ddram_addr, const char *text);
static void Update_Cell_Voltage_Filter(uint8_t cell_index, uint16_t sample_mV);

void Check_Slave_Timeout(void);
void Read_Digital_Input (void);
void Read_Analog_Input (void);
void Button_Handle (void);
void Send_Cells_UART(void);

uint16_t map(uint16_t x, uint16_t in_min, uint16_t in_max, uint16_t out_min, uint16_t out_max);

void LCD_Page1 (void);
void LCD_Page2 (void);
void LCD_Page3 (void);
void LCD_Page4 (void);
void LCD_PageSetting (void);
void LCD_Update(void);
void Precharge_Handle(void);

/* SOC estimation API: returns tenths of percent (0..1000) */
uint16_t Compute_SOC_TenthsPercent(void);


/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/*
 * CAN_UnpackData: unpack an 8-byte little-endian CAN payload into typed fields.
 * - rebuilds a uint64_t 'frame' from data[0]..data[7] (little-endian: data[0] = LSB)
 * - uses shifts and masks to extract each field according to the bit layout
 * - scales cell values back to millivolts (stored unit = 10mV)
 */
void CAN_UnpackData(uint8_t data[8],
                    uint16_t *c1, uint16_t *c2, uint16_t *c3,
                    uint16_t *c4, uint16_t *c5,
                    uint8_t *temp, uint8_t *mosfet)
{
  // Rebuild 64-bit frame from little-endian byte array.
  // data[0] is least-significant byte so shift by (8*i).
  uint64_t frame = 0ULL;
  for (int i = 0; i < 8; ++i) {
    frame |= ((uint64_t)data[i]) << (8 * i);
  }

  // Extract fields using masks and shifts. Masks limit the number of bits
  // taken for each field (prevent contamination from neighbor bits).
  // CELL_MASK (0x1FF) -> 9 bits, TEMP_MASK (0xFF) -> 8 bits, MOS_MASK (0x1F) -> 5 bits
  uint64_t s1 = (frame >> 0)  & CELL_MASK; // C1: bits 0-8
  uint64_t s2 = (frame >> 9)  & CELL_MASK; // C2: bits 9-17
  uint64_t s3 = (frame >> 18) & CELL_MASK; // C3: bits 18-26
  uint64_t s4 = (frame >> 27) & CELL_MASK; // C4: bits 27-35
  uint64_t s5 = (frame >> 36) & CELL_MASK; // C5: bits 36-44
  uint64_t st = (frame >> 45) & TEMP_MASK; // Temp: bits 45-52 (8 bits)
  uint64_t sm = (frame >> 53) & MOS_MASK;  // MOSFET: bits 53-57 (5 bits)

  // Scale back: transmitter stored cell voltages in units of 10mV
  // so multiply by 10 to get mV. Use casts and guards against NULL pointers.
  if (c1) *c1 = (uint16_t)(s1 * 10ULL);
  if (c2) *c2 = (uint16_t)(s2 * 10ULL);
  if (c3) *c3 = (uint16_t)(s3 * 10ULL);
  if (c4) *c4 = (uint16_t)(s4 * 10ULL);
  if (c5) *c5 = (uint16_t)(s5 * 10ULL);

  if (temp)   *temp   = (uint8_t)st; // temp is already 8-bit
  if (mosfet) *mosfet = (uint8_t)sm; // mosfet is 5-bit value stored in LSBs
}

/**
 * Example: receive handler for HAL CAN FIFO0 pending message.
 * This shows how to call CAN_UnpackData when a frame arrives.
 */

uint16_t rx_cell_voltage[5];

static void Update_Temperature_Max(void)
{
  float max_temp = slave_temp[0];

  for (uint8_t s = 1U; s < MASTER_SLAVE_COUNT; ++s) {
    if (slave_temp[s] > max_temp) {
      max_temp = slave_temp[s];
    }
  }

  temperature_max = max_temp;
  if (max_temp >= 0.0f) {
    temperature_max_tenths = (int16_t)(max_temp * 10.0f + 0.5f);
  } else {
    temperature_max_tenths = (int16_t)(max_temp * 10.0f - 0.5f);
  }
}

static void LCD_WriteLine(uint8_t ddram_addr, const char *text)
{
  char line[21];

  snprintf(line, sizeof(line), "%-20s", text);
  lcd_send_cmd(0x80 | ddram_addr);
  lcd_send_string(line);
}

static void Update_Cell_Voltage_Filter(uint8_t cell_index, uint16_t sample_mV)
{
  int32_t error;

  if (!cell_filter_initialized[cell_index]) {
    all_cell_voltage_mV[cell_index] = sample_mV;
    cell_filter_initialized[cell_index] = 1u;
    return;
  }

  error = (int32_t)sample_mV - (int32_t)all_cell_voltage_mV[cell_index];
  all_cell_voltage_mV[cell_index] = (uint16_t)((int32_t)all_cell_voltage_mV[cell_index] +
      (error >> CELL_FILTER_SHIFT));
}


void HAL_CAN_RxFifo1MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
  CAN_RxHeaderTypeDef RxHeader;
  uint8_t RxData[8];

  if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO1, &RxHeader, RxData) != HAL_OK)
  {
    HAL_CAN_ActivateNotification(hcan, CAN_IT_RX_FIFO1_MSG_PENDING);
    return;
  }

  uint8_t idx = 0xFFU;
  if      (RxHeader.StdId == CAN_ID_SLAVE1_TX) { idx = 0U; master_rx_slave1++; }
  else if (RxHeader.StdId == CAN_ID_SLAVE2_TX) { idx = 1U; master_rx_slave2++; }
  else if (RxHeader.StdId == CAN_ID_SLAVE3_TX) { idx = 2U; master_rx_slave3++; }
  else {
    HAL_CAN_ActivateNotification(hcan, CAN_IT_RX_FIFO1_MSG_PENDING);
    return;
  }

  /* Mark slave present and timestamp */
  slave_connected[idx] = 1;
  slave_last_tick[idx] = HAL_GetTick();
  master_rx_count++;

  uint8_t base = idx * 5;

  /* Distinguish the two-frame protocol using DLC */
  if (RxHeader.DLC == 8)
  {
    /* Frame A: four uint16_t cell voltages (little-endian) */
    uint16_t max_val = 0;
    for (int i = 0; i < 4; i++) {
      tmp_frameA_cells[idx][i] = (uint16_t)(RxData[2*i] | (RxData[2*i+1] << 8));
      if (tmp_frameA_cells[idx][i] > max_val) max_val = tmp_frameA_cells[idx][i];
    }
    for (int i = 0; i < 4; i++) {
      if (tmp_frameA_cells[idx][i] < CELL_VALID_MIN_MV ||
          tmp_frameA_cells[idx][i] > CELL_VALID_MAX_MV) {
        got_frameA[idx] = 0;
        HAL_CAN_ActivateNotification(hcan, CAN_IT_RX_FIFO1_MSG_PENDING);
        return;
      }
    }
    /* If values look 10x too large (e.g. 39990 instead of 3999), normalize */
    if (max_val > 15000) {
      for (int i = 0; i < 4; i++)
        tmp_frameA_cells[idx][i] = tmp_frameA_cells[idx][i] / 10;
    }
    got_frameA[idx] = 1;
  }
  else
  {
    /* Frame B: cell5 (u16), temp in tenths (int16), mask (u8), seq (u8 optional)
     * Expected at least 5 bytes (we use first 6 if present)
     */
    tmp_frameB_cell5[idx] = (uint16_t)(RxData[0] | (RxData[1] << 8));
    tmp_frameB_temp_tenths[idx] = (int16_t)(RxData[2] | (RxData[3] << 8));
    tmp_frameB_mask[idx] = RxData[4];

    if (tmp_frameB_cell5[idx] < CELL_VALID_MIN_MV ||
        tmp_frameB_cell5[idx] > CELL_VALID_MAX_MV) {
      got_frameB[idx] = 0;
      HAL_CAN_ActivateNotification(hcan, CAN_IT_RX_FIFO1_MSG_PENDING);
      return;
    }

    /* Update per-cell mosfet status */
    for (int i = 0; i < 5; i++)
      rx_mosfet_status[base + i] = (tmp_frameB_mask[idx] >> i) & 0x01;

    /* If cell5 looks 10x too large, normalize it */
    if (tmp_frameB_cell5[idx] > 15000) tmp_frameB_cell5[idx] = tmp_frameB_cell5[idx] / 10;

    /* Store temperature as float degrees (tenths -> °C) */
    slave_temp[idx] = ((float)tmp_frameB_temp_tenths[idx]) / 10.0f;
    Update_Temperature_Max();
    got_frameB[idx] = 1;
  }

  /* If we've received both halves for this slave, assemble into final buffers */
  if (got_frameA[idx] && got_frameB[idx]) {
    for (int i = 0; i < 4; i++)
      Update_Cell_Voltage_Filter((uint8_t)(base + i), tmp_frameA_cells[idx][i]);
    Update_Cell_Voltage_Filter((uint8_t)(base + 4), tmp_frameB_cell5[idx]);

    /* reset assembly flags */
    got_frameA[idx] = 0;
    got_frameB[idx] = 0;

    /* indicate new complete data set */
    new_can_data = 1;

    /* Blink user LED once per assembled two-frame message (matches slave) */
    HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
  }
  HAL_CAN_ActivateNotification(hcan, CAN_IT_RX_FIFO1_MSG_PENDING);
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
  MX_CAN_Init();
  MX_I2C1_Init();
  MX_TIM3_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */
	
	TxHeader_Bal.DLC = 8;
	TxHeader_Bal.IDE = CAN_ID_STD;
	TxHeader_Bal.RTR = CAN_RTR_DATA;
  TxHeader_Bal.StdId = CAN_ID_SLAVE1_RX; 

  HAL_ADC_Start_DMA(&hadc1, (uint32_t*) ADC_RawData, 6);
  /* Start a brief auto-calibration for current sensor zero point */
  CurrentSensor_AutoCalibrate();

    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3);

  HAL_CAN_Start(&hcan);

  sFilterConfig.FilterBank           = 0;
	sFilterConfig.FilterActivation     = CAN_FILTER_ENABLE;
	sFilterConfig.FilterFIFOAssignment = CAN_FILTER_FIFO1;
	sFilterConfig.FilterMode           = CAN_FILTERMODE_IDMASK;
	sFilterConfig.FilterScale          = CAN_FILTERSCALE_32BIT;
	sFilterConfig.FilterIdHigh         = 0x0000;
	sFilterConfig.FilterIdLow          = 0x0000;
	sFilterConfig.FilterMaskIdHigh     = 0x0000;  
	sFilterConfig.FilterMaskIdLow      = 0x0000;
	HAL_CAN_ConfigFilter(&hcan, &sFilterConfig);
	HAL_CAN_ActivateNotification(&hcan, CAN_IT_RX_FIFO1_MSG_PENDING);

	TxHeader_Cmd.DLC   = 8;
	TxHeader_Cmd.IDE   = CAN_ID_STD;
	TxHeader_Cmd.RTR   = CAN_RTR_DATA;
  TxHeader_Cmd.StdId = CAN_ID_SLAVE1_RX;

  lcd_init();
  lcd_send_cmd(0x80|0x02);
  lcd_send_string("PASSIVE BMS");
  lcd_send_cmd(0x80|0x43);
  lcd_send_string("NGUYEN DUY THONG");
  lcd_send_cmd(0x80|0x15);
  lcd_send_string("LE THANH PHAT");
  lcd_send_cmd(0x80|0x54);
  lcd_send_string("--------------------");
  HAL_Delay(1000);
  lcd_clear();

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
	while (1)
	{
    Read_Digital_Input();
    Precharge_Handle();
    Button_Handle();
    Read_Analog_Input();
    /* Enforce charge/discharge safety after analog/settings update */
    Safety_Control();
    Check_Slave_Timeout();

    if (new_can_data) {
        new_can_data = 0;
        Master_Balance_Control(); 
        Send_Cells_UART();
        lcd_update_flag = 1;
    }

    /* Periodic UART telemetry: non-blocking, every 700 ms */
    {
      static uint32_t uart_tick = 0;
      if ((HAL_GetTick() - uart_tick) >= 700U) {
        uart_tick = HAL_GetTick();
        Send_Cells_UART(); /* Send_Cells_UART is non-blocking (uses IT) and checks UART state */
      }
    }

    static uint32_t lcd_tick = 0;
    uint32_t lcd_period_ms = Digital_In.SW_LCDMode ? 100U : 500U;
    if (HAL_GetTick() - lcd_tick >= lcd_period_ms) {
        lcd_tick = HAL_GetTick();
        lcd_update_flag = 1;
    }

    LCD_Update();
    HAL_Delay(20);

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
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
  sConfig.Channel = ADC_CHANNEL_0;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_71CYCLES_5;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_1;
  sConfig.Rank = ADC_REGULAR_RANK_2;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_2;
  sConfig.Rank = ADC_REGULAR_RANK_3;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_3;
  sConfig.Rank = ADC_REGULAR_RANK_4;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_4;
  sConfig.Rank = ADC_REGULAR_RANK_5;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_5;
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
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 0;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 65535;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_PWM_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_4) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */

  /* USER CODE END TIM3_Init 2 */
  HAL_TIM_MspPostInit(&htim3);

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

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
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10|GPIO_PIN_11|GPIO_PIN_14|GPIO_PIN_15, GPIO_PIN_RESET);

  /*Configure GPIO pin : PC13 */
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : PC14 PC15 */
  GPIO_InitStruct.Pin = GPIO_PIN_14|GPIO_PIN_15;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : PB10 PB11 PB14 PB15 */
  GPIO_InitStruct.Pin = GPIO_PIN_10|GPIO_PIN_11|GPIO_PIN_14|GPIO_PIN_15;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : PA8 PA15 */
  GPIO_InitStruct.Pin = GPIO_PIN_8|GPIO_PIN_15;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : PB3 PB4 PB5 */
  GPIO_InitStruct.Pin = GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_5;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

void Master_Balance_Control(void)
{
    if (!Digital_In.SW_Balancing) {
        for (int s = 0; s < 3; s++) {
            if (!slave_connected[s]) continue;
            TxData_Bal[0] = 0; // enable = 0
            TxData_Bal[1] = 0; // mask = 0
            TxHeader_Bal.StdId = (uint32_t)CAN_ID_SLAVE1_RX + (uint32_t)s;
            if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0)
                HAL_CAN_AddTxMessage(&hcan, &TxHeader_Bal, TxData_Bal, &TxMailbox_Bal);
        }
        return;
    }

    uint16_t global_min_mV = 65535;
    uint16_t global_max_mV = 0;

    // Tìm max/min toàn hệ thống bằng số nguyên
    for (int i = 0; i < 15; i++) {
        if (all_cell_voltage_mV[i] < 500) continue; // Bỏ qua cell < 500mV (bị ngắt)
        
        if (all_cell_voltage_mV[i] < global_min_mV)
            global_min_mV = all_cell_voltage_mV[i];
            
        if (all_cell_voltage_mV[i] > global_max_mV)
            global_max_mV = all_cell_voltage_mV[i];
    }

    // Chênh lệch <= 10mV thì không xả
    if (global_max_mV >= global_min_mV && (global_max_mV - global_min_mV) <= 10) {
        for (int s = 0; s < 3; s++) {
            if (!slave_connected[s]) continue;
            TxData_Bal[0] = 1;
            TxData_Bal[1] = 0; // mask = 0
            TxHeader_Bal.StdId = (uint32_t)CAN_ID_SLAVE1_RX + (uint32_t)s;
            if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0)
                HAL_CAN_AddTxMessage(&hcan, &TxHeader_Bal, TxData_Bal, &TxMailbox_Bal);
        }
        return;
    }

    // Tính mask xả cho từng Slave
    for (int s = 0; s < 3; s++) {
        if (!slave_connected[s]) continue;

        uint8_t mask = 0;
        uint8_t base = s * 5;

        // BẢO VỆ MỀM CỦA MASTER: Nếu cụm pin này dưới 55 độ thì mới cho phép xả
        if (temperature_max < 55.0f) {
          for (int i = 0; i < 5; i++) {
            uint16_t v_mV = all_cell_voltage_mV[base + i];
            if (v_mV < 500) continue;

            // Select any cell that is greater than the global minimum by > 10mV.
            // Cells within 10mV of the minimum will not be selected for discharge.
            if (v_mV > (global_min_mV + 10)) {
              mask |= (1 << i);
            }
          }
        }

        TxData_Bal[0] = 1;    // enable = 1
        TxData_Bal[1] = mask; // balance mask
        TxHeader_Bal.StdId = (uint32_t)CAN_ID_SLAVE1_RX + (uint32_t)s;

        if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0)
            HAL_CAN_AddTxMessage(&hcan, &TxHeader_Bal, TxData_Bal, &TxMailbox_Bal);
    }
}

void Check_Slave_Timeout(void)
{
    uint32_t now = HAL_GetTick();
    for (int s = 0; s < 3; s++) {
        if (slave_last_tick[s] > 0 &&
            (now - slave_last_tick[s]) > SLAVE_TIMEOUT_MS) {
            uint8_t base = s * 5;
            for (int i = 0; i < 5; i++) {
                all_cell_voltage_mV[base + i] = 0;
                cell_filter_initialized[base + i] = 0u;
            }
            slave_temp[s]      = 0;
            slave_last_tick[s] = 0;
        }
    }
  Update_Temperature_Max();
}

/* Current sensor removed: no measurement function */

void Precharge_Handle(void)
{
    static PrechargeState_t precharge_state = PRECHARGE_IDLE;
    static uint32_t precharge_timer = 0;

    switch (precharge_state)
    {
        case PRECHARGE_IDLE:
            if (Digital_In.SW_Load)
            {
                // Bật precharge relay
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, GPIO_PIN_SET);
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_11, GPIO_PIN_RESET);

                precharge_timer = HAL_GetTick();
                precharge_state = PRECHARGE_ACTIVE;
            }
            else
            {
                // Tắt hết nếu không bật balance
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, GPIO_PIN_RESET);
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_11, GPIO_PIN_RESET);
            }
            break;

        case PRECHARGE_ACTIVE:
            // Chờ đủ 5s
            if (HAL_GetTick() - precharge_timer >= 5000)
            {
                // Tắt precharge, bật discharge
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, GPIO_PIN_RESET);
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_11, GPIO_PIN_SET);

                precharge_state = PRECHARGE_DONE;
            }
            break;

        case PRECHARGE_DONE:
            // Nếu tắt balance → reset toàn bộ
            if (!Digital_In.SW_Load)
            {
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, GPIO_PIN_RESET);
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_11, GPIO_PIN_RESET);

                precharge_state = PRECHARGE_IDLE;
            }
            break;
    }
}
void Read_Digital_Input (void) {
	Digital_In.ButtonLeft = 	 !HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_8);
	Digital_In.ButtonRight =     !HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_4);

	Digital_In.SW_Balancing =   !HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_3);
	Digital_In.SW_LabVIEW =     !HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_15);

	Digital_In.SW_LCDMode =     !HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_14);
	Digital_In.SW_Load =        !HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_15);
}
void Read_Analog_Input (void) {
  Analog_In.Raw_I_Charge_Max = ADC_RawData [0];
  Analog_In.Raw_V_Discharge_Min = ADC_RawData [1];
  Analog_In.Raw_V_Charge_Max =    ADC_RawData [2];
  Analog_In.Raw_I_Discharge_Max = ADC_RawData [3];

  Analog_In.ChargerVoltage = ADC_RawData [5];
  /* Update debug ADC/voltage for current sensor (PA4) */
  Current_Sensor_Calc();
  /* A fast EMA removes ADC noise without making adjustment feel delayed. */
  {
    uint16_t *filtered[4] = {&filtered_adc_ichg, &filtered_adc_vdis,
                              &filtered_adc_vchg, &filtered_adc_idis};
    uint16_t raw[4] = {Analog_In.Raw_I_Charge_Max, Analog_In.Raw_V_Discharge_Min,
                       Analog_In.Raw_V_Charge_Max, Analog_In.Raw_I_Discharge_Max};

    for (uint8_t ch = 0; ch < 4; ch++) {
      int32_t error = (int32_t)raw[ch] - (int32_t)*filtered[ch];
      if (!settings_filter_initialized)
        *filtered[ch] = raw[ch];
      else if (error > (int32_t)SETTINGS_DEADBAND_ADC ||
               error < -(int32_t)SETTINGS_DEADBAND_ADC)
        *filtered[ch] = (uint16_t)((int32_t)*filtered[ch] +
            (error >> SETTINGS_FILTER_SHIFT));
    }
    settings_filter_initialized = 1u;
  }
}

/* Safety control: set PB14/PB11 according to current and cell voltages
 * - PB14 HIGH: disconnect charge when current < -Ichg
 * - PB11 LOW : disconnect discharge when current > Idis or any cell < Vdis (ignore cells <=500mV)
 * - PB11 HIGH: disconnect charge when any cell > Vchg
 * When no condition holds for PB11, leave pin unchanged to avoid fighting other logic.
 */
void Safety_Control(void)
{
  int32_t cur_mA = CurrentSensor_Get_mA();

  /* Map filtered ADC settings to engineering units */
  uint32_t Ichg_mA = (uint32_t)map(filtered_adc_ichg, 0, ADC_MAX_VALUE, 0, ICHG_MAX_MA);
  uint32_t Vdis_mV = (uint32_t)map(filtered_adc_vdis, 0, ADC_MAX_VALUE, 0, VDIS_MAX_MV);
  uint32_t Vchg_mV = (uint32_t)map(filtered_adc_vchg, 0, ADC_MAX_VALUE, 0, VCHG_MAX_MV);
  uint32_t Idis_mA = (uint32_t)map(filtered_adc_idis, 0, ADC_MAX_VALUE, 0, IDIS_MAX_MA);

  /* PB14: charge inhibit (active HIGH) if charging current exceeds Ichg (negative current)
   * Set HIGH to disconnect charge, otherwise drive LOW to allow charge.
   */
  if (cur_mA < -(int32_t)Ichg_mA) {
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_SET);
  } else {
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_RESET);
  }

  /* Evaluate cell voltage conditions (ignore cells <= 500 mV for under-voltage check) */
  bool any_gt_vchg = false;
  bool any_lt_vdis = false;
  for (int i = 0; i < TOTAL_CELLS; ++i) {
    uint16_t v = all_cell_voltage_mV[i];
    if (v > Vchg_mV) any_gt_vchg = true;
    if (v > 500U && v < Vdis_mV) any_lt_vdis = true;
  }

  /* PB11: used for both charge and discharge inhibit
   * Priority: cell over-voltage -> inhibit charge (PB11 HIGH)
   * then current/discharge under-voltage -> inhibit discharge (PB11 LOW)
   * If none apply, do not modify PB11 (leave to precharge or other logic).
   */
  if (any_gt_vchg) {
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_SET); /* inhibit charge */
		HAL_GPIO_WritePin(GPIOB, GPIO_PIN_11, GPIO_PIN_RESET);
  } else if ((cur_mA > (int32_t)Idis_mA) || any_lt_vdis) {
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_11, GPIO_PIN_RESET); /* inhibit discharge */
  }
}

/* Read PA4 raw ADC value from DMA buffer and compute millivolts for debug */
void Current_Sensor_Calc(void)
{
  uint32_t raw = (uint32_t)ADC_RawData[4];
  debug_current_adc_raw = (uint16_t)raw;

  /* 50-sample moving average (circular buffer). 50 is not a power of two,
   * so use explicit modulo handling instead of bitmask.
   */
  enum { AVG_N = 50 };
  static uint16_t buf[AVG_N] = {0};
  static uint32_t sum = 0;
  static uint16_t idx = 0;
  static uint16_t count = 0;

  /* If buffer not yet full, append; otherwise replace oldest sample. */
  if (count < AVG_N) {
    buf[idx] = (uint16_t)raw;
    sum += buf[idx];
    idx++;
    count++;
    if (idx >= AVG_N) idx = 0;
  } else {
    /* full buffer: remove oldest, insert new */
    sum -= buf[idx];
    buf[idx] = (uint16_t)raw;
    sum += buf[idx];
    idx++;
    if (idx >= AVG_N) idx = 0;
  }

  uint32_t avg_adc = (count == 0) ? 0U : (sum / count);

  uint32_t mv = (uint32_t)(((uint64_t)avg_adc * (uint64_t)VREF_MV + (ADC_MAX_VALUE/2U)) / (uint64_t)ADC_MAX_VALUE);
  /* Apply runtime offset so measured mV can be corrected to a reference value. */
  int32_t mv_signed = (int32_t)mv + debug_current_adc_mv_offset;
  debug_current_adc_mv = mv_signed;
}

/* Set the calibration offset directly (mv). Use from debugger or via a command
 * to apply a fixed correction: debug_current_adc_mv_offset = offset_mv; */
void Set_Current_ADC_MV_Offset(int32_t offset_mv)
{
    debug_current_adc_mv_offset = offset_mv;
}

/* Calibrate using an observed true voltage (in mV). This computes offset = true - measured
 * and stores it in debug_current_adc_mv_offset so subsequent readings are corrected.
 * Call this once after observing `debug_current_adc_mv` and measuring the true mV. */
void Calibrate_Current_ADC_MV(uint32_t actual_mv)
{
    int32_t measured = debug_current_adc_mv;
    debug_current_adc_mv_offset = (int32_t)actual_mv - measured;
}

/*
 * Auto-calibrate the zero point for the current sensor by sampling
 * `CURRENT_CALIB_SAMPLES` values and storing the average into
 * `current_zero_mv`. This should be called after ADC DMA start.
 */
void CurrentSensor_AutoCalibrate(void)
{
  int64_t sum = 0;
  for (int i = 0; i < CURRENT_CALIB_SAMPLES; ++i) {
    /* allow DMA and averaging to update
     * small delay keeps CPU idle and lets Current_Sensor_Calc accumulate
     */
    HAL_Delay(2);
    Current_Sensor_Calc();
    sum += (int32_t)debug_current_adc_mv;
  }
  current_zero_mv = (int32_t)(sum / CURRENT_CALIB_SAMPLES);
}

/* Return current in amperes (float). Applies deadband around zero. */
float CurrentSensor_GetAmps(void)
{
  int32_t mv = debug_current_adc_mv;
  float diff_v = (float)(mv - current_zero_mv); /* mV */
  float amps = diff_v / (CURRENT_SENSOR_SENS_V_PER_A * 1000.0f);
  if (amps > -CURRENT_SENSOR_DEADBAND_A && amps < CURRENT_SENSOR_DEADBAND_A) return 0.0f;
  return amps;
}

/* Return current in milliamperes (rounded to nearest mA). */
int32_t CurrentSensor_Get_mA(void)
{
  float a = CurrentSensor_GetAmps();
  int32_t ma = (int32_t)(a * 1000.0f);
  return ma;
}

/* Compute SOC from average per-cell voltage using a simple linear OCV map.
 * Returns tenths of percent (0 -> 0.0%, 1000 -> 100.0%).
 */
uint16_t Compute_SOC_TenthsPercent(void)
{
    uint32_t sum = 0;
    uint16_t count = 0;

    for (int i = 0; i < TOTAL_CELLS; ++i) {
        uint16_t v = all_cell_voltage_mV[i];
        if (v < 500) continue; /* ignore invalid/disconnected cells */
        sum += v;
        ++count;
    }

    if (count == 0) return 0U;

    uint32_t avg_cell_mv = sum / count;

    /* Clamp to OCV range */
    if (avg_cell_mv <= SOC_OCV_MIN_MV_PER_CELL) return 0U;
    if (avg_cell_mv >= SOC_OCV_MAX_MV_PER_CELL) return 1000U;

    /* Linear mapping to tenths-percent: (avg - min) / (max - min) * 1000 */
    uint32_t numerator = (avg_cell_mv - SOC_OCV_MIN_MV_PER_CELL) * 1000U;
    uint32_t denom = (SOC_OCV_MAX_MV_PER_CELL - SOC_OCV_MIN_MV_PER_CELL);
    uint16_t soc_tenths = (uint16_t)(numerator / denom);
    return soc_tenths;
}


void Button_Handle (void) {
	static uint32_t debounce_time = 0;
	if (HAL_GetTick() - debounce_time < 200) return;

    if (Digital_In.ButtonRight && !Button_Right_Old) {
        lcd_page = (lcd_page + 1) % LCD_PAGE_MAX;
        lcd_update_flag = 1;
    }

    if (Digital_In.ButtonLeft && !Button_Left_Old) {
        lcd_page = (lcd_page == 0) ? LCD_PAGE_MAX - 1 : lcd_page - 1;
        lcd_update_flag = 1;
    }

    Button_Right_Old = Digital_In.ButtonRight;
    Button_Left_Old  = Digital_In.ButtonLeft;

    debounce_time = HAL_GetTick();
}
uint16_t map(uint16_t x, uint16_t in_min, uint16_t in_max, uint16_t out_min, uint16_t out_max) {
    if (in_max <= in_min) return out_min;

    uint32_t numerator = (uint32_t)(x - in_min) * (out_max - out_min);
    uint32_t denominator = (in_max - in_min);

    return (uint16_t)(numerator / denominator + out_min);
}
void LCD_Page1(void)
{
  char DataLCD[48];

    uint32_t Vtot_mV = 0;
    uint16_t Vmin_mV = 0xFFFF;
    uint16_t Vmax_mV = 0;
    uint16_t active_count = 0;

    for (uint8_t i = 0; i < TOTAL_CELLS; i++)
    {
      uint16_t v = all_cell_voltage_mV[i];
      if (v < 500) continue; /* ignore disconnected or invalid cells */
      Vtot_mV += v;
      if (v > Vmax_mV) Vmax_mV = v;
      if (v < Vmin_mV) Vmin_mV = v;
      active_count++;
    }

    if (active_count == 0) {
      Vmin_mV = 0;
      Vmax_mV = 0;
    }

    sprintf(DataLCD, "Vtot:%6lumV ", (unsigned long)Vtot_mV);
    lcd_send_cmd(0x80|0x00);
    lcd_send_string(DataLCD);

    {
      /* Display measured current (one decimal) and temperature (one decimal C) */
      int32_t cur_mA = CurrentSensor_Get_mA();
      int32_t abs_mA = (cur_mA < 0) ? -cur_mA : cur_mA;
      char sign = (cur_mA < 0) ? '-' : ' ';
      int cur_whole = (int)(abs_mA / 1000);
      int cur_tenth = (int)((abs_mA % 1000) / 100);

      int32_t temp_tenths = (int32_t)temperature_max_tenths;
      char temp_sign = (temp_tenths < 0) ? '-' : ' ';
      int32_t temp_abs = (temp_tenths < 0) ? -temp_tenths : temp_tenths;
      int temp_whole = (int)(temp_abs / 10);
      int temp_tenth = (int)(temp_abs % 10);

      snprintf(DataLCD, sizeof(DataLCD), "I:%c%1d.%1dA T:%c%1d.%1d%cC  ", sign, cur_whole, cur_tenth, temp_sign, temp_whole, temp_tenth, 0xDF);
    }
    lcd_send_cmd(0x80|0x40);
    lcd_send_string(DataLCD);

    /* Display Vmin and delta (Dv = Vmax - Vmin) on same line */
    unsigned long dv = (unsigned long)((Vmax_mV >= Vmin_mV) ? (Vmax_mV - Vmin_mV) : 0U);
    /* Keep total line length <=20: use 4 digits for Vmin and 3 for Dv */
    sprintf(DataLCD, "Vmin:%4lumV Dv:%3lumV", (unsigned long)Vmin_mV, dv);
    lcd_send_cmd(0x80|0x14);
    lcd_send_string(DataLCD);

    /* Show Vmax and SOC (one decimal) on the last line; compact to fit 20 chars */
    uint16_t soc_tenths_line = Compute_SOC_TenthsPercent();
    int soc_whole_line = soc_tenths_line / 10;
    int soc_tenth_line = soc_tenths_line % 10;
    sprintf(DataLCD, "Vmax:%4lumV S:%3d.%1d%%", (unsigned long)Vmax_mV, soc_whole_line, soc_tenth_line);
    LCD_WriteLine(0x54, DataLCD);
}

void LCD_Page2(void)
{
    char buf[21];
    sprintf(buf, "C1:%4d C2:%4d", all_cell_voltage_mV[0], all_cell_voltage_mV[1]);
    LCD_WriteLine(0x00, buf);

    sprintf(buf, "C3:%4d C4:%4d", all_cell_voltage_mV[2], all_cell_voltage_mV[3]);
    LCD_WriteLine(0x40, buf);

    sprintf(buf, "C5:%4d C6:%4d", all_cell_voltage_mV[4], all_cell_voltage_mV[5]);
    LCD_WriteLine(0x14, buf);

    sprintf(buf, "C7:%4d C8:%4d", all_cell_voltage_mV[6], all_cell_voltage_mV[7]);
    LCD_WriteLine(0x54, buf);
}

void LCD_Page3(void)
{
    char buf[21];
    sprintf(buf, "C9:%4d C10:%4d", all_cell_voltage_mV[8], all_cell_voltage_mV[9]);
    LCD_WriteLine(0x00, buf);

    sprintf(buf, "C11:%4d C12:%4d", all_cell_voltage_mV[10], all_cell_voltage_mV[11]);
    LCD_WriteLine(0x40, buf);

    sprintf(buf, "C13:%4d C14:%4d", all_cell_voltage_mV[12], all_cell_voltage_mV[13]);
    LCD_WriteLine(0x14, buf);

    sprintf(buf, "C15:%4d", all_cell_voltage_mV[14]);
    LCD_WriteLine(0x54, buf);
}
void LCD_Page4 (void) {
    char buf[21];

    // Dòng 1
    sprintf(buf, "Balancing: %s     ", Digital_In.SW_Balancing ? "ON " : "OFF");
    lcd_send_cmd(0x80|0x00);
    lcd_send_string(buf);

    // Dòng 2
    sprintf(buf, "LabVIEW:   %s     ", Digital_In.SW_LabVIEW ? "ON " : "OFF");
    lcd_send_cmd(0x80|0x40);
    lcd_send_string(buf);

    // Dòng 3
    sprintf(buf, "Load:      %s     ", Digital_In.SW_Load ? "ON " : "OFF");
    lcd_send_cmd(0x80|0x14);
    lcd_send_string(buf);

    // Dòng 4 - clear
    sprintf(buf, "                ");
    lcd_send_cmd(0x80|0x54);
    lcd_send_string(buf);
}
void LCD_PageSetting (void) {
    char buf[21];

    static uint16_t last_ichg = 0xFFFF;
    static uint16_t last_vdis = 0xFFFF;
    static uint16_t last_vchg = 0xFFFF;
    static uint16_t last_idis = 0xFFFF;

    /* Use filtered ADC values to reduce jitter while keeping responsiveness */
    uint16_t Ichg = map(filtered_adc_ichg, 0, ADC_MAX_VALUE, 0, ICHG_MAX_MA);
    uint16_t Vdis = map(filtered_adc_vdis, 0, ADC_MAX_VALUE, 0, VDIS_MAX_MV);
    uint16_t Vchg = map(filtered_adc_vchg, 0, ADC_MAX_VALUE, 0, VCHG_MAX_MV);
    uint16_t Idis = map(filtered_adc_idis, 0, ADC_MAX_VALUE, 0, IDIS_MAX_MA);

    if (Ichg != last_ichg) {
        last_ichg = Ichg;
        sprintf(buf, "Ichg_max:%4dmA    ", Ichg);
        lcd_send_cmd(0x80|0x00);
        lcd_send_string(buf);
    }

    if (Vdis != last_vdis) {
        last_vdis = Vdis;
        sprintf(buf, "Vdis_min:%4dmV    ", Vdis);
        lcd_send_cmd(0x80|0x40);
        lcd_send_string(buf);
    }

    if (Vchg != last_vchg) {
        last_vchg = Vchg;
        sprintf(buf, "Vchg_max:%4dmV    ", Vchg);
        lcd_send_cmd(0x80|0x14);
        lcd_send_string(buf);
    }

    if (Idis != last_idis) {
        last_idis = Idis;
        sprintf(buf, "Idis_max:%5dmA    ", Idis);
        lcd_send_cmd(0x80|0x54);
        lcd_send_string(buf);
    }
}
void LCD_Update () {
    static uint8_t last_page = LCD_PAGE_MAX;
    static uint8_t last_setting = 0;

    /* The main loop schedules setting refreshes at 100 ms to avoid blocking I2C writes. */
    // Force update setting mode HOẶC khi có flag
    if (!lcd_update_flag && last_setting == Digital_In.SW_LCDMode) {
        // Trong setting mode, vẫn cần update liên tục để refresh ADC
        if (Digital_In.SW_LCDMode) return;  // Skip check flag khi ở setting
    }
    lcd_update_flag = 0;

    // Xử lý chuyển setting mode
    if (Digital_In.SW_LCDMode != last_setting) {
        lcd_clear();
        HAL_Delay(10);
        last_setting = Digital_In.SW_LCDMode;
        last_page = LCD_PAGE_MAX;
    }

    // Setting mode - Luôn update
    if (Digital_In.SW_LCDMode) {
        LCD_PageSetting();
        return;
    }

    // Normal page mode - Chỉ update khi đổi page
    if (last_page != lcd_page) {
        lcd_clear();
        HAL_Delay(10);
        last_page = lcd_page;
    }

    switch (lcd_page) {
        case LCD_PAGE_1: LCD_Page1(); break;
        case LCD_PAGE_2: LCD_Page2(); break;
        case LCD_PAGE_3: LCD_Page3(); break;
        case LCD_PAGE_4: LCD_Page4(); break;
    }
}

/* Send CSV telemetry: total,SOC,current,temp,Vmin,Vmax,deltaV,swSetting,swLoad,swBal,cell1...cell15 */
void Send_Cells_UART(void)
{
  /* Only send telemetry when LabVIEW switch is enabled */
  if (!Digital_In.SW_LabVIEW) {
    uart_tx_debug_len = 0;
    return;
  }
  int len = 0;

  /* Compute total, min, max using the same rules as LCD (ignore cells < 500 mV) */
  int32_t total_mV = 0;
  int32_t min_mV = 0;
  int32_t max_mV = 0;
  uint16_t active_count = 0;
  for (int i = 0; i < TOTAL_CELLS_COUNT; i++) {
    int32_t v = (int32_t)all_cell_voltage_mV[i];
    if (v < 500) continue; /* ignore disconnected/invalid cells */
    if (active_count == 0) { min_mV = max_mV = v; }
    if (v < min_mV) min_mV = v;
    if (v > max_mV) max_mV = v;
    total_mV += v;
    active_count++;
  }
  if (active_count == 0) {
    min_mV = 0;
    max_mV = 0;
    total_mV = 0;
  }
  uint32_t deltaV = (uint32_t)(max_mV >= min_mV ? (uint32_t)(max_mV - min_mV) : 0U);

  /* SOC and current unavailable -> set to 0 as requested */
  /* Compute SOC in tenths of percent for UART (LabVIEW will divide by 10) */
  uint16_t soc = Compute_SOC_TenthsPercent();
  int32_t current_mA = CurrentSensor_Get_mA();

  /* Temperature: send as tenths (one decimal) encoded as 3-digit integer (e.g. 25.3 -> 253) */
  int32_t temp_tenths = (int32_t)temperature_max_tenths; /* tenths of °C */

  /* Switch states: 0 = off, 1 = on */
  uint8_t swSetting = (uint8_t)(Digital_In.SW_LCDMode ? 1U : 0U);
  uint8_t swLoad = (uint8_t)(Digital_In.SW_Load ? 1U : 0U);
  uint8_t swBal = (uint8_t)(Digital_In.SW_Balancing ? 1U : 0U);

  /* Build CSV header fields */
    len += snprintf(uart_tx_debug_buf + len, sizeof(uart_tx_debug_buf) - len,
      "%ld,%u,%ld,%03d,%ld,%ld,%lu,%u,%u,%u,",
      (long)total_mV, (unsigned)soc, (long)current_mA, (int)temp_tenths,
      (long)min_mV, (long)max_mV, (unsigned long)deltaV,
      (unsigned)swSetting, (unsigned)swLoad, (unsigned)swBal);

  /* Append cell voltages (mV) */
  for (int i = 0; i < TOTAL_CELLS_COUNT; i++) {
    uint32_t mv = (uint32_t)all_cell_voltage_mV[i];
    if (i < TOTAL_CELLS_COUNT - 1) {
      len += snprintf(uart_tx_debug_buf + len, sizeof(uart_tx_debug_buf) - len, "%lu,", (unsigned long)mv);
    } else {
      len += snprintf(uart_tx_debug_buf + len, sizeof(uart_tx_debug_buf) - len, "%lu", (unsigned long)mv);
    }
    if (len >= (int)sizeof(uart_tx_debug_buf) - 16) break; /* guard */
  }

  /* Terminate with newline */
  if (len < (int)sizeof(uart_tx_debug_buf) - 2) uart_tx_debug_buf[len++] = '\n';

  if (len > 0) {
    uart_tx_debug_len = (uint16_t)len;
    if (HAL_UART_GetState(&huart1) == HAL_UART_STATE_READY) {
      HAL_UART_Transmit_IT(&huart1, (uint8_t *)uart_tx_debug_buf, uart_tx_debug_len);
    }
  } else {
    uart_tx_debug_len = 0;
  }
}
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
