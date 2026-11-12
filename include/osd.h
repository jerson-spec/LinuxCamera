#ifndef _OSD_H_
#define _OSD_H_

int osd_show_text(unsigned int x, unsigned int y ,
	const unsigned char* str, unsigned int color);

int osd_text_width(const unsigned char* str);

int osd_show_text_right(unsigned int right_x, unsigned int y ,
	const unsigned char* str, unsigned int color);

int osd_show_text_shadow(unsigned int x, unsigned int y ,
	const unsigned char* str, unsigned int color, unsigned int shadow_color);

int osd_show_rect(unsigned int x, unsigned int y ,
	unsigned int w, unsigned int h, unsigned int color);


#endif
