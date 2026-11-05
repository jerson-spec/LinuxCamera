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
static int fd;


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
	pos = mem_addr + y * lcd_fix_screeninfo_t.line_length + x * 
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
	/* fix */
	if(ioctl(fd , FBIOGET_FSCREENINFO ,&lcd_fix_screeninfo_t) < 0)
	{	
		//LOG_ERROR("ioctl lcd fix error\n");
		perror("ioctl lcd fix");
		return -1;
	}
	/* var */
	if(ioctl(fd , FBIOGET_VSCREENINFO ,&lcd_var_screeninfo_t) < 0)
	{
		//LOG_ERROR("ioctl lcd var error\n");
		perror("ioctl lcd var");
		return -1;
	}

	/* mmap映射 */
	fb_size = lcd_fix_screeninfo_t.smem_len;
	mem_addr = mmap(NULL, fb_size, PROT_READ | PROT_WRITE, MAP_SHARED , fd, 0);
	if(mem_addr == MAP_FAILED )
	{		
		//LOG_ERROR("mmap error\n");
		perror("mmap");
		return -1;
	}
	memset(mem_addr , 0 ,fb_size);

	return 0;
}

int lcd_deinit(void)
{
	/* 收尾 */
	close(fd);
	log_close();
	
	memset(mem_addr , 0 ,fb_size);
	if(munmap(mem_addr , fb_size) < 0 )
	{
		perror("munmap");
		return -1;
	}
	return 0;
}


void Get_lcd_parameters(struct fb_fix_screeninfo *lcd_fix_screeninfo_p,struct fb_var_screeninfo *lcd_var_screeninfo_p)
{
	if (lcd_fix_screeninfo_p != NULL) {
        *lcd_fix_screeninfo_p = lcd_fix_screeninfo_t;   
    }
    if (lcd_var_screeninfo_p != NULL) {
        *lcd_var_screeninfo_p = lcd_var_screeninfo_t;
    }
}

