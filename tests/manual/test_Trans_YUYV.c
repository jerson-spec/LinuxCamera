#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "config.h"
#include "yuyv.h"
#include "lcd.h"

//#define NULL ((void*)0)

/*
* ./test_Trans_YUYV [path/trans.ini]
*	-获得YUYV区域
*	-YUYV区域转RGB
*	-绘制RGB（RGB888）
*/
int main(int argc,char**argv)
{
	int w;
	int h;
	int i,j;
	int ret;
	size_t index;
	size_t size;
	int time;
	unsigned char r,g,b;
	struct fb_fix_screeninfo lcd_fix_screeninfo;
	struct fb_var_screeninfo lcd_var_screeninfo;
	unsigned char* yuyvbuf;
	unsigned char* rgbbuf;
	unsigned int color;
	app_config_t trans_cfg;
	app_config_t lcd_cfg;
	
	/* 用法、加载trans配置项 */
	if(argc > 3)
	{
		fprintf(stderr,"Usage: %s [path/trans.ini] [path/lcd.ini]\n",argv[0]);
		return -1;
		
	}
	char *trans_path = (argc > 1)? argv[1] : "config/trans.ini";
	
	if(config_load(trans_path, &trans_cfg) < 0)
	{
		fprintf(stderr,"load config ini: %s error\n",trans_path);
		return -1;
	}

	config_print(&trans_cfg);
	Is_full_range(&trans_cfg);

	/* lcd初始化 */
	char *lcd_path = (argc > 2)? argv[2] : "config/lcd.ini";
	ret = lcd_init(lcd_path , &lcd_cfg);
	if(ret < 0)
	{
		fprintf(stderr,"%s,lcd init error\n",__FUNCTION__);
		return -1;
	}
	
	/* 获得lcd信息 */
	memset(&lcd_fix_screeninfo , 0 , sizeof(struct fb_fix_screeninfo));
	memset(&lcd_var_screeninfo , 0 , sizeof(struct fb_var_screeninfo));

	Get_lcd_parameters(&lcd_fix_screeninfo,&lcd_var_screeninfo);
		
	w = lcd_var_screeninfo.xres;
	h = lcd_var_screeninfo.yres;

	 if (w <= 0 || h <= 0 || w & 1) {
        fprintf(stderr, "invalid lcd size: %d x %d\n", w, h);
        return -1;
    }
	
	/* 获得一片YUYV区域 */
	yuyvbuf = generate_yuyv_region(w , h);
	if(yuyvbuf == NULL)
	{
		fprintf(stderr,"%s, generate yuyv region error\n",__FUNCTION__);
		return -1;
	}
	
	/* 把YUYV格式条状区域转rgb */
	size = (size_t)w * (size_t)h * 3;
	rgbbuf = malloc(size);
	if(rgbbuf == NULL)
	{
		fprintf(stderr, "%s,malloc error\n",__FUNCTION__);
		free(yuyvbuf);
		return -1;
	}
	memset(rgbbuf , 0 , size);
	
	if(yuyv_to_rgb(yuyvbuf , rgbbuf , w , h) == -1)
	{
		fprintf(stderr,"%s, yuyv to rgb error\n",__FUNCTION__);
		free(yuyvbuf);
		free(rgbbuf);
		return -1;
	}
	
	/* lcd显示rgb */		
	time = trans_cfg.trans_display_time;
	while(time--)
	{
		for(i = 0 ; i < h ; i++ )
		{
			for(j = 0 ; j < w ; j++)
			{
				index = (size_t)i * (size_t)w + (size_t)j;

				r = rgbbuf[3 * index + 0];
				g = rgbbuf[3 * index + 1];
				b = rgbbuf[3 * index + 2];
				color = ((unsigned int)r << 16) | ((unsigned int)g << 8 ) | ((unsigned int)b) ;
				 
				ret = lcd_put_pixel((unsigned int)j , (unsigned int)i , color);
				if(ret < 0)
				{
					fprintf(stderr, "lcd put pixel error, line:%d,column:%d\n",j,i);
					free(yuyvbuf);
					free(rgbbuf);
					return -1;
				}
			}
		}	
		sleep(1);
	}
	
	/* 收尾 */	
	free(yuyvbuf);
	free(rgbbuf);

	if(lcd_deinit() < 0 )
	{
		fprintf(stderr,"%s,lcd deinit error\n",__FUNCTION__);
		return -1;
	}
	
	return 0;
}
