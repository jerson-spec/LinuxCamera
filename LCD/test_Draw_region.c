#include <stdio.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <linux/fb.h>
#include <sys/ioctl.h>
#include "log.h"
#include "config.h"
#include <sys/mman.h>
#include <string.h>
#include <unistd.h>

struct fb_fix_screeninfo lcd_fix_screeninfo_t;
struct fb_var_screeninfo lcd_var_screeninfo_t;
unsigned char* mem_addr;

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

/*
*  ./test_Draw_region
*/

int main(int argc,char**argv)
{
	int fd;
	unsigned int fb_size;
	int ret;
	log_level_t log_level;

	if(argc > 2)
	{
		fprintf(stderr,"Usage: %s [path/lcd.ini]\n",argv[0]);
		return -1;
	}	

	/* 解析配置项并打印 */
	app_config_t cfg;
	char *path = (argc > 1)? argv[1] : "config/lcd.ini";
	
	if(config_load(path, &cfg) < 0 )
	{
		fprintf(stderr,"can't load the config: %s\n",path);
		return -1;
	}

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
	/* fix */
	if(ioctl(fd , FBIOGET_FSCREENINFO ,&lcd_fix_screeninfo_t) < 0)
	{	
		//LOG_ERROR("ioctl lcd fix error\n");
		perror("ioctl lcd fix");
		return -1;
	}
	LOG_INFO("fix:\n");
	LOG_INFO("Length of frame buffer mem : %u\n",lcd_fix_screeninfo_t.smem_len);
	LOG_INFO("length of a line in bytes  : %u\n",lcd_fix_screeninfo_t.line_length);
	
	fb_size = lcd_fix_screeninfo_t.smem_len;
	
	/* var */
	if(ioctl(fd , FBIOGET_VSCREENINFO ,&lcd_var_screeninfo_t) < 0)
	{
		//LOG_ERROR("ioctl lcd var error\n");
		perror("ioctl lcd var");
		return -1;
	}
	
	LOG_INFO("var:\n");
	LOG_INFO("visible xres : %u\n",lcd_var_screeninfo_t.xres);
	LOG_INFO("visible yres : %u\n",lcd_var_screeninfo_t.yres);
	
	LOG_INFO("bits_per_pixel : %u\n",lcd_var_screeninfo_t.bits_per_pixel);
	
	printf("virtual xres : %u\n",lcd_var_screeninfo_t.xres_virtual);
	printf("virtual yres : %u\n",lcd_var_screeninfo_t.yres_virtual);	

	printf("transp offset : %u\n",lcd_var_screeninfo_t.transp.offset);
	printf("transp length : %u\n",lcd_var_screeninfo_t.transp.length);
	
	printf("red    offset : %u\n",lcd_var_screeninfo_t.red.offset);
	printf("red    length : %u\n",lcd_var_screeninfo_t.red.length);
//	printf("red    msb_right : %u\n",lcd_var_screeninfo_t.red.msb_right);
	
	printf("green  offset : %u\n",lcd_var_screeninfo_t.green.offset);
	printf("green  length : %u\n",lcd_var_screeninfo_t.green.length);

	printf("blue   offset : %u\n",lcd_var_screeninfo_t.blue.offset);
	printf("blue   length : %u\n",lcd_var_screeninfo_t.blue.length);

	
	/* mmap映射 */
	mem_addr = mmap(NULL, fb_size, PROT_READ | PROT_WRITE, MAP_SHARED , fd, 0);
	if(mem_addr == MAP_FAILED )
	{		
		//LOG_ERROR("mmap error\n");
		perror("mmap");
		return -1;
	}
	memset(mem_addr , 0 ,fb_size);

	/* 绘制一片区域 */
	#if 0
	unsigned int color = 0xffff0000;
	unsigned int i = 0;
	for(; i < lcd_fix_screeninfo_t.smem_len / 4;i++)
	{
		*(unsigned int*)(mem_addr + i*4) = color;
	}
	
	#else
	unsigned int i , j;
	int time = cfg.lcd_display_time;
	
	while(time)
	{
		for(i = 0 ; i < lcd_var_screeninfo_t.yres / cfg.lcd_display_size; i++ )
		{
			for(j = 0 ; j < lcd_var_screeninfo_t.xres /cfg.lcd_display_size ; j++)
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
	memset(mem_addr , 0 ,fb_size);
	if(munmap(mem_addr , fb_size) < 0 )
	{
		perror("munmap");
		return -1;
	}
	log_close();
	close(fd);
	
	return 0;
}
