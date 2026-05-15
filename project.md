# bootloader

### 1. 什么是 Bootloader？（前台服务员比喻）

平常我们写的代码（App）是写字楼里的公司。如果没有 Bootloader，芯片通电后会直接冲进公司开始干活。但有了 Bootloader，通电后的过程就变成了这样：

- **第一步：前台接待员（Bootloader）先到岗。** 芯片一通电，它最先运行。
- **第二步：观察情况。** 它的任务是问两个问题：
  1. “老板（开发者）现在要给我发新的工作手册（更新固件）吗？”
  2. “现在的写字楼（代码区）里有没有合法的公司在办公？”
- **第三步：决定去向。**
  - 如果要更新，它就搬个板凳坐下，从大门口（串口/USB/网口）接数据，把新手册写在纸上。
  - 如果不更新，它就指路：“走，去 2 楼开始执行正式任务！”然后它就把控制权交给正式代码。

------

### 2. 为什么要搞这玩意儿？（核心考点）

在工业现场，你不能每次改个代码都拆开机器插上仿真器（J-Link）。Bootloader 的核心作用就是 **IAP（在应用编程）**。

- **远程升级：** 机器装在零下 30 度的户外，你只要通过一根通讯线（串口或 CAN 总线）把代码发过去，Bootloader 就能自己把自己“洗脑”更新。
- **防死机/防变砖：** 如果主程序跑飞了，Bootloader 还能作为“紧急备用系统”把机器救回来。

------

### 3. 在西门子杯赛题里，它是怎么工作的？

在 2026 年的比赛中，你可能需要自己手写一个简单的 Bootloader。它的逻辑流程通常如下：

| **阶段**     | **动作**                                             | **重点**                          |
| ------------ | ---------------------------------------------------- | --------------------------------- |
| **启动阶段** | 芯片复位，进入 Bootloader 代码区。                   | 必须放在 Flash 的起始地址。       |
| **判断阶段** | 检查某个按键有没有按下，或者串口有没有收到特定指令。 | “进不进入更新模式”的开关。        |
| **传输阶段** | 通过串口（UART）接收 .bin 或 .hex 格式的代码包。     | 涉及到**校验（CRC）**，防止传错。 |
| **擦写阶段** | 把接收到的数据写进芯片 Flash 的“App 区域”。          | 这里的操作最危险，写错就“变砖”。  |
| **跳转阶段** | 修改堆栈指针（MSP），跳到 App 的地址开始运行。       | 这是最显功底的“瞬间转移”。        |

# 关于V2改动

## 引脚

U17传感器引脚

flash从 PB12-15更改到PB3,4,5

要替换按键 将单独的一些函数去掉

# 工程文件功能实现

## 休眠功能

  1. 关闭高耗能外设
     它先调用一系列 *_disable_for_deepsleep()：
     
      - bsp_usart_disable_for_deepsleep()
      - bsp_oled_disable_for_deepsleep()
      - bsp_spi_disable_for_deepsleep()
      - bsp_sdio_disable_for_deepsleep()
     
     这些函数做的事很直接：
      - 关串口中断、关 NVIC、关 DMA、关 USART
      - 关 OLED 显示、关 I2C、关 DMA
      - 关 SPI、关 SPI DMA，并把 CS 拉到非选中
      - 关 SDIO 时钟、关电源、反初始化 SDIO
     
     这是低功耗设计里最核心的一层: 先把会持续翻转、持续时钟、持续 DMA 的模块停掉。
  2. 关闭 ADC / DAC / 定时器等模拟与周期源
     在 bsp_enter_deepsleep() 里还额外做了：
      - adc_disable(ADC0)
      - adc_dma_mode_disable(ADC0)
      - dac_disable(DAC0, DAC_OUT0)
      - dac_dma_disable(DAC0, DAC_OUT0)
      - timer_disable(TIMER5)

     这说明作者知道单纯 CPU 睡眠不够，模拟外设和定时器如果不停，电流不会下来。
  3. 把 GPIO 改成低泄漏状态
     bsp_gpio_enter_deepsleep_state() 是这个文件里最像“低功耗设计”的部分。它做了两类关键动作：
      - 把大量 IO 改成 GPIO_MODE_ANALOG + GPIO_PUPD_NONE
      - 只保留必要唤醒脚 PA0(KEYW) 为输入上拉

     这么做的目的不是“为了功能”，而是为了减少数字输入悬空、电平翻转和上下拉泄漏。
     这是很多人容易漏掉的点，你如果只盯着 pmu_to_deepsleepmode()，会误以为低功耗只靠 PMU 指令，实际上GPIO 状态往往
     决定了睡下去以后电流能不能真的低。
  4. 配置唤醒源
     bsp_wkup_key_exti_init() 里把 PA0 配成 EXTI0，并打开中断：
      - syscfg_exti_line_config(EXTI_SOURCE_GPIOA, EXTI_SOURCE_PIN0);
      - exti_init(EXTI_0, EXTI_INTERRUPT, EXTI_TRIG_BOTH);
      - nvic_irq_enable(EXTI0_IRQn, 1U, 0U);

     也就是说，这个休眠方案当前主要依赖 PA0 外部按键唤醒。
     而且它用的是 EXTI_TRIG_BOTH，即双边沿触发，这意味着按下和释放都有机会唤醒。
  5. 真正进入深度休眠
     最后它调用：
      - pmu_to_deepsleepmode(PMU_LDO_LOWPOWER, PMU_LOWDRIVER_ENABLE, WFI_CMD);

     这句才是正式进入低功耗模式的核心：
      - PMU_LDO_LOWPOWER：LDO 进入低功耗模式
      - PMU_LOWDRIVER_ENABLE：开启低驱动能力，进一步降功耗
      - WFI_CMD：通过 Wait For Interrupt 进入休眠，等中断唤醒

  唤醒后怎么恢复：

  - 唤醒返回后它没有直接继续跑业务，而是执行 bsp_deepsleep_reinit_after_wakeup()。
  - 里面重新做了：
      - SystemInit()
      - SystemCoreClockUpdate()
      - systick_config()
      - 再初始化 LED、按键、USART、OLED、ADC、DAC、Flash、SD 卡等外设

  这说明作者的设计思路是：

  - 睡前尽量把外设关干净
  - 醒后重新初始化外设
  - 而不是指望所有外设上下文自动保持

