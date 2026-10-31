#include "config.h"
#include "ini.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int copy_str(char *dst, size_t cap, const char *src)
{
    int n = snprintf(dst, cap, "%s", src);
    return n >= 0 && (size_t)n < cap;
}

static int parse_int(const char *s, int min, int max, int *out)
{
    char *end = NULL;
    errno = 0;
   // long v = strtol(s, &end, 10);
    long v = strtol(s, &end, 0);

    if (errno != 0 || end == s || *end != '\0' || v < min || v > max)
        return 0;

    *out = (int)v;
    return 1;
}
static int parse_unsigned_int(const char *s, unsigned int min, unsigned int max, unsigned int *out)
{
	char *end = NULL;
    errno = 0;
   // long v = strtol(s, &end, 10);
   unsigned long v = strtoul(s, &end, 0);

    if (errno != 0 || end == s || *end != '\0' || v < min || v > max)
        return 0;

    *out = (unsigned int)v;
    return 1;
}


static int on_key(void *user, const char *section,
                  const char *name, const char *value)
{
    app_config_t *c = user;

    if (!section)
        return 1;

    if (!strcmp(section, "camera")) {
        if (!strcmp(name, "device"))
            return copy_str(c->device, sizeof(c->device), value);
        if (!strcmp(name, "width"))
            return parse_int(value, 16, 4096, &c->width);
        if (!strcmp(name, "height"))
            return parse_int(value, 16, 4096, &c->height);
        if (!strcmp(name, "format"))
            return copy_str(c->format, sizeof(c->format), value);
        if (!strcmp(name, "buffers"))
            return parse_int(value, 2, 8, &c->buffers);
		 if (!strcmp(name, "flag"))
            return parse_int(value, 0, 1, &c->flag);
    } else if (!strcmp(section, "storage")) {
        if (!strcmp(name, "save_count"))
            return parse_int(value, 0, 100000, &c->save_count);
        if (!strcmp(name, "save_dir"))
            return copy_str(c->save_dir, sizeof(c->save_dir), value);
    }
	  else if (!strcmp(section, "log")) {
        if (!strcmp(name, "log_level"))
            return copy_str(c->log_level, sizeof(c->log_level), value);
        if (!strcmp(name, "log_file"))
             return copy_str(c->log_file, sizeof(c->log_file), value);	
	 }
	  else if (!strcmp(section, "lcd")) {
        if (!strcmp(name, "device"))
            return copy_str(c->lcd_device, sizeof(c->lcd_device), value);
      	if (!strcmp(name, "color"))
            return parse_unsigned_int(value, 0, 0xffffffff, &c->lcd_color);
		if (!strcmp(name, "display_size"))
            return parse_int(value, 0, 10 , &c->lcd_display_size);
		if (!strcmp(name, "display_time"))
            return parse_int(value, 0, 100 , &c->lcd_display_time);
	  }
	  else if (!strcmp(section, "lcd_log")) {
        if (!strcmp(name, "log_level"))
            return copy_str(c->lcd_log_level, sizeof(c->lcd_log_level), value);
		if (!strcmp(name, "log_file"))
            return copy_str(c->lcd_log_file, sizeof(c->lcd_log_file), value);		
	  }
	  

    fprintf(stderr, "config: unknown key [%s] %s\n", section, name);
    return 1;
}

static void set_defaults(app_config_t *c)
{
    snprintf(c->device, sizeof(c->device), "%s", "/dev/video0");
	
    c->width = 640;
    c->height = 480;
    snprintf(c->format, sizeof(c->format), "%s", "MJPG");
    c->buffers = 4;
    c->save_count = 50;
	c->flag = 1;
    snprintf(c->save_dir, sizeof(c->save_dir), "%s", "./images");
	
	snprintf(c->log_level, sizeof(c->log_level), "%s", "DEBUG");
 	snprintf(c->log_file, sizeof(c->log_file), "%s", "./camera.log");

	snprintf(c->lcd_device ,sizeof(c->lcd_device),"%s","/dev/fb0");
	c->lcd_color = 0xffff0000;
	c->lcd_display_size  = 1;
	c->lcd_display_time  = 10;
	snprintf(c->lcd_log_level ,sizeof(c->lcd_log_level),"%s","DEBUG");	
	snprintf(c->lcd_log_file  ,sizeof(c->lcd_log_file), "%s","./lcd.log");
}

int config_load(const char *path, app_config_t *c)
{
    if (!path || !c)
        return -1;

    set_defaults(c);

    int line = ini_parse(path, on_key, c);

    if (line < 0) {
        fprintf(stderr, "config: cannot open %s\n", path);
        return -1;
    }
    if (line > 0) {
        fprintf(stderr, "config: %s:%d: parse error\n", path, line);
        return -1;
    }

    if (c->device[0] == '\0' ||
        c->format[0] == '\0' ||
        c->save_dir[0] == '\0' ||
        c->width < 16 || c->height < 16 ||
        c->buffers < 2 || c->buffers > 8 ||
        c->save_count < 0 || ((c->flag != 0) && (c->flag != 1)) ||
        c->log_file[0] == '\0' || c->log_level[0] == '\0' || c->lcd_device[0] == '\0' || 
        c->lcd_display_size <= 0 || c->lcd_display_size >= 10 || c->lcd_log_file[0] == '\0' ||
        c->lcd_log_level[0] == '\0' || c->lcd_display_time < 0 || c->lcd_display_time > 100){
        fprintf(stderr, "config: invalid values\n");
        return -1;
    }

    return 0;
}

void config_print(const app_config_t *c)
{
    printf("device     = %s\n", c->device);
	
    printf("format     = %s\n", c->format);
    printf("resolution = %dx%d\n", c->width, c->height);
    printf("buffers    = %d\n", c->buffers);	
    printf("flag       = %d\n", c->flag);
	
    printf("save_count = %d\n", c->save_count);
    printf("save_dir   = %s\n", c->save_dir);	

	printf("log_level  = %s\n",c->log_level);
	printf("log_file   = %s\n",c->log_file);

    printf("lcd_device = %s\n", c->lcd_device);
	printf("lcd color  = 0x%x\n", c->lcd_color);
	printf("lcd_display_size  = %d\n", c->lcd_display_size);	
	printf("lcd_display_time  = %d\n", c->lcd_display_time);

	printf("lcd_log_level  = %s\n",c->lcd_log_level);
	printf("lcd_log_file   = %s\n",c->lcd_log_file);
	
}

