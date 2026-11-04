#ifndef _YUYV_H_
#define _YUYV_H_

/*
*	rgb结构体
*/
struct rgb {
	unsigned char r;
	unsigned char g;
	unsigned char b;
};

void Is_full_range(app_config_t *trans_cfg_p);

int yuyv_to_rgb(unsigned char* yuyvdata,unsigned char* rgbdata,int width,int height);
int rgb_to_yuyv(unsigned char* yuyvdata,unsigned char* rgbdata,int width,int height);
int rgb_to_yuyv_pair(unsigned char*yuyvdata,struct rgb rgb_0,struct rgb rgb_1,int full);

unsigned char* generate_yuyv_region(int width,int height);


#endif
