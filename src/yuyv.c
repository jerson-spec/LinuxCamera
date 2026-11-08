#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "yuyv.h"

static int full = -1;


/*
*	辅助函数：对unsigned char,越界数处理
*/
static void clamp(int *val)
{
	*val = (*val < 0)? 0 : ((*val > 255)? 255 : *val);
}

/*
*	辅助函数：全范围、有限范围判断
*/
void Is_full_range(app_config_t *trans_cfg_p)
{
	app_config_t cfg;
	cfg = *trans_cfg_p;
	
	if(strcmp(cfg.trans_range,"FULL") == 0)			//全范围
	{
		full = 1;
	}
	
	else if(strcmp(cfg.trans_range,"LIMIT") == 0)	//有限范围
	{
		full = 0;
	}
	else
	{
		fprintf(stderr,"%s, trans_range: %s error\n",__FUNCTION__,cfg.trans_range);
		full = -1;
	}
}


/*
*	BT.601、无stride、w必须为偶数
*/
int yuyv_to_rgb(unsigned char* yuyvdata,unsigned char* rgbdata,int width,int height)
{
	int i;
	unsigned char data[4];
	unsigned char Y0,U0,Y1,V1;
	int r0,g0,b0,r1,g1,b1;

	/* 条件判断 */
	if(full == -1)
	{
		return -1;
	}

	if(width <= 0 || width & 1 || height <= 0)
	{
		fprintf(stderr, "%s,Parameter error\n",__FUNCTION__);
		return -1;
	}
	
	/* 分情况处理 */
	for(i = 0 ; i < width * height/2 ; i++)
	{
 		memcpy(data , &yuyvdata[i * 4] , 4);	
		// Y0 U0 Y1 V1 -> Y0 U0 V1 | Y1 U0 V1 -> R G B
		Y0 = data[0];
		U0 = data[1];
		Y1 = data[2];
		V1 = data[3];

		if(full) 		
		{
			/* 第一个RGB像素 */
			r0 = Y0 + ((360 * (V1 - 128) + 128) >> 8);
			clamp(&r0);

			g0 = Y0 - (( 88 * (U0 - 128) + 184 * (V1 - 128) + 128) >> 8);
			clamp(&g0);

			b0 = Y0 + ((455 * (U0 - 128) + 128) >> 8);
			clamp(&b0);
				
			/* 第二个RGB像素 */
			r1 = Y1 + ((360 * (V1 - 128) + 128) >> 8);
			clamp(&r1);

			g1 = Y1 - (( 88 * (U0 - 128) + 184 * (V1 - 128) + 128) >> 8);
			clamp(&g1);

			b1 = Y1 + ((455 * (U0 - 128) + 128) >> 8);
			clamp(&b1);
						
		}	
		else 			
		{						
			/* 第一个RGB像素 */
			int c0 = 298 * (Y0 - 16);
			
			r0 = (c0 + 409 * (V1 - 128) + 128) >> 8;
			clamp(&r0);
			
			g0 = (c0 - 100 * (U0 - 128) - 208 * (V1 - 128) + 128) >> 8;
			clamp(&g0);
			
			b0 = (c0 + 516 * (U0 - 128) + 128) >> 8;
			clamp(&b0);

			/* 第二个RGB像素 */
			int c1 = 298 * (Y1 - 16);
			
			r1 = (c1 + 409 * (V1 - 128) + 128) >> 8;
			clamp(&r1);
			
			g1 = (c1 - 100 * (U0 - 128) - 208 * (V1 - 128) + 128) >> 8;
			clamp(&g1);
			
			b1 = (c1 + 516 * (U0 - 128) + 128) >> 8;
			clamp(&b1);
			
		}

		/* 转换 */
		rgbdata[i * 6 + 0] =  r0;
		rgbdata[i * 6 + 1] =  g0;
		rgbdata[i * 6 + 2] =  b0;
		rgbdata[i * 6 + 3] =  r1;
		rgbdata[i * 6 + 4] =  g1;
		rgbdata[i * 6 + 5] =  b1;
		
	}
	return 0;
}

/*
*	BT.601、无stride、w必须为偶数
*/
int rgb_to_yuyv(unsigned char* yuyvdata,unsigned char* rgbdata,int width,int height)
{
	unsigned char data[6];
	unsigned char r0,g0,b0,r1,g1,b1;
	int Y0,U0,V0,Y1,U1,V1;
	int i;

	/* 条件判断 */
	if(full == -1)
	{
		return -1;
	}

	if(width <= 0 || width & 1 || height <= 0)
	{
		fprintf(stderr, "%s,Parameter error\n",__FUNCTION__);
		return -1;
	}

	/* 分情况处理 */
	for(i = 0 ; i < width * height /2 ; i++)
	{
		memcpy(data,&rgbdata[i * 6],6);
		r0 = data[0];
		g0 = data[1];
		b0 = data[2];
		r1 = data[3];
		g1 = data[4];
		b1 = data[5];

		if(full)
		{
			Y0 = 0.299 * r0 +0.587 * g0 + 0.114 * b0;
			clamp(&Y0);
			U0 = -0.1687 * r0 - 0.3313 * g0 + 0.5 * b0 + 128;
			clamp(&U0);
			V0 = 0.5 * r0 - 0.4187 * g0 - 0.0813 * b0 +128;
			clamp(&V0);

			Y1 = 0.299 * r1 +0.587 * g1 + 0.114 * b1;
			clamp(&Y1);
			U1 = -0.1687 * r1 - 0.3313 * g1 + 0.5 * b1 + 128;
			clamp(&U1);
			V1 = 0.5 * r1 - 0.4187 * g1 - 0.0813 * b1 +128;
			clamp(&V1);
		}
		else 
		{
			Y0 = 0.257 * r0 +0.504 * g0 + 0.098 * b0 + 16;
			clamp(&Y0);
			U0 = -0.1482 * r0 - 0.2910 * g0 + 0.4392 * b0 + 128;
			clamp(&U0);
			V0 = 0.4392 * r0 - 0.3678 * g0 - 0.0714 * b0 +128;
			clamp(&V0);

			Y1 = 0.257 * r1 +0.504 * g1 + 0.098 * b1 + 16;
			clamp(&Y1);
			U1 = -0.1482 * r1 - 0.2910 * g1 + 0.4392 * b1 + 128;
			clamp(&U1);
			V1 = 0.4392 * r1 - 0.3678 * g1 - 0.0714 * b1 +128;
			clamp(&V1);
		}

		/* 转换 */
		yuyvdata[i * 4 + 0] = Y0;
		yuyvdata[i * 4 + 1] = (U0 + U1) / 2;
		yuyvdata[i * 4 + 2] = Y1;
		yuyvdata[i * 4 + 3] = (V0 + V1) / 2;	
	}
	
	return 0;
}

