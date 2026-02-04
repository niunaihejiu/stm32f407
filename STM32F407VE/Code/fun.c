#include "fun.h"
#include "gpio.h"
#include "usart.h"
#include "adc.h"
#include "i2c.h"  
#include <string.h>
#include <stdio.h>

// 校准系数 - 根据万用表测量值0.40V对应ADC 55计算得出
static float g_scale_factor = 9.03f;     // 校准系数

// 蜂鸣器控制函数
void Buzzer_On(void)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET);  // PB0低电平，蜂鸣器响
}

void Buzzer_Off(void)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET);    // PB0高电平，蜂鸣器关闭
}

// LED控制函数
void LED_On(void)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_2, GPIO_PIN_RESET);  // PB2低电平，LED亮
}

void LED_Off(void)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_2, GPIO_PIN_SET);    // PB2高电平，LED灭
}

// MQ2传感器函数
void MQ2_Preheat_Tips(void)
{
    printf("MQ2 preheating... Wait 2-3 mins!\r\n");
    HAL_Delay(1000);
}

// 读取MQ2模拟输出值（10次采样平均）
uint16_t MQ2_Read_AO_Value(void)
{
    uint16_t adc_val = 0;
    uint8_t sample_cnt = 10;
    
    HAL_ADC_Start(&hadc1);  // 启动ADC
    
    for(uint8_t i = 0; i < sample_cnt; i++)
    {
        if(HAL_ADC_PollForConversion(&hadc1, 100) == HAL_OK)
        {
            adc_val += HAL_ADC_GetValue(&hadc1);  // 累加ADC值
        }
        HAL_Delay(10);  // 采样间隔
    }
    
    HAL_ADC_Stop(&hadc1);  // 停止ADC
    return adc_val / sample_cnt;  // 返回平均值
}

// ADC值转换为实际电压（应用校准系数）
float MQ2_Convert_Voltage(uint16_t adc_val)
{
    // 基础电压计算（STM32 ADC参考电压3.3V）
    float voltage = (float)adc_val / 4095.0f * 3.3f;
    
    // 应用校准系数（考虑分压电路）
    voltage = voltage * g_scale_factor;
    
    // 电压范围限制（0-5V）
    if(voltage < 0.0f) voltage = 0.0f;
    if(voltage > 5.0f) voltage = 5.0f;
    
    return voltage;
}

// 读取MQ2数字输出状态
uint8_t MQ2_Read_DO_State(void)
{
    return HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_1);  // 读取PA1引脚状态
}

// 传感器校准函数
void MQ2_Calibrate(float multimeter_voltage, uint16_t adc_value)
{
    // 计算原始电压（未校准）
    float raw_voltage = (float)adc_value / 4095.0f * 3.3f;
    
    // 计算校准系数 = 实际电压 / 原始电压
    g_scale_factor = multimeter_voltage / raw_voltage;
    
    printf("=== CALIBRATION ===\r\n");
    printf("ADC: %d\r\n", adc_value);
    printf("Multimeter: %.3fV\r\n", multimeter_voltage);
    printf("Raw: %.3fV\r\n", raw_voltage);
    printf("Factor: %.2f\r\n", g_scale_factor);
}


#define SHT30_I2C_ADDR    0x44 << 1  // 无ADD接口，默认地址（左移1位，HAL库要求）
#define SHT30_MEAS_CMD    0x2C06     // 高精度测量命令
#define I2C_TIMEOUT_MS    500        // 延长超时时间到500ms


