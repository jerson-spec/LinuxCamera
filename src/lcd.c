#include <stdio.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <unistd.h>

#include "log.h"
#include "lcd.h"

struct fb_fix_screeninfo lcd_fix_screeninfo_t;
struct fb_var_screeninfo lcd_var_screeninfo_t;
unsigned char* mem_addr;
unsigned int fb_size;
unsigned int lcd_size;

static int fd;
static unsigned int draw_offset;

static int Is_lcd_doublebuffering = 0;

int lcd_doublebuffering_on(void)
{
	return Is_lcd_doublebuffering;
}

/*
*	双缓冲绘制翻转
*	**ioctl FBIOPAN_DISPLAY设置可见区
*	**visible_offset记录成功设置的上一次可见区（偏移）
*/
int lcd_flip(void)
{
	static unsigned int visible_offset = 0;
	unsigned int display_target = (visible_offset == 0)? lcd_size : 0;
	unsigned int display_target_y = display_target / lcd_fix_screeninfo_t.line_length;
	
	/* ioctl FBIOPAN_DISPLAY */
	lcd_var_screeninfo_t.xoffset = 0;
	lcd_var_screeninfo_t.yoffset = display_target_y;
	
	if(ioctl(fd , FBIOPAN_DISPLAY ,&lcd_var_screeninfo_t) < 0)
	{
		LOG_ERROR("ioctl: FBIOPAN_DISPLAY, xoffset: %u, yoffset: %u",
			lcd_var_screeninfo_t.xoffset,lcd_var_screeninfo_t.yoffset);
		
		Is_lcd_doublebuffering = 0;
		draw_offset = visible_offset;
		
		return -1;
	}
	
	visible_offset = display_target;
	draw_offset = (display_target == 0) ? lcd_size : 0;
	
	return 0;
}

/*
*	逐点绘制像素，兼容32位、16位格式
*	参数含义：要绘制像素的x值（列号）、y值（行号）、像素颜色（RGB）
*/
int lcd_put_pixel(unsigned int x, unsigned int y , unsigned int color)
{
	unsigned char *pos;
	unsigned char red,green,blue;
	red = green = blue =0;
	
	if(x >= lcd_var_screeninfo_t.xres || y >= lcd_var_screeninfo_t.yres)
	{
		fprintf(stderr,"%s", "Exceeds resolution\n");
		return -1;
	}
	pos = mem_addr + draw_offset + y * lcd_fix_screeninfo_t.line_length + x * 
		lcd_var_screeninfo_t.bits_per_pixel / 8;

	if(lcd_var_screeninfo_t.bits_per_pixel == 16)
	{
		unsigned short pixel;
		red   = (color >> 16) & 0xff;
		green = (color >> 8)  & 0xff;
		blue  = (color >> 0 ) & 0xff;
		
		pixel = ((red >> 3) << 11) | ((green >> 2) << 5) | ((blue >> 3)) ;
		memcpy(pos, &pixel,sizeof(pixel));
	}
	else if(lcd_var_screeninfo_t.bits_per_pixel == 32)
	{
		unsigned int pixel;
		pixel = color;
		memcpy(pos,&pixel,sizeof(pixel));
	}
	else
	{
		fprintf(stderr, "pixel bits error, num : %u\n",lcd_var_screeninfo_t.bits_per_pixel);
		return -1;
	}
	
	return 0;
}

/*
*	按行绘制，仅支持RGB888格式
*	参数含义：要绘制的行号、x方向偏移（传入算好的定值）、rgbbuf（数据源头）、（相机）行像素数
*/
int lcd_write_line_rgb888(int line,int x_offset,unsigned char* rgb,int width)
{
	unsigned char *pos;
	unsigned char* rgbbuffer = rgb;

	if(line < 0 || x_offset < 0)
	{
		fprintf(stderr,"line: %d or x_offset: %d error\n",line,x_offset);
		return -1;
	}
	if((unsigned int)(x_offset + width) > lcd_var_screeninfo_t.xres || (unsigned int)line >= lcd_var_screeninfo_t.yres)
	{
		fprintf(stderr,"%s", "Exceeds resolution\n");
		return -1;
	}
	if(lcd_var_screeninfo_t.bits_per_pixel != 32)
	{
		fprintf(stderr,"bits per pixel: %u != 32\n",lcd_var_screeninfo_t.bits_per_pixel);
		return -1;
	}
	
	pos = mem_addr + draw_offset + line * lcd_fix_screeninfo_t.line_length + x_offset * 4;
	
	int x = 0;
	unsigned int* dst = (unsigned int*) pos;
	
	while(x < width)
	{
		dst[x++] = rgbbuffer[0] << 16 | rgbbuffer[1] << 8 | rgbbuffer[2];
		rgbbuffer += 3;
	}
				
	return 0;
}

