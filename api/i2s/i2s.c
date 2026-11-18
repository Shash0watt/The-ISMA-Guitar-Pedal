// I2S: USIC2 channel 1 as I2S master, talking to the PCM1808 ADC and the
// PCM5102 DAC at the same time. Wiring and clock plan: api/xmc4700-audio-io-context.md.
//
//   MCLK  P3.4   12 MHz      (256 * fs) -> PCM1808 SCKI. PCM5102 SCK is tied to GND.
//   BCK   P3.13  3 MHz       (64 * fs)  -> both converters
//   LRCK  P3.0   46.875 kHz  (fs)       -> both converters
//   DIN   P3.12  <- PCM1808 DOUT
//   DOUT  P3.11  -> PCM5102 DIN
//
// USIC2 ch1 is also the channel for the board's QSPI flash, but the flash is
// wired to P4.2..P4.7, which this doesn't touch. Nothing in the pedal uses it.
//
// Each channel is a 32-bit slot, moved as two 16-bit USIC words: the high
// word (the top 16 bits of the converters' 24-bit sample) and the low word.
// So a stereo frame is 4 words each way. We send 0 as the low word and ignore
// the low word we receive, which gives 16-bit audio.
// (Moving just 16 bits per slot left the right channel silent on the PCM5102.)
//
// FIFOs: transmit = 16 words of the USIC buffer, receive = the next 16.
// The receive FIFO raises its interrupt (service request 0 -> USIC2_0_IRQn)
// when it holds a full frame (4 words). The audio engine's handler then reads
// that frame and writes one frame to the transmit FIFO.
#include "../board.h"
#include "xmc_i2s.h"
#include "xmc_scu.h"

#define I2S_CHANNEL         XMC_USIC2_CH1
#define I2S_MCLK_HZ         12000000UL
#define I2S_SLOT_BITS       32U    // BCK periods per channel: 64 * fs per frame
#define I2S_WORD_BITS       16U    // USIC word: the most it moves at once
#define WORDS_PER_FRAME     4U     // left high, left low, right high, right low
#define FIFO_WORDS          XMC_USIC_CH_FIFO_SIZE_16WORDS
#define TX_FIFO_START       0U
#define RX_FIFO_START       16U
#define TX_PRIME_FRAMES     2U     // silent frames queued before starting: underrun margin

// Receive control info, RCIM = 3: OUTR bit 16 is SOF, set on the first word
// after LRCK changes, i.e. a high word. Used to stay aligned to the frame.
#define RX_INFO_MODE_SOF    (3UL << USIC_CH_RBCTR_RCIM_Pos)
#define OUTR_SOF            (1UL << USIC_CH_OUTR_RCI_Pos)

// Last 4 raw received words (OUTR, with the info bits), for the debugger.
volatile uint32_t i2s_rx_raw[WORDS_PER_FRAME];

// P3.12 as USIC2 ch1 input DX0D.
#define I2S_DATA_IN_SOURCE  USIC2_C1_DX0_P3_12
#define I2S_IRQn            USIC2_0_IRQn

static void init_pins(void)
{
    XMC_GPIO_CONFIG_t output = { .mode = XMC_GPIO_MODE_OUTPUT_PUSH_PULL_ALT1,   // U2C1 on all four
                                 .output_level = XMC_GPIO_OUTPUT_LEVEL_LOW,
                                 .output_strength = XMC_GPIO_OUTPUT_STRENGTH_STRONG_MEDIUM_EDGE };
    XMC_GPIO_CONFIG_t input  = { .mode = XMC_GPIO_MODE_INPUT_TRISTATE };

    XMC_GPIO_Init(I2S_MCLK_PIN,     &output);
    XMC_GPIO_Init(I2S_BCK_PIN,      &output);
    XMC_GPIO_Init(I2S_LRCK_PIN,     &output);
    XMC_GPIO_Init(I2S_DATA_OUT_PIN, &output);
    XMC_GPIO_Init(I2S_DATA_IN_PIN,  &input);
}

// Clock chain (USIC baud rate generator, normal divider mode):
//   fFD  = fPERIPH / (1024 - STEP)      = 24 MHz   (144 MHz / 6)
//   MCLK = fFD / 2           (PPPEN)    = 12 MHz
//   BCK  = MCLK / (PDIV + 1) / 2        = 3 MHz    (PDIV = 1)
//   LRCK = BCK / (2 * I2S_SLOT_BITS)    = 46.875 kHz
// Every step is an exact integer division, so MCLK stays locked to LRCK
// (the PCM1808 needs that).
static void set_clocks(void)
{
    uint32_t fd_divider = XMC_SCU_CLOCK_GetPeripheralClockFrequency() / (2U * I2S_MCLK_HZ);

    I2S_CHANNEL->FDR = XMC_USIC_CH_BRG_CLOCK_DIVIDER_MODE_NORMAL |
                       ((1024U - fd_divider) << USIC_CH_FDR_STEP_Pos);
    I2S_CHANNEL->BRG = (I2S_CHANNEL->BRG & ~(USIC_CH_BRG_PDIV_Msk | USIC_CH_BRG_CTQSEL_Msk)) |
                       (1UL << USIC_CH_BRG_PDIV_Pos) |
                       (2UL << USIC_CH_BRG_CTQSEL_Pos) |   // time quanta count BCK periods
                       USIC_CH_BRG_PPPEN_Msk;
    XMC_I2S_CH_SetSystemWordLength(I2S_CHANNEL, I2S_SLOT_BITS);  // WA toggles every 32 BCK
}

