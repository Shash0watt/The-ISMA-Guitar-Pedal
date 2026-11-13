// DAC: DAC0, which drives AUDIO_OUTPUT_PIN. Single-value mode: a write updates
// the pin immediately.
#include "../board.h"
#include "xmc_dac.h"

#define DAC_MID_SCALE 2048

void init_dac(void)
{
    XMC_DAC_Enable(XMC_DAC0);                        // ungate clock + release reset
    DAC->DAC0CFG0 = (1UL << DAC_DAC0CFG0_MODE_Pos);  // single-value mode, unsigned data (SIGN = 0)
    DAC->DAC0CFG1 = DAC_DAC0CFG1_ANAEN_Msk;          // analog output on, no scaling
    DAC->DAC0DATA = DAC_MID_SCALE;                   // start at silence

    XMC_GPIO_DisableDigitalInput(AUDIO_OUTPUT_PIN);  // analog only: digital input buffer off
}

// The data register is 12 bits wide; upper bits of the code are dropped.
void dac_write(uint16_t code)
{
    DAC->DAC0DATA = code & 0xFFFU;
}
