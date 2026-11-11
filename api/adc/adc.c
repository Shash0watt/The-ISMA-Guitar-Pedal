// ADC: VADC group 0, channel 0 (P14.0). See api/README.md.
//
// Oversampling: each adc_start_conversion() queues ADC_OVERSAMPLE back-to-back
// conversions of CH0 (~1 us each). The result register adds them up in hardware
// (data reduction, RCR.DRCTR), and adc_read() returns the average. Averaging 4
// halves the random noise (+6 dB). All conversions start right at the tick, so
// the sample instant stays fixed.
#include "../board.h"
#include "xmc_vadc.h"

#if ADC_OVERSAMPLE != 1 && ADC_OVERSAMPLE != 2 && ADC_OVERSAMPLE != 4
#error "ADC_OVERSAMPLE must be 1, 2 or 4"
#endif

void init_adc(void)
{
    XMC_VADC_GLOBAL_EnableModule();         // ungate clock + release reset
    VADC->CLC = 0;                          // module clock on

    // fADC = fPERIPH = 144 MHz. Analog clock fADCI = 144 / (DIVA+1) = 28.8 MHz
    // (must be <= 30 MHz). DIVWC lets the DIVA write take effect.
    VADC->GLOBCFG = (4UL << VADC_GLOBCFG_DIVA_Pos) | VADC_GLOBCFG_DIVWC_Msk;

    // Group 0 converter: normal operation (ANONC = 3)
    VADC_G0->ARBCFG = (3UL << VADC_G_ARBCFG_ANONC_Pos);

    // Start-up calibration, wait for it to finish
    VADC->GLOBCFG |= VADC_GLOBCFG_SUCAL_Msk;
    while (VADC_G0->ARBCFG & VADC_G_ARBCFG_CAL_Msk) { }

    // Input class 0 of group 0: 12-bit (CMS = 0), sample time = (2 + STCS)
    // analog clocks = 12 / 28.8 MHz = 417 ns.
    VADC_G0->ICLASS[0] = (10UL << VADC_G_ICLASS_STCS_Pos);

    // Channel 0: input class = group class 0, result -> G0RES0
    VADC_G0->CHCTR[0] = (0UL << VADC_G_CHCTR_ICLSEL_Pos) |
                        (0UL << VADC_G_CHCTR_RESREG_Pos);

    // Result 0: accumulate ADC_OVERSAMPLE results (DRCTR = N-1, DMM = 0).
    // RES0 then holds the sum, and its valid flag sets after the last one.
    VADC_G0->RCR[0] = ((uint32_t)(ADC_OVERSAMPLE - 1) << VADC_G_RCR_DRCTR_Pos);

    // Queue source 0 (arbiter slot 0): convert whatever entries are queued.
    VADC_G0->QMR0   = (1UL << VADC_G_QMR0_ENGT_Pos);   // issue requests when entries pending
    VADC_G0->ARBPR |= VADC_G_ARBPR_ASEN0_Msk;          // enable queue arbiter slot

    PORT14->PDISC |= (1UL << 0);            // P14.0: disable digital input buffer

    adc_start_conversion();                 // so the first adc_read() is valid
}

void adc_start_conversion(void)
{
    // N entries of CH0, no refill: exactly N conversions, back to back.
    for (uint32_t i = 0; i < ADC_OVERSAMPLE; i++)
    {
        VADC_G0->QINR0 = (0UL << VADC_G_QINR0_REQCHNR_Pos);
    }
}

uint16_t adc_read(void)
{
    // Sum of N 12-bit results -> rounded 12-bit average
    uint32_t sum = VADC_G0->RES[0] & 0xFFFFU;
    return (uint16_t)((sum + ADC_OVERSAMPLE / 2) / ADC_OVERSAMPLE);
}
