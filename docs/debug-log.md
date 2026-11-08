# [ YUYV < - > RGB ]——26_11_03

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



# 【 整理目录结构 】——26_11_04

- lcd里面类似于struct fb_fix_screeninfo lcd_fix_screeninfo_t;  unsigned int fb_size;这样的定义不能直接放到.h文件里，如果多个文件包含，重复定义这些变量，必会出错，正确的办法是在.h文件里用extern 声明外部可用，extern struct fb_fix_screeninfo lcd_fix_screeninfo_t; 这样包含该头文件的文件中就可以使用这个变量，但更好的做法是在函数中用参数传递，否则这样使用的次数多了一是会造成混乱，二是在头文件中引入变量，包含该头文件的文件多了，内存占用也会被撑大，不是很规范的做法；我们这里实际上并不需要把这些变量引出去给别的文件用，一开始没想过来后面明白了不必要把这些东西压到头文件里；

- 另外关于头文件中的文件包含，例如在头文件中加#include <linux/fb.h>，这样引用者和源文件里都无需再额外包含该文件，即使包含了，对一般的.h文件，例如这里的fb.h，一般都会有#ifndef ,define的逻辑，所以也没事；这样的方式对引用者来说是个便利，不需要关心引用外部文件后是不是需要补上什么文件包含关系，只关心使用即可；

- 在tests/manual/test_Draw_region.c文件中，一开始是定义struct fb_fix_screeninfo *lcd_fix_screeninfo_p;，然后下面直接调用lcd.h/c的	Get_lcd_parameters(lcd_fix_screeninfo_p,lcd_var_screeninfo_p);函数，然后在下面把里面的成员引出来打印，编译上机，结果就是段错误：这里是野指针，lcd_fix_screeninfo_p在定义后没指向任何值、分配任何空间，然后直接传参，就会造成错误，又因为我们这里这个函数没有错误检查没有返回值，所以一直走到下面的 	LOG_INFO("Length of frame buffer mem : %u\n",lcd_fix_screeninfo.smem_len); 才报段错误；正确的方式是定义后先给他指向一个结构体，然后传参，传完后就可以使用这个指向的结构体，但是这里更简便的办法是直接定义struct fb_fix_screeninfo lcd_fix_screeninfo;，然后	Get_lcd_parameters(&lcd_fix_screeninfo,&lcd_var_screeninfo);，之后就可以打印；

