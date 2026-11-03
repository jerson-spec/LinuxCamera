#ifndef _TEST_DRAW_REGION_H
#define _TEST_DRAW_REGION_H

#include <linux/fb.h>

int lcd_put_pixel(unsigned int x, unsigned int y , unsigned int color);
int Get_lcd_parameters(struct fb_fix_screeninfo *lcd_fix_screeninfo_p,struct fb_var_screeninfo *lcd_var_screeninfo_p);

#endif