// 读取SHT30温湿度数据（返回0=成功，1=失败）
uint8_t SHT30_Read_Temp_Humi(float *temp, float *humi)
{
    uint8_t tx_buf[2] = {0};
    uint8_t rx_buf[6] = {0};
    uint8_t retry_cnt = 3;          // 最多重试3次
    HAL_StatusTypeDef i2c_status;

    // 初始化温湿度为异常值（便于识别通信失败）
    *temp = -99.9f;
    *humi = -99.9f;

    // 1. 发送测量命令（0x2C06），增加重试
    tx_buf[0] = (SHT30_MEAS_CMD >> 8) & 0xFF;  // 高字节
    tx_buf[1] = SHT30_MEAS_CMD & 0xFF;         // 低字节
    while(retry_cnt--)
    {
        i2c_status = HAL_I2C_Master_Transmit(&hi2c1, SHT30_I2C_ADDR, tx_buf, 2, I2C_TIMEOUT_MS);
        if(i2c_status == HAL_OK) break;
        HAL_Delay(50);  // 重试前延时
    }
    if(i2c_status != HAL_OK)
    {
        printf("SHT30: Send command failed! (Status: %d)\r\n", i2c_status);
        return 1;
    }

    HAL_Delay(20);  // 等待测量完成（高精度模式需≥15ms）

    // 2. 读取6字节数据，增加重试
    retry_cnt = 3;
    while(retry_cnt--)
    {
        i2c_status = HAL_I2C_Master_Receive(&hi2c1, SHT30_I2C_ADDR, rx_buf, 6, I2C_TIMEOUT_MS);
        if(i2c_status == HAL_OK) break;
        HAL_Delay(50);
    }
    if(i2c_status != HAL_OK)
    {
        printf("SHT30: Read data failed! (Status: %d)\r\n", i2c_status);
        return 1;
    }

    // 3. 计算温度和湿度（公式参考SHT30 datasheet）
    uint16_t temp_raw = (rx_buf[0] << 8) | rx_buf[1];
    uint16_t humi_raw = (rx_buf[3] << 8) | rx_buf[4];

    *temp = -45.0f + 175.0f * (float)temp_raw / 65535.0f;  // 温度转换公式
    *humi = 100.0f * (float)humi_raw / 65535.0f;            // 湿度转换公式

    // 4. 范围限制（防止异常值）
    if(*temp < -45.0f || *temp > 125.0f) *temp = -99.9f;
    if(*humi < 0.0f || *humi > 100.0f) *humi = -99.9f;

    return 0;
}

// 系统主函数
void Gas_Detection_System(void)
{
    static uint8_t init_flag = 0;
    uint16_t mq2_adc;
    float mq2_volt;
    uint8_t mq2_do;
    float temp = 0.0f, humi = 0.0f;  // 温湿度变量
    
    // 首次调用初始化
    if(init_flag == 0)
    {
        MQ2_Preheat_Tips();  // MQ2预热提示
        Buzzer_Off();        // 关闭蜂鸣器
        LED_Off();           // 关闭LED
        init_flag = 1;
    }
    
    // 阶段1：LED关闭
    LED_Off();
    mq2_adc = MQ2_Read_AO_Value();
    mq2_volt = MQ2_Convert_Voltage(mq2_adc);
    mq2_do = MQ2_Read_DO_State();
    SHT30_Read_Temp_Humi(&temp, &humi);  // 读取温湿度
    
    // 根据DO状态控制蜂鸣器
    if(mq2_do == 0)  // 检测到气体
    {
        Buzzer_On();
        printf("LED_OFF | GAS! ADC:%4d | Volt:%.2fV | DO:%d | BEEP:ON | Temp:%.1f C | Humi:%.1f%%\r\n", 
               mq2_adc, mq2_volt, mq2_do, temp, humi);
    }
    else  // 正常状态
    {
        Buzzer_Off();
        printf("LED_OFF | NORMAL. ADC:%4d | Volt:%.2fV | DO:%d | BEEP:OFF | Temp:%.1f C | Humi:%.1f%%\r\n", 
               mq2_adc, mq2_volt, mq2_do, temp, humi);
    }
    HAL_Delay(1000);
    
    // 阶段2：LED开启
    LED_On();
    mq2_adc = MQ2_Read_AO_Value();
    mq2_volt = MQ2_Convert_Voltage(mq2_adc);
    mq2_do = MQ2_Read_DO_State();
    SHT30_Read_Temp_Humi(&temp, &humi);  // 读取温湿度
    
    // 根据DO状态控制蜂鸣器
    if(mq2_do == 0)  // 检测到气体
    {
        Buzzer_On();
        printf("LED_ON  | GAS! ADC:%4d | Volt:%.2fV | DO:%d | BEEP:ON | Temp:%.1f C | Humi:%.1f%%\r\n", 
               mq2_adc, mq2_volt, mq2_do, temp, humi);
    }
    else  // 正常状态
    {
        Buzzer_Off();
        printf("LED_ON  | NORMAL. ADC:%4d | Volt:%.2fV | DO:%d | BEEP:OFF | Temp:%.1f C | Humi:%.1f%%\r\n", 
               mq2_adc, mq2_volt, mq2_do, temp, humi);
    }
    HAL_Delay(1000);
}
