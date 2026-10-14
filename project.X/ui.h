/* 
 * File:   ui.h
 * Author: 
 *
 * Created on October 14, 2026, 11:21 PM
 */

#ifndef UI_H
#define UI_H

void ui_init(void);   /* lcd_init + initial state; call after interrupts are enabled */
void ui_task(void);   /* call every main-loop pass, after the ticks loop */

#endif	/* UI_H */

