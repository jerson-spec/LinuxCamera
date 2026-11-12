#include "font.h"
#include "lcd.h"

/* 
*	绘制字符串字体
*	- x、y超限依赖lcd_put_pixel内部判断；
*	- 遇换行符换行绘制；超出分辨率的绘制返回错误
*/
int osd_show_text(unsigned int x, unsigned int y ,
		const unsigned char* str, unsigned int color)
{
	const unsigned char* s = str;
	int i,j;
	int cnt = 0;
	while(*s){
		const unsigned char* dots = &fontdata_8x16[*s * 16];
		if(*s == '\n'){
			y += 16;
			cnt = 0;
			s++;
			continue;
		}
		for(i = 0; i < 16 ; i++){
			unsigned char byte = dots[i];
			for(j = 7; j >= 0; j-- ){
				if((1 << j) & byte){
					if(lcd_put_pixel(x + 8 * cnt + 7 - j, y + i, color) < 0)
						return -1;
				}
			}
		}
		s++;
		cnt++;
	}
	return 0;
}


/*
*	计算字符串显示宽度（像素）
*	- 8x16字体，每个字符宽8像素；
*	- 遇换行符重新计数，返回最长一行的宽度。
*/
int osd_text_width(const unsigned char* str)
{
	const unsigned char* s = str;
	int cnt = 0;
	int max = 0;
	
	while(*s){
		if(*s == '\n'){
			if(cnt > max)
				max = cnt;
			cnt = 0;
		}
		else
			cnt++;
		s++;
	}
	if(cnt > max)
		max = cnt;
	
	return max * 8;
}


/*
*	右对齐绘制字符串
*	- right_x是文本右边界（不含），一般传lcd的xres - 8；
*	- 文本比right_x还宽时，退化成从x = 0开始画。
*/
int osd_show_text_right(unsigned int right_x, unsigned int y ,
		const unsigned char* str, unsigned int color)
{
	int width = osd_text_width(str);
	int x;
	
	if(width <= 0)
		return 0;
	
	if(right_x < (unsigned int)width)
		x = 0;
	else
		x = (int)right_x - width;
	
	return osd_show_text((unsigned int)x, y, str, color);
}


/*
*	带1像素阴影绘制
*	- 先在(x+1, y+1)用阴影色画一遍，再用正文色画一遍；
*	- 阴影可能被屏幕边缘裁掉，这里忽略它的返回值。
*/
int osd_show_text_shadow(unsigned int x, unsigned int y ,
		const unsigned char* str, unsigned int color, unsigned int shadow_color)
{
	osd_show_text(x + 1, y + 1, str, shadow_color);
	return osd_show_text(x, y, str, color);
}


/*
*	填充矩形
*	- 给OSD垫一块底色，避免文字和画面混在一起；
*	- 坐标必须在屏幕内，超出时返回-1。
*/
int osd_show_rect(unsigned int x, unsigned int y ,
		unsigned int w, unsigned int h, unsigned int color)
{
	unsigned int i,j;
	
	for(i = 0; i < h; i++){
		for(j = 0; j < w; j++){
			if(lcd_put_pixel(x + j, y + i, color) < 0)
				return -1;
		}
	}
	
	return 0;
}


