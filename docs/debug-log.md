# [ YUYV < - > RGB]——26_11_03

- test_Trans_YUYV.c怎么链接test_Draw_region.c得到里面的lcd_put_pixel函数是个问题，得到了以后又怎么用又是个问题，我是简单的#if else,endif把test_Draw_region.c里的main注释了然后再创建了一个新函数Get_lcd_parameters把我想要的东西返回，因此加了一个test_Draw_region.h就放在LCD目录下，然后再在CMakelists里调整了一下，把test_Draw_region原来的add_executable改成了add_library，供test_Trans_YUYV调用，这样做并不规范，但确实能达到目的；

- test_Trans_YUYV.c里的clamp函数我一开始写的不对，压根没起作用，因此以下的YUYV<->RGB的代码夹逼部分都没起作用，是怎么个没起作用呢，是参数传int，里面对这个参数做变化，这样相当于对实参没一点影响，这样做的结果是，lcd上的色条full下：前四条全是白，然后后两条为品红，再一条蓝，一条黑，limit下：白，红，蓝，蓝，蓝，蓝不同程度的蓝，再青，黑，原因是没有夹逼，负值没有截到0而是溢出回绕到255，导致颜色出现混乱；

-  另一个相似的错误是Get_lcd_parameters函数，也是形参取址，调用该函数时传入一个地址，形参地址等于此，然后函数里把test_Draw_region.c里的fb_fix_screeninfo全局结构体地址再赋给形参，相当于从始至终围着形参转而从未对实参及其地址作出半点修改，这样做的后果是得不到lcd的参数，直接段错误；

- 除此之外，yuyv转rgb的公式，full情况下是正确的，以上错误都修改完后full下的显示是正确的白，黄，青，绿，品红，红，蓝，黑八条带，但limit情况下是错误的，公式写错了，一是1.596这个参数写成了1.159，二是g的公式(V1 - 128)前的0.813 ‘-’写成了‘+’，这里比如说取一个rgb，255,0,255,转一下yuyv格式，然后把转出来的数代到这个公式里就可以发现确实错了，并没有正确得到255,0,255，解决的办法就是重新检查公式，对应修改过来，修改完后，两种情况下均能正常显示了；

  
  code -> git : 
  ```
  lcd: yuyv color bars displayed
  ```
  
  ```
  tag: lcd-yuyv-milestone
  ```



# 【整理目录结构】——26_11_04

- lcd里面类似于struct fb_fix_screeninfo lcd_fix_screeninfo_t;  unsigned int fb_size;这样的定义不能直接放到.h文件里，如果多个文件包含，重复定义这些变量，必会出错，正确的办法是在.h文件里用extern 声明外部可用，extern struct fb_fix_screeninfo lcd_fix_screeninfo_t; 这样包含该头文件的文件中就可以使用这个变量，但更好的做法是在函数中用参数传递，否则这样使用的次数多了一是会造成混乱，二是在头文件中引入变量，包含该头文件的文件多了，内存占用也会被撑大，不是很规范的做法；我们这里实际上并不需要把这些变量引出去给别的文件用，一开始没想过来后面明白了不必要把这些东西压到头文件里；

- 另外关于头文件中的文件包含，例如在头文件中加#include <linux/fb.h>，这样引用者和源文件里都无需再额外包含该文件，即使包含了，对一般的.h文件，例如这里的fb.h，一般都会有#ifndef ,define的逻辑，所以也没事；这样的方式对引用者来说是个便利，不需要关心引用外部文件后是不是需要补上什么文件包含关系，只关心使用即可；

- 在tests/manual/test_Draw_region.c文件中，一开始是定义struct fb_fix_screeninfo *lcd_fix_screeninfo_p;，然后下面直接调用lcd.h/c的	Get_lcd_parameters(lcd_fix_screeninfo_p,lcd_var_screeninfo_p);函数，然后在下面把里面的成员引出来打印，编译上机，结果就是段错误：这里是野指针，lcd_fix_screeninfo_p在定义后没指向任何值、分配任何空间，然后直接传参，就会造成错误，又因为我们这里这个函数没有错误检查没有返回值，所以一直走到下面的 	LOG_INFO("Length of frame buffer mem : %u\n",lcd_fix_screeninfo.smem_len); 才报段错误；正确的方式是定义后先给他指向一个结构体，然后传参，传完后就可以使用这个指向的结构体，但是这里更简便的办法是直接定义struct fb_fix_screeninfo lcd_fix_screeninfo;，然后	Get_lcd_parameters(&lcd_fix_screeninfo,&lcd_var_screeninfo);，之后就可以打印；

