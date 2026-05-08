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
#include "usb_device.h"
#include "usbd_hid.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* String the keyboard types when PA0 is pressed. Edit this and reflash to
 * change what gets typed. Supports printable ASCII plus '\n' (Enter) and
 * '\t' (Tab). Anything else is silently skipped by AsciiToHid below. */
#ifndef KEYBUM_TYPE_STRING
#define KEYBUM_TYPE_STRING "Hello from STM32 keybum!\nHello from STM32 keybum!\nHello from STM32 keybum!\n"
#endif

/* Delay before the initial auto-type after enumeration.
 * Gives the host time to finish binding HID and lets you focus a text field. */
#ifndef KEYBUM_STARTUP_DELAY_MS
#define KEYBUM_STARTUP_DELAY_MS 4000u
#endif

/* HID modifier bits */
#define KEY_MOD_LCTRL  0x01u
#define KEY_MOD_LSHIFT 0x02u
#define KEY_MOD_LALT   0x04u
#define KEY_MOD_LGUI   0x08u
#define KEY_MOD_RCTRL  0x10u
#define KEY_MOD_RSHIFT 0x20u
#define KEY_MOD_RALT   0x40u
#define KEY_MOD_RGUI   0x80u

/* HID Usage IDs we use directly */
#define KEY_NONE     0x00u
#define KEY_ENTER    0x28u
#define KEY_TAB      0x2Bu
#define KEY_SPACE    0x2Cu

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
extern USBD_HandleTypeDef hUsbDeviceFS;

/* HID boot keyboard report (8 bytes): modifier, reserved, 6 keycodes. */
typedef struct {
    uint8_t modifier;
    uint8_t reserved;
    uint8_t keys[6];
} Keyboard_Report;

static Keyboard_Report kbd_report = {0, 0, {0, 0, 0, 0, 0, 0}};

/* xorshift32 PRNG state — seeded once in main() before any GetRandom() call */
static uint32_t prng_state = 0x9E3779B9u; /* fallback seed; replaced at runtime */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
// xorshift32: tiny, fast, no peripheral required. NOT cryptographic — good
// enough to randomize keystroke timing so typing looks organic.
static inline uint32_t xorshift32(void) {
    uint32_t x = prng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    prng_state = x;
    return x;
}

// Seed the PRNG from the DWT cycle counter (free-running CPU cycles) XOR'd
// with the current systick tick. DWT must already be enabled before calling.
static void SeedRandom(void) {
    uint32_t s = DWT->CYCCNT ^ HAL_GetTick() ^ 0xA3C59B71u;
    if (s == 0) s = 0x9E3779B9u; // xorshift mustn't be seeded with zero
    prng_state = s;
}

// Helper to get a random number in a range [min, max] inclusive
uint32_t GetRandom(uint32_t min, uint32_t max) {
    uint32_t val = xorshift32();
    return (val % (max - min + 1)) + min;
}

/* Send a single 8-byte HID report, retrying while the IN endpoint is busy.
 * Without the retry loop, fast back-to-back reports can be silently dropped
 * because USBD_HID_SendReport refuses while a previous transfer is in flight. */
static void SendReportBlocking(void) {
    uint8_t status;
    do {
        status = USBD_HID_SendReport(&hUsbDeviceFS,
                                     (uint8_t *)&kbd_report,
                                     sizeof(kbd_report));
        if (status == USBD_BUSY) {
            HAL_Delay(1);
        }
    } while (status == USBD_BUSY);
}

/* Convert an ASCII char to (HID keycode, modifier). Returns 1 if the char
 * is typeable on a US-layout keyboard, 0 if it should be skipped.
 *
 * Note: this implements the US-layout map only. Hosts with a non-US keyboard
 * layout configured will see different characters land in their input field
 * because HID keyboards send keycodes (positions), not characters — the
 * host's active keymap decides the glyph. If you need a different layout,
 * either change the host keymap or rewrite this function for that layout. */
