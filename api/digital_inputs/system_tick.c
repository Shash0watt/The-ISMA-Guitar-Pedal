#include "../board.h"
#include "xmc_common.h"
#include <stdio.h>

void init_systick()
{
    // Make sure the SystemCoreClock value is correct (recompute from the clock registers)
    
    SystemCoreClockUpdate();

    // SysTick_Config - Creates a periodic SysTick interrupt using the SysTick timer, 
    // with a interval defined by the ticks parameter
    // System Core Clock - global variable holding the current frequency of the processor
    //  core clock in Hertz (144 Mhz)
    SysTick_Config(SystemCoreClock / 1000U); // ~ every 1ms
}


void SysTick_Handler(){
    poll_encoders();
    poll_buttons();

    // Example: 
    // print the encoder's current position when its button is pressed.
    // is_pressed stays 1 the whole time it's held, so only print on the
    // 0 -> 1 change (otherwise it would print every 1 ms while held).
    static uint8_t was_pressed = 0;
    uint8_t pressed = encoder_is_pressed(0);
    if (pressed && !was_pressed) {
        printf("pressed, position: %ld\n", (long)encoder_get_position(0));
    }
    was_pressed = pressed;
}