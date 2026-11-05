#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "log.h"
#include "lcd.h"

struct fb_fix_screeninfo lcd_fix_screeninfo;
struct fb_var_screeninfo lcd_var_screeninfo;


/*
*  ./test_Draw_region
*/

int main(int argc,char**argv)
{
	int ret;
	char *path;
	app_config_t cfg;

	if(argc > 2)
	{
		fprintf(stderr,"Usage: %s [path/lcd.ini]\n",argv[0]);
		return -1;
	}	
	path = (argc > 1)? argv[1] : "config/lcd.ini";

	/* lcd初始化 */
	ret = lcd_init(path , &cfg);
	if(ret < 0 )
	{
		fprintf(stderr,"%s,lcd init error\n",__FUNCTION__);
		return -1;
	}
	config_print(&cfg);

	memset(&lcd_fix_screeninfo , 0 , sizeof(struct fb_fix_screeninfo));
	memset(&lcd_var_screeninfo , 0 , sizeof(struct fb_var_screeninfo));
	Get_lcd_parameters(&lcd_fix_screeninfo,&lcd_var_screeninfo);
	
	/* 获取lCD信息 */
	/* fix */
	LOG_INFO("fix:\n");
	LOG_INFO("Length of frame buffer mem : %u\n",lcd_fix_screeninfo.smem_len);
	LOG_INFO("length of a line in bytes  : %u\n",lcd_fix_screeninfo.line_length);
	
	
	/* var */	
	LOG_INFO("var:\n");
	LOG_INFO("visible xres : %u\n",lcd_var_screeninfo.xres);
	LOG_INFO("visible yres : %u\n",lcd_var_screeninfo.yres);
	
	LOG_INFO("bits_per_pixel : %u\n",lcd_var_screeninfo.bits_per_pixel);
	
	printf("virtual xres : %u\n",lcd_var_screeninfo.xres_virtual);
	printf("virtual yres : %u\n",lcd_var_screeninfo.yres_virtual);	

	printf("transp offset : %u\n",lcd_var_screeninfo.transp.offset);
	printf("transp length : %u\n",lcd_var_screeninfo.transp.length);
	
	printf("red    offset : %u\n",lcd_var_screeninfo.red.offset);
	printf("red    length : %u\n",lcd_var_screeninfo.red.length);
//	printf("red    msb_right : %u\n",lcd_var_screeninfo.red.msb_right);
	
	printf("green  offset : %u\n",lcd_var_screeninfo.green.offset);
	printf("green  length : %u\n",lcd_var_screeninfo.green.length);

	printf("blue   offset : %u\n",lcd_var_screeninfo.blue.offset);
	printf("blue   length : %u\n",lcd_var_screeninfo.blue.length);

	/* 绘制一片区域 */
	#if 0
	unsigned int color = 0xffff0000;
	unsigned int i = 0;
	for(; i < *lcd_fix_screeninfo_p .smem_len / 4;i++)
	{
		*(unsigned int*)(mem_addr + i*4) = color;
	}
	
	#else
	unsigned int i , j;
	int time = cfg.lcd_display_time;
	
	while(time)
	{
		for(i = 0 ; i < lcd_var_screeninfo.yres / cfg.lcd_display_size; i++ )
		{
			for(j = 0 ; j < lcd_var_screeninfo.xres /cfg.lcd_display_size ; j++)
			{
				ret = lcd_put_pixel(j , i , cfg.lcd_color);
				if(ret < 0)
				{
					fprintf(stderr, "lcd put pixel error, line:%d,column:%d\n",j,i);
					return -1;
				}
			}
		}
		time--;
		sleep(1);
	}
	
	#endif

	/* 收尾 */		
	ret = lcd_deinit();
	if(ret < 0 )
	{
		fprintf(stderr,"%s,lcd deinit error\n",__FUNCTION__);
		return -1;
	}
	
	return 0;
}