- scripts/*.sh这些脚本文件一开始Ubuntu上运行报/bin: bad interpreter: Permission denied错误，原因是脚本是 **Windows 换行符（CRLF）**，不是 Linux 换行符（LF）。`/usr/bin/env` 看到的解释器名变成了 `bash\r`（多了个 `\r`），所以找不到。解决的办法是 sudo apt install dos2unix -y 
  dos2unix scripts/ *.sh，这是一个用于将 Windows 格式的文本文件转换为 Unix/Linux 格式的工具。这样转换了之后就可以./build.sh或其他.sh文件自动执行经常输入的那些命令了，这里也作了一个提醒，shell命令需要自己会写，光能看懂和修改还不够，短期长期来看都是这样；
  
  code -> git : 
  
  ```
  Change the directory structure, add docs and scripts
  ```
  
  

# [ camera_preview ]——26_11_05

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



# [ continuous preview loop ]——26_11_05

- 无调试，编写思路见obsidian；

  code -> git : 

  ```
  camera: continuous preview loop, single buffer (v0.5 step 1)
  ```

  

# [ track and count ]——26_11_06

 **最直观的两个问题：一是快速移动时有撕裂，二是帧率低，撕裂的问题后面可以通过双缓冲垂直同步等手段解决，帧率低的问题现在先测一测：**

- 首先不急，先做一个取放平衡的监测：定义三个变量dq，q和frames，在dqbuf,qbuf的位置分别成功后+1，frames也一样，在绘完一帧后++，这里我一开始没注意把他放到了两层for循环里的底部，结果frames的数当然很大，因为给他记成了绘制的像素数，合理的位置是在两层for循环后，和qbuf挨在一起，在qbuf成功后的位置，因为qbuf如果失败没有放回去的话，就不能认为是成功绘制了一帧，因而不让frames++，与之类似的是如果绘制某一帧时出现bytesused  < width * height * 2，表示短帧，那就不让他继续往下绘制，让他continue跳出本轮循环，在此之前qbuf把buf还回去；最后看结果，dqbuf,qbuf,frames三者应该是要相等的，即取、放、实际的绘制三者应相等，如果结果出现dq > q，表示取的比放的多，说明有取出来的没放回来，那表示循环内中间某环节出了问题，长期来看的话慢慢就会造成卡死，这就是这三个参数的作用，可以及时发现取放不平衡的问题，这里我们试验多次，一分钟，三分钟，几种情况三者都相等，是正常的；

- 之后就是测时间，以一秒为度量测帧率、测耗时、测跳帧，首先引入<time.h>，把获得系统启动后时间clock_gettime函数（参数传CLOCK_MONOTONIC：自系统启动以来的时间（不受系统时间调整影响））封装进一个get_time_ms函数，然后定义几个double型时间变量t0 - t3，分别在1.poll之前、2.poll和dqbuf之后，YUYV->RGB之前、3.YUYV->RGB之后，lcd_put_pixel之前、4.lcd_put_pixel之后获得系统时间，最后对应相减，即得：等待数据时间、YUYV转换时间、lcd绘制时间，我们让他们 += ，实现累加，为下面的打印做准备；——这里第一个t0一定要放在poll之前，我一开始放在poll之后，dqbuf之前，语义是不对的，没有体现记录“等待/dev/video*设备传来数据供我们读”的耗时时间的作用，结果就是这个wait_time始终接近于0；

- 关于丢帧数，buf的sequence成员（帧序列号）可以帮我们实现，如果当前buf的sequence成员比前一个buf的大一，正常，否则不正常，丢帧，把多的部分 += 记录下来，即可；

- 关于fps（每秒帧率），类似的思想，记录当前时间是否已超出前一个记录的时间一秒，是则记为dt，其接近一秒，之后frames / dt，即为fps，此外上面算出的三项耗时时间过了一秒也累加出一个结果，即这一秒内三项的耗时时间都知道了，除以这个frames一秒内绘制帧数，即得一秒内平均每一帧对应的三项耗时时间（ms）；

- 最后的实验结果是稳定后三项加起来乘以fps接近1000，即一秒，且表现为前几秒（6秒左右，第一帧wait时间最长，约一秒，fps接近1，摄像头/驱动要等曝光收敛和第一个完整帧，正常）不太稳定，后面就逐步稳定（与代码的处理、摄像头的节拍、cpu调频均有密切关系），都表现在fps: 15.0左右, wait: 2~3左右, conv: 34.8左右, draw: 28.1左右, dropped: 0，偶发性dropped出现1，即偶尔跳帧，总体表现正常；

- 后面v4l2-ctl -d /dev/video1 --list-formats-ext后看YUYV，800*600分辨率的情况下，fps到顶也就15，也就是说相机特性就是这样，降低分辨率fps能冲到30，这就是帧率的极限了；

- 这里get_time_ms函数的返回值是double，我一开始是直接在最前面加一个（double）强转，但实际上后面的 “sec * 1000”，尤其是“ nsec / 1000000“更需要加强转，原因是ts.tv_sec和nsec是整型，不给他们加强转的话就丢失了浮点精度，因此比较合适的是return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0 ;

    code -> git : 

    ```
     camera: track DQBUF/QBUF ownership; count fps,time spent on each item,dropped frames
    ```



# [ Enable double buffering ]——26_11_07

- 这里关键是在获取可变参数后设置xres_virtual和yres_virtual，把yres_virtual设置成两倍于lcd分辨率yres，然后再在mmap之前判断一下fb_size，lcd_fix_screeninfo_t.smem_len是否大于两倍的lcd分辨率大小，这里有个坑，是lcd分辨率大小计算时我一开始xres * yres 忘记乘bits_per_pixel了，结果就是这个值很小，和fb_size的34,359,738,368结果，也就是32M相去甚远，这样显然是错误的，这里我能够发现这个问题是因为恰巧这时候检查smem_len大小的判断条件我写的是fb_size == 2 * lcd_size，这个条件显然是不成立的，应该写成大于等于，所以走下面的LOG_ERROR分支把fb_size和lcd_size都打印出来；说来好笑，改完这个还不对，因为没有除以8算字节数，结果就是这个值非常大，这样显然也是错误的，不用==，就算改成>=依然走LOG_ERROR分支把错误打印出来，这两个问题都比较好发现；——一开始我还分不清fb_size，即lcd_fix_screeninfo_t.smem_len，和我们设置的yres_virtual 之间的关系，我以为既然我们设置了xres_virtual ,yres_virtual ，那再重新FBIOGET_FSCREENINFO获取lcd_fix_screeninfo_t.smem_len的时候这个“Length of frame buffer mem”值他会给我们分配为等于这个xres_virtual * yres_virtual 的字节数，实际上并不是这样的，”smem_len 是 驱动 probe 时预留的整块显存（本例 32MB），而yres_virtual  只是告诉驱动“这块内存里，前 1200 行是有效的”“，关于这个的概念模糊是引发我下面很多处错误的罪魁祸首；

- 改完了下面的lcd_init里的相应内容分配好了空间就到上面的lcd_flip函数了，这里面最关键的就是两件事，一是设置绘制翻转，一是设置显示翻转，二者取反，设置区域为绘制另一个区域就显示，设置区域为显示另一个区域就绘制，对应涉及到两个偏移，关于绘制的翻转，只需要在我们原先的pos = mem_addr + y * lcd_fix_screeninfo_t.line_length + x * lcd_var_screeninfo_t.bits_per_pixel / 8;公式基础上加上一个draw_offset ，然后draw_offset 在0和lcd_size lcd分辨率大小之间来回取值即可，这里注意我一开始把他设置的是 xres *yres *bitsperpixel /8，这样不兼容不紧凑的情况，更好的是改成lcd_var_screeninfo_t.yres * lcd_fix_screeninfo_t.line_length；关于显示的翻转，涉及到ioctl FBIOPAN_DISPLAY函数，由他来设置可见区，而这个的偏移计算就用lcd_size / lcd_fix_screeninfo_t.line_length，算出y方向像素偏移，这个值在这里就等于lcd yres分辨率，之后y_offset在0和lcd_size / lcd_fix_screeninfo_t.line_length之间来回取值即可实现可见区在两块区域间不停切换；

- 上机实验发现走到lcd.c 里ioctl FBIOPAN_DISPLAY函数后判断条件不成立走下面的LOG_ERROR("ioctl: FBIOPAN_DISPLAY, xoffset: %u, yoffset: %u",...)分支，因为一开始我没加ioctl 里返回-1的处理，和test_camera_preview.c里调用lcd_flip函数后返回值的判断，所以结果就表现为一直打印LOG_ERROR信息，但是LOG_ERROR打印出来的现象又是符合我们代码逻辑的，即xoffset一直为0，yoffset在0和600之间来回切换（lcd分辨率大小是1024*600），这实际上能证明双缓冲切换是成功了的；这就很奇怪，一开始我还以为设备不支持FBIOPAN_DISPLAY，都已经找好了后备方案（自己 `malloc` 一块  RGB565 后台缓冲，画完 `memcpy` 到 fb），后面重新编译debug，上机gdb调试，结果发现好了，lcd_flip函数成功执行完返回，再去执行lcd_put_pixel（这个时候没注意lcd_put_pixel和lcd_flip的先后顺序，实际上lcd_flip应该在lcd_put_pixel之后，否则最后绘制的一帧永远显示不到，这是关于尾帧的处理，关于首帧的处理也很有意思，注意到lcd_put_pixel函数和lcd_flip函数的关联仔细琢磨能处理好，后面我都改好了），后面才发现原来和lcd的控制台消隐、屏幕休眠有关，休眠时设置ioctl FBIOPAN_DISPLAY会失败，所以结果<0走error分支，后面查询了一下方法，echo 0 > /sys/class/graphics/fb0/blank临时唤醒，echo -e "\033[9;15]" > /dev/tty0设置持续显示15分钟，15改成0完全禁止消隐（屏幕常亮），之后再实验，果然就没有再出现过ioctl FBIOPAN_DISPLAY失败的问题；——另外发现debug模式下相机显示画面明显有延迟、滞后，相比之下build type为RelWithDebInfo的情况下相机的拍摄就顺畅很多，这在我openzen-visualizer的项目里也有所体会；

  code -> git : 

  ```
   Enable double buffering
  ```

  

 # [ Add double buffering downgrade ]——26_11_07

- lcd初始化函数里如果成功分配了双缓冲空间，就置全局变量Is_lcd_doublebuffering为1，再创建一个函数int lcd_doublebuffering_on(void)在里面只做一件事——把这个全局变量返回去，这样test_camera_preview函数先调用该函数判断Is_lcd_doublebuffering的值，判断成功设置了双缓冲才执行下面的lcd_flip函数，否则降级为单缓冲：一方面由于draw_offset是全局变量，如果一开始lcd_init后Is_lcd_doublebuffering没被置为1，那执行到if(lcd_doublebuffering_on())直接跳出去，再下一轮lcd_put_pixel里pos = mem_addr + draw_offset + y * lcd_fix_screeninfo_t.line_length + x * lcd_var_screeninfo_t.bits_per_pixel / 8;，draw_offset为0，照常绘制，是单缓冲；另一方面如果一开始test_camera_preview里调用lcd_init后，lcd_init里确实把Is_lcd_doublebuffering置为1，返回来条件成立可以往下继续执行到下面的lcd_flip函数里执行ioctl FBIOPAN_DISPLAY，如果在这里失败，那就表明双缓冲切换失败，于是就去把Is_lcd_doublebuffering以及draw_offset都置为0，再返回-1，回test_camera_preview判断失败后直接continue，这样下轮循环以及之后就都不走if(lcd_doublebuffering_on())分支了，lcd_put_pixel依然照常绘制，draw_offset为0，是单缓冲；

- 这里一开始我把全局变量变量名和int lcd_doublebuffering_on(void)函数函数名都取作lcd_doublebuffering_on，结果编译错误，所以不要把函数名和变量名取作一样；

- 如上所述，最后一帧丢失、一开始黑帧：把lcd_flip放到lcd_put_pixel之后，另外不要夹在时间计算那一堆东西里，所以后面干脆放到了最后；在lcd_init最后把draw_offset 设为lcd_size（改lcd_flip里起始y_offset也行，但麻烦，还容易出错（变量赋值失败（左值？、右值？）），在lcd_init里加draw_offset起始绘制偏移最省事）；

- 设置可变参数后要更新lcd_var_screeninfo_t和lcd_fix_screeninfo_t才能继续读变量值，一开始我以为设置可变参数后只需要更新lcd_var_screeninfo_t，判断一下lcd_var_screeninfo_t.yres_virtual是不是成功设置成等于两倍lcd_var_screeninfo_t.yres就够了，实际上接下来要读lcd_fix_screeninfo_t.smem_len的话也需要更新lcd_fix_screeninfo_t后才能读，原因是“在 Linux framebuffer 框架下，调用 `FBIOPUT_VSCREENINFO`（即 `PUT_VSCREENINFO`）**确实有可能改变** `fb_fix_screeninfo` 中的 `line_length` 和 `smem_len`，`fb_fix_screeninfo` 之所以被称为“固定”，**并非指这些值在整个系统生命周期内绝对不变**，而是指**应用程序不能直接指定或修改它们**。这些值由驱动程序根据硬件能力和当前选定的视频模式计算得出，对用户空间而言是**只读**的。”——更具体的解释见obsidian；

  code -> git : 

  ```
   Add double buffering downgrade
  ```

  

# [ lcd: fix flip state update ]——26_11_08

- lcd_flip函数里如果ioctl失败，退出前置draw_offset为0有问题，假设退出时可见区设置在lcd_size那，那退出、下一轮降级为单缓冲时可见区没改过来画的一直没显示，就表现为lcd画面冻住了，这个问题我调试发现不了，因为我由双缓冲降级为单缓冲的办法是使lcd息屏/消影，所以降级为单缓冲后的画面我看不见，这里是让ai帮我审查代码时发现的，算是比较严重的一个问题，解决的办法是新增一个变量表示当前可见区，并在ioctl成功后才更新可见区和绘制区，这样如果ioctl失败，就把当前显示区返回为绘制区，以后单缓冲就继续在这片区域绘制，如果成功就正常走更新；这里我修改过一次没改对，更新应该是要放在icotl判断之后的，放在他之前，更新完，再判断，判断错误，把更新的值返回——这样相当于把没有成功更新的值返回，结果当然是出错的；

- 另外还有一些不足之处也让ai帮审查代码是时发现，一是test_camera_preview.c里while循环里几次判断失败都只break，break后走清理，然后return 0，这和正常ctrl + c退出后走的路径一模一样，没有区分，显然不合理，于是根据建议我新建了一个变量exit_code(一开始是用ret，然后在程序最后判断ret < 0，成立的话表示出错，return -1，但是这样区分还不够)初始化为0，并在循环里每一处失败break前加exit_code = 某个负值，退出后把这个值打印出来，再return -1或者这个负值，如果一路不出错的话就正常走最后的 return 0；——另外在这个过程中我还顺带修改了一会fprintf(stderr,....,...)，一会LOG_ERROR，一会perror的风格不一致的问题，都统一为用LOG_ERROR；但是循环外的那些失败打印，然后清理，然后return -1，打印方式没有改，主要太多了，而且大多用的是fprintf(stderr,....,...),也算是统一的；

- test_Get_images里之前修改过一次记录文件名的后缀的问题，但实际上只修改了一处还有一处没修改到，而且修改后后缀是.MJPEG/.MJPG和.YUYV，.MJPEG/.MJPG的情况还得改成.jpg，.YUYV的情况最好也改成.yuv，这样将来打开格式匹配更受支持，解决的办法很简单，就是做一个后缀判断，然后把判断结果设为.jpg和.yuv，然后再使用，即可；

- poll timout那个分支也有点问题，polltimeout后continue而不是break，想继续等，没问题，但是如果一直等下去日志一直空打印，不好，于是加了个判断等待次数的变量，poll走到timeout三次，即打印错误，然后break；这里还有一个问题，就是应该还要在下面dqbuffer成功后把这个变量置为0，不置为0的话相当于是判断累计出现三次超时，而不是连续出现的三次超时，我们想要的是后者的效果，即等待你连续三次都是超时，就退出，所以在下面成功dqbuffer后把变量清为0；

- 实验十分钟后得出一个实验结果，数据很好，VmRSS一直稳定在6324Kb一直没变过，内存没泄露；其他的fps、几项耗时也都和之前的实验结论类似，15fps，和0左右，三十几，三十几的值（ms）；另外dropped十分钟一直是0，零丢帧；DQbuf: 9036, Qbuf: 9036, Frames:9036，也都吻合；这里发现一个比较有意思的现象，就是明明我们在test_camera_preview.c里初始化了camera日志，然后下面lcd初始化，调用到lcd_init函数里的lcd日志初始化，两个日志都初始化了，但结果却是下面调用LOG_INFO、LOG_ERROR时信息全打印到lcd.log里了，而camera.log里是空的，原因看代码就能发现，后初始化的覆盖了先初始化的：log.c 的 `log_init()` 第一步就是：

  ```
  if (g_fp) { fclose(g_fp); g_fp = NULL; }   // 把 camera.log 关掉
  g_fp = fopen(path, "a");                    // 换成 lcd.log
  ```

​	 所以 `lcd_init` 一返回，全局 `g_fp` 就指向 `lcd.log` 了，之后所有 `LOG_*` 都只写 `lcd.log`。`camera.log` 会被创建（`fopen` 的 `"a"` 模式），但里面基本是	空的；

- code -> git : 

  ```
   lcd: fix flip state update
  - lcd_flip: only update visible/draw offset after FBIOPAN_DISPLAY succeeds
  - test_camera_preview: per-stage exit codes, poll-timeout guard, VmRSS in stats
  - test_Get_images: log file name with actual format
  ```

  





