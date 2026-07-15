# Plan: Fix LCD Setting Mode Flickering

## Problem Analysis

**Root Cause:** In `LCD_PageSetting()` (main.c:1470), every call unconditionally writes all 4 LCD lines via I2C, even when the values haven't changed. This causes:
- Severe flickering on the LCD (I2C writes are slow ~100kHz)
- Potentiometer adjustments feel sluggish because each ADC change triggers full I2C redraw
- The existing EMA filter (SETTINGS_FILTER_SHIFT=2) in `Read_Analog_Input()` helps but doesn't prevent redundant I2C writes

**Current Code Flow:**
1. `Read_Analog_Input()` runs every ~20ms (main loop delay)
2. EMA filter smooths ADC values with shift=2 (fast response, some noise remains)
3. `LCD_Update()` calls `LCD_PageSetting()` every ~100ms (when in setting mode)
4. `LCD_PageSetting()` blindly writes all 4 lines every call via slow I2C

## Solution Plan

### 1. Modify `LCD_PageSetting()` to Only Write Changed Lines (main.c:1470)
- Add `static` variables to track last displayed values for each of the 4 settings
- Only call `lcd_send_cmd()` + `lcd_send_string()` when the mapped value actually changes
- This eliminates redundant I2C traffic causing flicker

### 2. Tune EMA Filter (Optional, main.c:71)
- Current: `#define SETTINGS_FILTER_SHIFT 2U` (fast response, ~25% weight per sample)
- Consider: `#define SETTINGS_FILTER_SHIFT 3U` (smoother, ~12.5% weight per sample)
- The deadband `SETTINGS_DEADBAND_ADC = 2` already helps ignore small ADC noise
- **Recommendation**: Try shift=3 first; only change if potentiometer feels too sluggish

### 3. Ensure LCD Update Rate in Setting Mode (main.c:1501)
- Current `LCD_Update()` logic at line 1507-1510 returns early in setting mode when `lcd_update_flag=0`
- This is correct - it forces continuous updates in setting mode
- The fix in step 1 makes these updates cheap (only changed lines written)

## Files to Modify
- `Software/Master/Core/Src/main.c` - `LCD_PageSetting()` function (lines 1470-1500)

## Implementation Details

```c
void LCD_PageSetting (void) {
    static uint16_t last_ichg = 0xFFFF;
    static uint16_t last_vdis = 0xFFFF;
    static uint16_t last_vchg = 0xFFFF;
    static uint16_t last_idis = 0xFFFF;
    char buf[21];

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
```

## Validation
1. Build with Keil MDK (no hardware needed for compile check)
2. On hardware: verify LCD setting values are stable (±1-2 counts max)
3. Verify potentiometer adjustment feels responsive
4. Verify normal LCD pages (1-4) still update correctly

## Risks
- Low risk: Only changes LCD write logic in setting mode
- Static variables initialized to 0xFFFF ensure first draw always happens
- No change to ADC filtering or main loop timing