static void init_fifos(void)
{
    XMC_USIC_CH_TXFIFO_Configure(I2S_CHANNEL, TX_FIFO_START, FIFO_WORDS, 1U);
    XMC_USIC_CH_RXFIFO_Configure(I2S_CHANNEL, RX_FIFO_START, FIFO_WORDS,
                                 WORDS_PER_FRAME - 1U);                    // event at 4 words
    I2S_CHANNEL->RBCTR |= RX_INFO_MODE_SOF;

    XMC_USIC_CH_RXFIFO_SetInterruptNodePointer(I2S_CHANNEL,
        XMC_USIC_CH_RXFIFO_INTERRUPT_NODE_POINTER_STANDARD, 0U);
    XMC_USIC_CH_RXFIFO_EnableEvent(I2S_CHANNEL, XMC_USIC_CH_RXFIFO_EVENT_CONF_STANDARD);

    for (uint32_t frame = 0; frame < TX_PRIME_FRAMES; frame++)
    {
        i2s_write_frame(0, 0);
    }
}

void init_i2s(void)
{
    const XMC_I2S_CH_CONFIG_t config = {
        .data_bits = I2S_WORD_BITS,
        .frame_length = I2S_SLOT_BITS,           // the whole 32-bit slot: 2 words
        .data_delayed_sclk_periods = 1U,         // I2S: data starts 1 BCK after LRCK changes
        .wa_inversion = XMC_I2S_CH_WA_POLARITY_DIRECT,
        .bus_mode = XMC_I2S_CH_BUS_MODE_MASTER,
    };

    XMC_I2S_CH_InitEx(I2S_CHANNEL, &config, false);  // also ungates USIC2; clocks set below
    set_clocks();
    XMC_I2S_CH_ConfigureShiftClockOutput(I2S_CHANNEL, XMC_I2S_CH_BRG_SHIFT_CLOCK_OUTPUT_SCLK);
    XMC_I2S_CH_SetInputSource(I2S_CHANNEL, XMC_I2S_CH_INPUT_DIN0, I2S_DATA_IN_SOURCE);
    XMC_I2S_CH_EnableMasterClock(I2S_CHANNEL);
    init_fifos();
    init_pins();

    NVIC_SetPriority(I2S_IRQn, 0);       // the audio interrupt: highest priority
    NVIC_EnableIRQ(I2S_IRQn);
    XMC_I2S_CH_Start(I2S_CHANNEL);       // clocks start; frames begin flowing
}

// Reads received words until it has a whole frame. Only high words (SOF set)
// are kept; low words are skipped. Words arrive in LRCK order, left then
// right. Both converters run off the same LRCK, so a frame read here lines up
// with the frame written below. Returns false when the FIFO runs out first;
// the half-read frame is finished on the next call.
bool i2s_read_frame(int16_t *left, int16_t *right)
{
    static int16_t high_words[2];
    static uint32_t high_word_count;
    static uint32_t raw_index;

    while (XMC_USIC_CH_RXFIFO_GetLevel(I2S_CHANNEL) > 0U)
    {
        uint32_t word = I2S_CHANNEL->OUTR;              // reading OUTR pops the FIFO
        i2s_rx_raw[raw_index] = word;
        raw_index = (raw_index + 1U) % WORDS_PER_FRAME;
        if ((word & OUTR_SOF) == 0U) { continue; }      // a low word

        high_words[high_word_count++] = (int16_t)(word & 0xFFFFU);
        if (high_word_count == 2U)
        {
            high_word_count = 0;
            *left  = high_words[0];
            *right = high_words[1];
            return true;
        }
    }
    return false;
}

// Each transmit word is tagged with the channel it belongs to: writing to
// IN[0] means left (LRCK low), IN[16] means right (LRCK high). The USIC holds
// a word until its channel's half of the frame comes around. The low word of
// each slot is 0.
void i2s_write_frame(int16_t left, int16_t right)
{
    XMC_I2S_CH_Transmit(I2S_CHANNEL, (uint16_t)left,  XMC_I2S_CH_CHANNEL_1_LEFT);
    XMC_I2S_CH_Transmit(I2S_CHANNEL, 0U,              XMC_I2S_CH_CHANNEL_1_LEFT);
    XMC_I2S_CH_Transmit(I2S_CHANNEL, (uint16_t)right, XMC_I2S_CH_CHANNEL_2_RIGHT);
    XMC_I2S_CH_Transmit(I2S_CHANNEL, 0U,              XMC_I2S_CH_CHANNEL_2_RIGHT);
}
