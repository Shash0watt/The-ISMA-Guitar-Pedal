// ADC: VADC group 0, channel 0, which is AUDIO_INPUT_PIN.
//
// Oversampling: each adc_start_conversion() queues ADC_OVERSAMPLE back-to-back
// conversions (~1 us each). The result register adds them up in hardware and
// adc_read() returns the average. Averaging 4 halves the random noise (+6 dB).
#include "../board.h"
#include "xmc_vadc.h"

#if ADC_OVERSAMPLE != 1 && ADC_OVERSAMPLE != 2 && ADC_OVERSAMPLE != 4
#error "ADC_OVERSAMPLE must be 1, 2 or 4"
#endif

// Ungate the VADC clock, release its reset, then switch the module on.
static void enable_adc_module(void)
{
    XMC_VADC_GLOBAL_EnableModule();
    VADC->CLC = 0;
}

// Converter clock fADCI = 144 MHz / (DIVA + 1) = 28.8 MHz. Must be <= 30 MHz.
// DIVWC lets the DIVA write take effect.
static void set_converter_clock(void)
{
    VADC->GLOBCFG = (4UL << VADC_GLOBCFG_DIVA_Pos) | VADC_GLOBCFG_DIVWC_Msk;
}

// ANONC = 3: the group 0 converter stays powered (normal operation).
static void power_on_converter(void)
{
    VADC_G0->ARBCFG = (3UL << VADC_G_ARBCFG_ANONC_Pos);
}

static void run_startup_calibration(void)
{
    VADC->GLOBCFG |= VADC_GLOBCFG_SUCAL_Msk;
    while (VADC_G0->ARBCFG & VADC_G_ARBCFG_CAL_Msk) { }
}

// Input class 0: 12-bit (CMS = 0), sample time = (2 + STCS) converter clocks
// = 12 / 28.8 MHz = 417 ns. Channel 0 uses that class and writes to result 0.
static void configure_channel(void)
{
    VADC_G0->ICLASS[0] = (10UL << VADC_G_ICLASS_STCS_Pos);
    VADC_G0->CHCTR[0] = (0UL << VADC_G_CHCTR_ICLSEL_Pos) |
                        (0UL << VADC_G_CHCTR_RESREG_Pos);
}

// DRCTR = N - 1: result 0 adds up N conversions, and its valid flag sets
// after the last one.
static void configure_result_averaging(void)
{
    VADC_G0->RCR[0] = ((uint32_t)(ADC_OVERSAMPLE - 1) << VADC_G_RCR_DRCTR_Pos);
}

// Queue source 0: ENGT = 1 converts entries as soon as they are queued,
// ASEN0 gives the queue its arbiter slot.
static void enable_conversion_queue(void)
{
    VADC_G0->QMR0   = (1UL << VADC_G_QMR0_ENGT_Pos);
    VADC_G0->ARBPR |= VADC_G_ARBPR_ASEN0_Msk;
}

// The audio input is analog only: turn off its digital input buffer.
static void disable_pin_digital_input(void)
{
    XMC_GPIO_DisableDigitalInput(AUDIO_INPUT_PIN);
}

void init_adc(void)
{
    enable_adc_module();
    set_converter_clock();
    power_on_converter();
    run_startup_calibration();
    configure_channel();
    configure_result_averaging();
    enable_conversion_queue();
    disable_pin_digital_input();

    adc_start_conversion();                 // so the first adc_read() is valid
}

// Queues N entries of channel 0 with no refill: exactly N conversions, back
// to back, all starting right at the call.
void adc_start_conversion(void)
{
    for (uint32_t conversion = 0; conversion < ADC_OVERSAMPLE; conversion++)
    {
        VADC_G0->QINR0 = (0UL << VADC_G_QINR0_REQCHNR_Pos);
    }
}

// Result 0 holds the sum of N 12-bit conversions; return the rounded average.
uint16_t adc_read(void)
{
    uint32_t sum = VADC_G0->RES[0] & 0xFFFFU;
    return (uint16_t)((sum + ADC_OVERSAMPLE / 2) / ADC_OVERSAMPLE);
}
