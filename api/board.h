#ifndef BOARD_H
#define BOARD_H

#ifdef __cplusplus
extern "C" {
#endif

// === Pin Setup ===


// === Rotary Encoder ===
// A timer polls the rotary encoder for a change 
// MAKE SURE IT"S LOWER PRIORITY HAN AUDIO

// Starts a timer that reads the rotary encoder's changes
void init_rotary_encoder();


// === DAC





#ifdef __cplusplus
}
#endif

#endif /* BOARD_H */