# RTC配置相关

  - bsp_rtc_init() 里现在无条件调用 bsp_rtc_setup()。
  - 这意味着只要执行初始化，RTC 时间就会被重新写一遍。
  - 也就是说，这份代码默认不保留原有时间，除非你把下面那段被注释掉的备份域判断逻辑恢复。

  你问的“三个跟 RTC 有关的 API”，从 mcu_cmic_gd32f470vet6.c 看，核心就是这三个：

  - bsp_rtc_pre_cfg()
  - bsp_rtc_setup()
  - bsp_rtc_init()

  ## 1. 这三个 API 分别干什么

  ### bsp_rtc_pre_cfg()

  作用：给 RTC 准备时钟源和寄存器访问环境

  它做的事是：

  - 选择 RTC 时钟源
      - RCU_RTCSRC_IRC32K 或
      - RCU_RTCSRC_LXTAL
  - 使能 RCU_RTC
  - 等待 rtc_register_sync_wait()

  这一步的本质是：

  - RTC 先得有时钟
  - RTC 寄存器先得同步好
  - 否则后面写时间参数没有意义

---------

  ### bsp_rtc_setup()

  作用：真正把时间参数写进 RTC

  它做的事是：

  - 填 rtc_initpara
      - factor_asyn
      - factor_syn
      - year
      - day_of_week
      - month
      - date
      - display_format
      - am_pm
      - hour
      - minute
      - second
  - 然后调用 rtc_init(&rtc_initpara)
  - 成功后写 RTC_BKP0 = BKP_VALUE

  这一步的本质是：

  - 配分频
  - 配日历格式
  - 配具体年月日时分秒
  - 最后写一个 backup 标记，表示“RTC 初始化过了”

---------

  ### bsp_rtc_init()

  作用：总入口，负责把前两步串起来

  它做的事是：

  - 允许访问 Backup 域
  - 调 bsp_rtc_pre_cfg()
  - 读取 RTCSRC_FLAG
  - 调 bsp_rtc_setup()

  按作者原意，下面其实应该是：

  - 如果备份域里没有标记，或者 RTC 时钟源没配置过，才重新设时间
  - 但现在那段判断被注释掉了，所以变成了：
      - 每次初始化都重设 RTC

---------

  ## 2. 如果你要“设定 RTC”，完整步骤是什么

  基于这份代码，RTC 设定流程应该是：

  1. 先打开 Backup 域访问权限
     否则 RTC/备份寄存器不能写。
  2. 配 RTC 时钟源
     由 bsp_rtc_pre_cfg() 完成。
     常见选择：
      - IRC32K
      - LXTAL
  3. 使能 RTC 时钟并等待寄存器同步
     也是 bsp_rtc_pre_cfg() 做的。
  4. 设置 RTC 分频参数
     这两个值决定 RTC 1 秒节拍是否正确：
      - prescaler_a
      - prescaler_s
  5. 填写日历时间参数
     在 rtc_initpara 里设置：
      - 年
      - 月
      - 日
      - 星期
      - 12/24 小时制
      - 时分秒
  6. 调 rtc_init() 把参数写入 RTC
     这是“真正设定时间”的动作。
  7. 写备份标志位到 RTC_BKP0
     用来标记“RTC 已初始化过”。

---------

  ## 3. 你如果自己要改 RTC，最少要改哪些内容

  你要设定 RTC，至少要关心这些参数：

  - prescaler_a
  - prescaler_s
  - rtc_initpara.year
  - rtc_initpara.month
  - rtc_initpara.date
  - rtc_initpara.day_of_week
  - rtc_initpara.display_format
  - rtc_initpara.hour
  - rtc_initpara.minute
  - rtc_initpara.second

  其中最不能乱改的是：

  - prescaler_a
  - prescaler_s

  因为这两个不是“显示时间”，而是RTC 计时基准。
  如果时钟源和分频不匹配，RTC 会走快或走慢。

