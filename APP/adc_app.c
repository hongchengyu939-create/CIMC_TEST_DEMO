#include "mcu_cmic_gd32f470vet6.h"

extern uint16_t adc_value[2];
extern uint16_t convertarr[CONVERT_NUM];
extern uint16_t voltage_mv;
void adc_task(void)
{
    convertarr[0] = adc_value[0];

    voltage_mv = (convertarr[0] * 3300) / 4096;

//    // 3. 【结果输出】打印应用层的数据
//    my_printf(DEBUG_USART, "ADC Raw: %4d | Voltage: %d.%03d V\r\n",
//              convertarr[0], // 打印车间里的原始数据
//              voltage_mv / 1000,
//              voltage_mv % 1000);
}
