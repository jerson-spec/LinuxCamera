#ifndef _LCD_H_
#define _LCD_H_

#include <linux/fb.h>

int lcd_init(char* lcd_cfg_path , app_config_t *lcd_cfg_p);
int lcd_deinit(void);

int lcd_put_pixel(unsigned int x, unsigned int y , unsigned int color);
void Get_lcd_parameters(struct fb_fix_screeninfo *lcd_fix_screeninfo_p,struct fb_var_screeninfo *lcd_var_screeninfo_p);

#endif