static uint8_t AsciiToHid(char c, uint8_t *keycode, uint8_t *modifier) {
    *modifier = 0;
    *keycode  = 0;

    if (c >= 'a' && c <= 'z') {
        *keycode = (uint8_t)(0x04 + (c - 'a'));   /* 'a' = 0x04 */
        return 1;
    }
    if (c >= 'A' && c <= 'Z') {
        *keycode  = (uint8_t)(0x04 + (c - 'A'));
        *modifier = KEY_MOD_LSHIFT;
        return 1;
    }
    if (c >= '1' && c <= '9') {
        *keycode = (uint8_t)(0x1E + (c - '1'));   /* '1' = 0x1E */
        return 1;
    }
    if (c == '0') {
        *keycode = 0x27;
        return 1;
    }
    if (c == ' ')  { *keycode = KEY_SPACE; return 1; }
    if (c == '\n') { *keycode = KEY_ENTER; return 1; }
    if (c == '\t') { *keycode = KEY_TAB;   return 1; }

    /* US-layout punctuation. Each entry: (unshifted, shifted) — both share
     * the same key position, only the modifier differs. */
    switch (c) {
        case '-': *keycode = 0x2D; return 1;
        case '_': *keycode = 0x2D; *modifier = KEY_MOD_LSHIFT; return 1;
        case '=': *keycode = 0x2E; return 1;
        case '+': *keycode = 0x2E; *modifier = KEY_MOD_LSHIFT; return 1;
        case '[': *keycode = 0x2F; return 1;
        case '{': *keycode = 0x2F; *modifier = KEY_MOD_LSHIFT; return 1;
        case ']': *keycode = 0x30; return 1;
        case '}': *keycode = 0x30; *modifier = KEY_MOD_LSHIFT; return 1;
        case '\\':*keycode = 0x31; return 1;
        case '|': *keycode = 0x31; *modifier = KEY_MOD_LSHIFT; return 1;
        case ';': *keycode = 0x33; return 1;
        case ':': *keycode = 0x33; *modifier = KEY_MOD_LSHIFT; return 1;
        case '\'':*keycode = 0x34; return 1;
        case '"': *keycode = 0x34; *modifier = KEY_MOD_LSHIFT; return 1;
        case '`': *keycode = 0x35; return 1;
        case '~': *keycode = 0x35; *modifier = KEY_MOD_LSHIFT; return 1;
        case ',': *keycode = 0x36; return 1;
        case '<': *keycode = 0x36; *modifier = KEY_MOD_LSHIFT; return 1;
        case '.': *keycode = 0x37; return 1;
        case '>': *keycode = 0x37; *modifier = KEY_MOD_LSHIFT; return 1;
        case '/': *keycode = 0x38; return 1;
        case '?': *keycode = 0x38; *modifier = KEY_MOD_LSHIFT; return 1;
        case '!': *keycode = 0x1E; *modifier = KEY_MOD_LSHIFT; return 1; /* shift+1 */
        case '@': *keycode = 0x1F; *modifier = KEY_MOD_LSHIFT; return 1; /* shift+2 */
        case '#': *keycode = 0x20; *modifier = KEY_MOD_LSHIFT; return 1;
        case '$': *keycode = 0x21; *modifier = KEY_MOD_LSHIFT; return 1;
        case '%': *keycode = 0x22; *modifier = KEY_MOD_LSHIFT; return 1;
        case '^': *keycode = 0x23; *modifier = KEY_MOD_LSHIFT; return 1;
        case '&': *keycode = 0x24; *modifier = KEY_MOD_LSHIFT; return 1;
        case '*': *keycode = 0x25; *modifier = KEY_MOD_LSHIFT; return 1;
        case '(': *keycode = 0x26; *modifier = KEY_MOD_LSHIFT; return 1;
        case ')': *keycode = 0x27; *modifier = KEY_MOD_LSHIFT; return 1;
    }
    /* Unsupported character — skip silently. */
    return 0;
}

