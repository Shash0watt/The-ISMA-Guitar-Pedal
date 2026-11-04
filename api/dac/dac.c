// DAC: DAC0 on P14.8, single-value mode. See api/README.md.
#include "../board.h"
#include "xmc_dac.h"

void init_dac(void)
{
    XMC_DAC_Enable(XMC_DAC0);                        // ungate clock + release reset
    DAC->DAC0CFG0 = (1UL << DAC_DAC0CFG0_MODE_Pos);  // single value, SIGN = 0 (unsigned)
    DAC->DAC0CFG1 = DAC_DAC0CFG1_ANAEN_Msk;          // analog output on, no scaling
    DAC->DAC0DATA = 2048;                            // start at mid-scale

    PORT14->PDISC |= (1UL << 8);                     // P14.8: disable digital input buffer
}

void dac_write(uint16_t code)
{
    DAC->DAC0DATA = code & 0xFFFU;
}
