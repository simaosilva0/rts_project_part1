#include <stdint.h>

#include "mcc_generated_files/adcc.h"
#include "adc.h"

uint8_t adc_read_level(void)
{
    adc_result_t sample = ADCC_GetSingleConversion(POT);

    return (uint8_t)(sample >> 7);
}