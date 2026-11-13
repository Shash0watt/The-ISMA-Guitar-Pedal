// The steps of main(): board start-up, the 1 ms loop timing, and the on-board
// buttons, LEDs and meters. main.c explains what the buttons and LEDs mean.
#include "main_helpers.h"
#include "cybsp.h"
#include "cy_utils.h"
#include "xmc_gpio.h"
#include "api/board.h"
#include <stdio.h>

typedef enum { MODE_PASSTHROUGH = 0, MODE_SINE, MODE_SILENCE, MODE_COUNT } bringup_mode_t;

#define BOOT_BEEP_MS            1000U
#define STATUS_TICK_MS          50U       // LED / button / meter update period
#define SIGNAL_THRESHOLD        64U       // DAC counts from mid-scale (~35 mV)
#define BIAS_LOW                1700U     // healthy bias is ~2048 (1.65 V)
#define BIAS_HIGH               2400U
#define SAMPLES_PER_MILLISECOND (AUDIO_SAMPLE_RATE_HZ / 1000U)

// Watch these in the debugger (Live Watch).
volatile bringup_mode_t dbg_mode;
volatile uint16_t dbg_in_mean;      // ~2048 when the input bias is healthy
volatile uint16_t dbg_out_peak;

static bringup_mode_t current_mode;
static uint32_t boot_beep_ticks_left;       // status ticks until the boot beep ends; 0 = no beep
static uint32_t next_wake_sample = SAMPLES_PER_MILLISECOND;
static uint32_t milliseconds_elapsed;
static uint32_t status_ticks;               // counts 50 ms ticks; times the LED blinking
static bool test_mode_button_was_down;
static bool effect_button_was_down;
static uint16_t input_mean;                 // latest meter readings
static uint16_t output_peak;


// printf goes to the debug UART: the C library sends all its output through _write().
int _write(int file, char *text, int length)
{
    (void)file;
    for (int index = 0; index < length; index++)
    {
        if (text[index] == '\n')
        {
            XMC_UART_CH_Transmit(CYBSP_DEBUG_UART_HW, '\r');    // terminals want \r\n
        }
        XMC_UART_CH_Transmit(CYBSP_DEBUG_UART_HW, text[index]);
    }
    return length;
}


// --- small private helpers ---

static void set_mode(bringup_mode_t mode)
{
    static const audio_test_mode_t audio_test_modes[MODE_COUNT] = {
        [MODE_PASSTHROUGH] = AUDIO_TEST_NONE,
        [MODE_SINE]        = AUDIO_TEST_SINE_1K,
        [MODE_SILENCE]     = AUDIO_TEST_MIDSCALE,
    };
    audio_set_test_mode(audio_test_modes[mode]);
    current_mode = mode;
    dbg_mode = mode;
}

static void set_led(XMC_GPIO_PORT_t *port, uint8_t pin, bool on)
{
    if (on) { XMC_GPIO_SetOutputHigh(port, pin); }
    else    { XMC_GPIO_SetOutputLow(port, pin); }
}

// True for one status tick only: the tick where the button goes from up to
// down. Looking just once every 50 ms also debounces the button.
static bool button_was_just_pressed(XMC_GPIO_PORT_t *port, uint8_t pin, bool *button_was_down)
{
    bool button_is_down = XMC_GPIO_GetInput(port, pin) == CYBSP_BTN_PRESSED;
    bool just_pressed = button_is_down && !*button_was_down;
    *button_was_down = button_is_down;
    return just_pressed;
}

// Blink pattern: flips every `ticks_per_flip` status ticks (50 ms each),
// so 10 -> 1 Hz, 2 -> 5 Hz, 1 -> 10 Hz.
static bool blink_is_on(uint32_t ticks_per_flip)
{
    return (status_ticks / ticks_per_flip) & 1U;
}

static bool input_bias_is_out_of_range(void)
{
    return (input_mean < BIAS_LOW) || (input_mean > BIAS_HIGH);
}


// --- start-up ---

void init_board(void)
{
    cy_rslt_t result = cybsp_init();
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }
}

// If you hear the beep, the DAC, the output circuit and the headphones all work.
void start_boot_beep(void)
{
    boot_beep_ticks_left = BOOT_BEEP_MS / STATUS_TICK_MS;
    set_mode(MODE_SINE);
}


// --- every 1 ms ---

// Time is counted in audio samples, because the audio engine owns SysTick.
// The subtraction is compared as a signed number so this keeps working when
// the sample counter wraps around.
void wait_for_next_millisecond(void)
{
    while ((int32_t)(audio_sample_count() - next_wake_sample) < 0) { }
    next_wake_sample += SAMPLES_PER_MILLISECOND;
}

bool status_tick_is_due(void)
{
    milliseconds_elapsed++;
    if (milliseconds_elapsed % STATUS_TICK_MS != 0) { return false; }
    status_ticks++;
    return true;
}


// --- every status tick (50 ms) ---

void end_boot_beep_when_done(void)
{
    if (boot_beep_ticks_left == 0) { return; }      // no beep playing

    boot_beep_ticks_left--;
    if (boot_beep_ticks_left == 0)
    {
        set_mode(MODE_PASSTHROUGH);
    }
}

void handle_test_mode_button(void)
{
    if (button_was_just_pressed(TEST_MODE_BUTTON_PIN, &test_mode_button_was_down))
    {
        boot_beep_ticks_left = 0;                   // the button cancels the boot beep
        set_mode((bringup_mode_t)((current_mode + 1) % MODE_COUNT));
    }
}

void handle_effect_button(void)
{
    if (button_was_just_pressed(NEXT_EFFECT_BUTTON_PIN, &effect_button_was_down))
    {
        pedal_next_effect();
    }
}

// Reading a meter clears it, so each one is read once per tick and kept here
// for the LEDs.
void update_meters(void)
{
    input_mean  = audio_in_mean();
    output_peak = audio_out_peak();
    dbg_in_mean  = input_mean;
    dbg_out_peak = output_peak;
}

void update_status_leds(void)
{
    bool test_mode_led_on;
    switch (current_mode)
    {
        case MODE_PASSTHROUGH: test_mode_led_on = blink_is_on(10); break;    // 1 Hz
        case MODE_SINE:        test_mode_led_on = blink_is_on(2);  break;    // 5 Hz
        default:               test_mode_led_on = true;            break;    // silence: solid
    }
    set_led(TEST_MODE_LED_PIN, test_mode_led_on);

    bool signal_led_on = input_bias_is_out_of_range() ? blink_is_on(1)       // 10 Hz
                                                      : (output_peak > SIGNAL_THRESHOLD);
    set_led(SIGNAL_LED_PIN, signal_led_on);
}