/* Send "no keys pressed" report. Required between successive presses of the
 * same key, otherwise the host sees one long hold instead of two presses. */
static void ReleaseAllKeys(void) {
    kbd_report.modifier = 0;
    kbd_report.reserved = 0;
    memset(kbd_report.keys, 0, sizeof(kbd_report.keys));
    SendReportBlocking();
}

/* Press one key (with optional modifier), then release. The two delays
 * model a human keystroke:
 *   - hold time: how long a finger stays on the key (40–110 ms)
 *   - inter-key gap: pause before the next character (60–180 ms)
 * Both are randomized per-keystroke so the cadence isn't a giveaway. */
static void PressKey(uint8_t keycode, uint8_t modifier) {
    kbd_report.modifier = modifier;
    kbd_report.reserved = 0;
    kbd_report.keys[0]  = keycode;
    /* slots 1..5 stay zero — single-key presses only */
    memset(&kbd_report.keys[1], 0, sizeof(kbd_report.keys) - 1);
    SendReportBlocking();
    HAL_Delay(GetRandom(40, 110)); /* key down hold */

    ReleaseAllKeys();
    HAL_Delay(GetRandom(60, 180)); /* gap before next char */
}

/* Type a NUL-terminated ASCII string with human-like timing. */
static void TypeString(const char *s) {
    while (*s) {
        uint8_t kc = 0, mod = 0;
        if (AsciiToHid(*s, &kc, &mod)) {
            PressKey(kc, mod);

            /* Occasional longer "thinking" pause after spaces so the rhythm
             * isn't perfectly regular even in the middle of typing. */
            if (*s == ' ' && GetRandom(0, 9) == 0) {
                HAL_Delay(GetRandom(150, 400));
            }
        }
        s++;
    }
    /* Belt-and-braces: make sure the last keypress is fully released. */
    ReleaseAllKeys();
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
  MX_USB_DEVICE_Init();
  /* USER CODE BEGIN 2 */
  /* Enable the DWT cycle counter so we can use it to seed the PRNG.
   * DWT->CYCCNT runs at SYSCLK (96 MHz) and gives us a non-deterministic
   * value at the moment of seeding because USB enumeration above takes
   * a variable number of cycles. */
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
  SeedRandom();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  /* Wait for the USB host to enumerate and configure us before we send
   * any HID reports — reports sent before this point are lost. */
  while (hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED) {
      HAL_Delay(10);
  }
  /* Brief settle delay after enumeration so the OS finishes binding the HID
   * driver before we start typing. */
  HAL_Delay(1500);

  /* Extra grace time before first payload so host/input focus is ready. */
  HAL_Delay(KEYBUM_STARTUP_DELAY_MS);

  /* Auto-type once right after enumeration so the payload is sent on plug-in
   * even if no trigger button is connected. */
  TypeString(KEYBUM_TYPE_STRING);

  /* Keep PA0 as an optional manual retrigger input. Track press edges so we
   * type once per press, not continuously while held.
   * PA0 is active-low (GPIO_PIN_RESET == pressed). */
  GPIO_PinState last_btn = GPIO_PIN_SET;

  while (1)
  {
      GPIO_PinState btn = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0);

      /* Falling edge: button just got pressed. Type the configured string
       * exactly once, then wait for release before arming again. */
      if (btn == GPIO_PIN_RESET && last_btn == GPIO_PIN_SET) {
          /* Tiny debounce — ignore bounces shorter than ~20 ms. */
          HAL_Delay(20);
          if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_RESET) {
              TypeString(KEYBUM_TYPE_STRING);
          }
      }
      last_btn = btn;
      HAL_Delay(10);
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

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 25;
  RCC_OscInitStruct.PLL.PLLN = 192;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
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

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
  {
    Error_Handler();
  }
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
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /* USER CODE BEGIN MX_GPIO_Init_2 */
  /* Configure PA0 as input with pull-up for trigger button (active-low) */
  GPIO_InitStruct.Pin = GPIO_PIN_0;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
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
