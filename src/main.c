#include "config.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
	if(argc > 2 )
	{
		printf("Usage:%s [ini path/camera.ini]\n",argv[0]);
		return -1;
	}

    const char *path = (argc > 1)? argv[1]: "config/camera.ini";
    app_config_t cfg;

    if (config_load(path, &cfg) != 0)
        return -1;

    config_print(&cfg);
    return 0;
}