/*
*	lcd初始化
*	解析lcd.ini配置项、初始化lcd.log日志、...
*/
int lcd_init(char* lcd_cfg_path , app_config_t *lcd_cfg_p)
{
	/* 解析配置项并打印 */
	app_config_t cfg;
	log_level_t log_level;
	char *path =  lcd_cfg_path;
	
	if(config_load(path, &cfg) < 0 )
	{
		fprintf(stderr,"can't load the config: %s\n",path);
		return -1;
	}
	*lcd_cfg_p = cfg;
	
	config_print(&cfg);

	/* 初始化日志 */
	if(log_level_parse(cfg.lcd_log_level, &log_level) < 0 )
	{
		fprintf(stderr,"parse log: %s error\n",cfg.lcd_log_level);
		return -1;
	}
	
	if(log_init(log_level, cfg.lcd_log_file) < 0 )
	{
		fprintf(stderr,"init log: %s error\n",cfg.lcd_log_file);
	}

	/* 打开设备 */	
	fd = open(cfg.lcd_device , O_RDWR);
	if(fd < 0)
	{
		//LOG_ERROR("open error\n");
		perror("open");
		return -1;
	}

	/* 获取lCD信息 */
	/* var */
	if(ioctl(fd , FBIOGET_VSCREENINFO ,&lcd_var_screeninfo_t) < 0)
	{
		//perror("ioctl lcd var");
		fprintf(stderr,"line: %d, ioctl lcd var error",__LINE__);
		return -1;
	}
	
	/** 设置可变参数 **/
	lcd_var_screeninfo_t.xres_virtual = lcd_var_screeninfo_t.xres;
	lcd_var_screeninfo_t.yres_virtual = 2 * lcd_var_screeninfo_t.yres;
	
	if(ioctl(fd , FBIOPUT_VSCREENINFO ,&lcd_var_screeninfo_t) < 0)
	{
		perror("ioctl put lcd var");
		return -1;
	}

	/* 更新lCD信息 */
	/* var */
	if(ioctl(fd , FBIOGET_VSCREENINFO ,&lcd_var_screeninfo_t) < 0)
	{
		//LOG_ERROR("ioctl lcd var error\n");
		fprintf(stderr,"line: %d, ioctl lcd var error",__LINE__);
		return -1;
	}
	/** 检查可变参数设置是否成功 **/
	if(lcd_var_screeninfo_t.yres_virtual != 2 * lcd_var_screeninfo_t.yres)
	{ 
		fprintf(stderr,"yres_virtual: %u != 2 * yres: %u",lcd_var_screeninfo_t.yres_virtual
			, 2 * lcd_var_screeninfo_t.yres);
		
		return -1;
	}
	
	/* fix */
	if(ioctl(fd , FBIOGET_FSCREENINFO ,&lcd_fix_screeninfo_t) < 0)
	{	
		//LOG_ERROR("ioctl lcd fix error\n");
		perror("ioctl lcd fix");
		return -1;
	}

	lcd_size = lcd_var_screeninfo_t.yres * lcd_fix_screeninfo_t.line_length;

	/* mmap映射 */
	fb_size = lcd_fix_screeninfo_t.smem_len;
	
	/** 检查smem_len大小 **/
	if(fb_size >= 2 * lcd_size)
	{
		LOG_INFO("fb_size: %u, lcd_size: %u",fb_size,lcd_size);
		mem_addr = mmap(NULL, fb_size, PROT_READ | PROT_WRITE, MAP_SHARED , fd, 0);
		if(mem_addr == MAP_FAILED )
		{		
			//LOG_ERROR("mmap error\n");
			perror("mmap");
			return -1;
		}
		memset(mem_addr , 0 , 2 * lcd_size);
	}
	else
	{
		LOG_ERROR("fb_size: %u, lcd_size: %u",fb_size,lcd_size);
		return -1;
	}
	Is_lcd_doublebuffering = 1;
	draw_offset = lcd_size;
	
	return 0;
}

int lcd_deinit(void)
{
	/* 收尾 */
	close(fd);
	log_close();
	
	memset(mem_addr , 0 , 2 * lcd_size);
	if(munmap(mem_addr , fb_size) < 0 )
	{
		perror("munmap");
		return -1;
	}
	return 0;
}

/* 获取lcd_init里获得的lcd参数：
*	struct fb_fix_screeninfo lcd_fix_screeninfo_t;
*  	struct fb_var_screeninfo lcd_var_screeninfo_t;
*  返回给调用者；
*/
void Get_lcd_parameters(struct fb_fix_screeninfo *lcd_fix_screeninfo_p,struct fb_var_screeninfo *lcd_var_screeninfo_p)
{
	if (lcd_fix_screeninfo_p != NULL) {
        *lcd_fix_screeninfo_p = lcd_fix_screeninfo_t;   
    }
    if (lcd_var_screeninfo_p != NULL) {
        *lcd_var_screeninfo_p = lcd_var_screeninfo_t;
    }
}

