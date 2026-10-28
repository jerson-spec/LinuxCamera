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
} app_config_t;

int  config_load(const char *path, app_config_t *cfg);
void config_print(const app_config_t *cfg);

#endif