- scripts/*.sh这些脚本文件一开始Ubuntu上运行报/bin: bad interpreter: Permission denied错误，原因是脚本是 **Windows 换行符（CRLF）**，不是 Linux 换行符（LF）。`/usr/bin/env` 看到的解释器名变成了 `bash\r`（多了个 `\r`），所以找不到。解决的办法是 sudo apt install dos2unix -y 
  dos2unix scripts/ *.sh，这是一个用于将 Windows 格式的文本文件转换为 Unix/Linux 格式的工具。这样转换了之后就可以./build.sh或其他.sh文件自动执行经常输入的那些命令了，这里也作了一个提醒，shell命令需要自己会写，光能看懂和修改还不够，短期长期来看都是这样；
  
  code -> git : 
  
  ```
  Change the directory structure, add docs and scripts
  ```
  
  

# [camera_preview]——26_11_05

- 一开始编译成RelWithDebInfo时config打印混乱，camera.ini的配置项有的解析不出来，而lcd.ini的配置项就干脆全都没解析，在配置lcd下的config_print那里打印的还是camera.ini的信息，而且是阉割的信息，最后parse log level  error返回，后面发现问题应该是两次config_load，中间出了问题，第二次config_load会**复用同一个 buffer**，导致解析出现混乱，解决的办法是app_config_t定义camera cfg，lcd_cfg结构体后memset(&camera_cfg, 0, sizeof(camera_cfg));，memset(&lcd_cfg, 0, sizeof(camera_cfg));。先把这片空间初始成0，之后再使用，这样就没问题了，但是当我重新编译重新试验后，问题依然存在，后来这个问题我暂时没管，去调试，./build_dbg.sh，./deploy_dbg.sh，板子shell上先gdbserver :1234 ./test_camera_preview，ubuntu上再./gdb.sh test_camera_preview（后面一定要加test_camera_preview，不加的话默认调试的是test_Get_images），之后调试发现居然一点问题没有，我还以为是因为DEBUG的优化关系导致RelWithDebInfo不行DEBUG能行，后面才发现原来我修改完后没有rm -rf build-arm，直接./build.sh没有清**旧的 build 缓存**，所以依然失败，和优化并不相关；——这个错误也提示我在之后应当修改下app_config_t的形式，现在像是一锅大杂烩，各种设备的配置项全用一个结构体表示，不仅不规范，还可能造成错误，后面要把他分离一下；

- 还有一个问题是gdb调试时发现的，程序在yuyv_to_rgb函数里走到	if(full == -1)，判断成立，直接返回-1，然后程序结束，根本走不到下面的lcd_put_pixel，这就是前面留的坑，在yuyv_to_rgb之前必须先调用yuyv.c里的Is_full_range函数根据配置项里是全范围还是有限范围置全局变量full = 1或0，不事先调用这个函数的话我在yuyv.c函数里默认给这个全局变量置值为-1，所以yuyv_to_rgb时出错，这个问题不难发现，后来在test_camera_preview函数里lcd_init 后补上 Is_full_range(&lcd_cfg) 即可;

- 上机后直接执行./test_camera_preview会出现 “画面没有铺满屏幕，而是缩在左上角，并且左上角的画面显示为上面有一条混乱的雪花彩点，下面所有区域均为绿色”的情况，原因在于config/camera.ini里写的format默认是MJPEG/JPEG，而我们代码里做的是采集YUYV格式照片，然后YUYV to RGB，再lcd_put_pixel，所以把配置项里的这一项修改为YUYV即能解决问题，画面显示正常，缩在左上角，因为我们在配置项里选的分辨率是640 * 480，小于这个屏幕分辨率1024 * 600 ，又结合代码，缩在左上角是正常的；

- 把配置项分辨率选择为1024x600后屏幕高度填满，长度没满，右边依然有黑条，原因在于V4L2 的 `S_FMT` 不是“强制设置”，而是“协商”，v4l2-ctl -d /dev/video1 --list-formats-ext检查摄像头设备（video1）支持的分辨率时发现YUYV下只有800x600，下一个就到了1280x720，所以`VIDIOC_S_FMT` 之后驱动就会返回最接近的 800×600 ，因此不是我们预期设置的值，没有填满lcd，想铺满 1024×600的话后面可以采取缩放的方式；

  code -> git : 

  ```
  v0.1: camera YUYV preview on LCD
  ```
  ```
  tag: v0.1
  ```
