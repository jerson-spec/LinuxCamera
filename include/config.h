#ifndef CAMERA_CONFIG_H
#define CAMERA_CONFIG_H

typedef struct {
    char device[64];
    int  width;
    int  height;
    char format[8];
    int  buffers;
    int  save_count;
    char save_dir[256];
	int flag;
	char log_level[16];
	char log_file[256];
	
	char lcd_device[64];
	unsigned int lcd_color;
	int lcd_display_size;
	char lcd_log_level[16];
	char lcd_log_file[256];
	int lcd_display_time;
} app_config_t;

int  config_load(const char *path, app_config_t *cfg);
void config_print(const app_config_t *cfg);

#endif