/*	
*	最小转换：
*	两个RGB -> Y0 U0 V0 | Y1 U1 V1 -> Y0 U Y1 V
*/
int rgb_to_yuyv_pair(unsigned char*yuyvdata,struct rgb rgb_0,struct rgb rgb_1,int is_full)
{
	unsigned char r0,g0,b0,r1,g1,b1;
	int Y0,U0,V0,Y1,U1,V1;

	r0 = rgb_0.r;
	g0 = rgb_0.g;
	b0 = rgb_0.b;
	
	r1 = rgb_1.r;
	g1 = rgb_1.g;
	b1 = rgb_1.b;
	
	if(is_full)
	{
		Y0 = 0.299 * r0 +0.587 * g0 + 0.114 * b0;
		clamp(&Y0);
		U0 = -0.1687 * r0 - 0.3313 * g0 + 0.5 * b0 + 128;
		clamp(&U0);
		V0 = 0.5 * r0 - 0.4187 * g0 - 0.0813 * b0 +128;
		clamp(&V0);

		Y1 = 0.299 * r1 +0.587 * g1 + 0.114 * b1;
		clamp(&Y1);
		U1 = -0.1687 * r1 - 0.3313 * g1 + 0.5 * b1 + 128;
		clamp(&U1);
		V1 = 0.5 * r1 - 0.4187 * g1 - 0.0813 * b1 +128;
		clamp(&V1);
	}
	else 
	{
		Y0 = 0.257 * r0 +0.504 * g0 + 0.098 * b0 + 16;
		clamp(&Y0);
		U0 = -0.1482 * r0 - 0.2910 * g0 + 0.4392 * b0 + 128;
		clamp(&U0);
		V0 = 0.4392 * r0 - 0.3678 * g0 - 0.0714 * b0 +128;
		clamp(&V0);

		Y1 = 0.257 * r1 +0.504 * g1 + 0.098 * b1 + 16;
		clamp(&Y1);
		U1 = -0.1482 * r1 - 0.2910 * g1 + 0.4392 * b1 + 128;
		clamp(&U1);
		V1 = 0.4392 * r1 - 0.3678 * g1 - 0.0714 * b1 +128;
		clamp(&V1);
	}
	
	/* 转换 */
	*yuyvdata       = Y0;
	*(yuyvdata + 1) = (U0 + U1) / 2;
	*(yuyvdata + 2) = Y1;
	*(yuyvdata + 3) = (V0 + V1) / 2;	
	
	return 0;
}

/*
*	生成八种颜色条状区域——YUYV格式
*/
unsigned char* generate_yuyv_region(int width,int height)
{
	int i,j;
	unsigned char* yuyvdata;
	int i0;	
	int i1;

	/* rgb格式条状区域 */
	struct rgb bars[8] = {
		{255, 255, 255},  // 白
		{255, 255,	 0},  // 黄
		{  0, 255, 255},  // 青
		{  0, 255,	 0},  // 绿
		{255,	0, 255},  // 品红
		{255,	0,	 0},  // 红
		{  0,	0, 255},  // 蓝
		{  0,	0,	 0},  // 黑	
	};
	
	/* 条件判断 */
	if(width <= 0 || height <= 0 || width & 1 || width < 8 )
	{
		fprintf(stderr, "%s,Parameter error\n",__FUNCTION__);
		return NULL;
	}

	if(full == -1)
	{
		return NULL;
	}

	/* 获得一片空间 */
	size_t size;
	unsigned char*buf;
	
	size = (size_t)width * (size_t)height * 2;
	buf = malloc(size);
	if(buf == NULL)
	{
		fprintf(stderr, "%s,malloc error\n",__FUNCTION__);
		return NULL;
	}
	memset(buf , 0 ,size);

	/* 循环处理_rgb转YUYV */
	for(i = 0 ; i < height; i++)
	{
		yuyvdata = buf + i * width * 2;
		for(j = 0 ; j < width ; j += 2)
		{
			/* 计算两个像素落在何区域，共八区域 */
			i0 = j * 8 / width;
			i1 = (j + 1) * 8 / width;
			if(i0 > 7) i0 = 7;
			if(i1 > 7) i1 = 7;

			/* rgb to yuyv */
			if(rgb_to_yuyv_pair(yuyvdata + j * 2, bars[i0], bars[i1], full) == -1)
			{
				free(buf);
				return NULL;
			}
		}
	}
	//free(buf);
	return buf;
}

