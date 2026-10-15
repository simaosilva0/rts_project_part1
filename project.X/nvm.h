#ifndef NVM_H
#define NVM_H

#include <stdint.h>

void nvm_load(uint8_t *clock_h, uint8_t *clock_m);
void nvm_save_config(uint8_t clock_h, uint8_t clock_m);
void nvm_save_records(void);
void nvm_reset_records(void);

#endif