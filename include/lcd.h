#ifndef _LCD_H_
#define _LCD_H_

#include <linux/fb.h>
#include "config.h"

typedef struct{
	int fd;
	struct fb_fix_screeninfo lcd_fix_screeninfo_t;
	struct fb_var_screeninfo lcd_var_screeninfo_t;
	unsigned int lcd_size;
	unsigned int draw_offset;
	int Is_lcd_doublebuffering;
}lcd_t;

typedef struct{
	int x_offset;
	int y_offset;
}letterbox_t;

int lcd_init(char* lcd_cfg_path , app_config_t *lcd_cfg_p);
int lcd_clear(void);
int lcd_deinit(void);

int lcd_doublebuffering_on(void);
int lcd_flip(void);
int lcd_put_pixel(unsigned int x, unsigned int y , unsigned int color);
int lcd_write_line_rgb888(int line,int x_offset,unsigned char* rgb,int width);

void Get_lcd_parameters(struct fb_fix_screeninfo *lcd_fix_screeninfo_p,struct fb_var_screeninfo *lcd_var_screeninfo_p);

#endif





