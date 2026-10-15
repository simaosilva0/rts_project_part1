#ifndef MONITOR_H
#define MONITOR_H

#include <stdint.h>

void monitor_init(void);
void monitor_tick(void);
void monitor_service(void);
void monitor_clear_alarm_latches(void);
uint8_t monitor_sample_valid(void);

#endif