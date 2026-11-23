#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/jiffies.h>

struct button_dev {
    struct gpio_desc *gpio;
    int irq;
    struct input_dev *input;
    unsigned int keycode;
    unsigned long last_jiffies;
    int last_state;
};

/*
 * 顺序和 DT 里 button-gpios 一致：
 *   第 0 个 -> KEY_F1（拍照）
 *   第 1 个 -> KEY_F2（切分辨率）
 */
static const unsigned int button_keycodes[] = {
    KEY_F1,
    KEY_F2,
};

static irqreturn_t button_irq(int irq, void *data)
{
    struct button_dev *b = data;
    int pressed;

    /* gpiod_get_value() 是逻辑值：GPIO_ACTIVE_LOW 时按下 = 1 */
    pressed = gpiod_get_value(b->gpio);
    if (pressed < 0)
        return IRQ_HANDLED;

    if (pressed == b->last_state)
        return IRQ_HANDLED;

    /* 只在按下沿去抖，松开沿不丢 */
    if (pressed &&
        time_before(jiffies, b->last_jiffies + msecs_to_jiffies(20)))
        return IRQ_HANDLED;

    b->last_jiffies = jiffies;
    b->last_state = pressed;
	
	/** input_event(dev, EV_KEY, code, !!value) **/
    input_report_key(b->input, b->keycode, pressed);

	/** input_event(dev, EV_SYN, SYN_REPORT, 0) **/
    input_sync(b->input);

    return IRQ_HANDLED;
}

static int button_probe(struct platform_device *pdev)
{
    struct input_dev *input;
    int i;
    int ret;

    /* 一个 input 设备，带两个键 */
	/** 分配一块由 pdev->dev 管理的输入设备空间 **/
    input = devm_input_allocate_device(&pdev->dev);
    if (!input)
        return -ENOMEM;

    input->name = "lc-button";

    for (i = 0; i < ARRAY_SIZE(button_keycodes); i++)
		/** 使这一个输入设备支持多个触发事件 **/
        input_set_capability(input, EV_KEY, button_keycodes[i]);	
    ret = input_register_device(input);
    if (ret)
        return ret;

    /* 每个 GPIO 一个中断，共用同一个 input 设备 */
    for (i = 0; i < ARRAY_SIZE(button_keycodes); i++) {
        struct button_dev *b;

		/** 分配一块由 pdev->dev 管理的 struct button_dev 空间 **/
        b = devm_kzalloc(&pdev->dev, sizeof(*b), GFP_KERNEL);
        if (!b)
            return -ENOMEM;

		/** 根据设备树中信息往这个 struct button_dev            结构体里填入各种信息 **/
        b->gpio = devm_gpiod_get_index(&pdev->dev, "button", i, GPIOD_IN);
        if (IS_ERR(b->gpio))
            return PTR_ERR(b->gpio);

        b->irq = gpiod_to_irq(b->gpio);
        if (b->irq < 0)
            return b->irq;

        b->input = input;
        b->keycode = button_keycodes[i];

		/** 注册 GPIO 中断处理函数 **/
        ret = devm_request_irq(&pdev->dev, b->irq, button_irq,
                               IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
                               "lc-button", b);
        if (ret)
            return ret;

        pr_info("button: %d irq=%d key=%u\n", i, b->irq, b->keycode);
    }

    return 0;
}

static const struct of_device_id button_of_match[] = {
    { .compatible = "linuxcamera,button" },
    { }
};
MODULE_DEVICE_TABLE(of, button_of_match);

static struct platform_driver button_driver = {
    .probe = button_probe,
    .driver = {
        .name = "lc-button",
        .of_match_table = button_of_match,
    },
};
module_platform_driver(button_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("LinuxCamera platform button driver");
