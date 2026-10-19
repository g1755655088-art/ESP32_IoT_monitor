#include <stdio.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/i2c_master.h"
#include "esp_err.h"

#define FAN_GPIO GPIO_NUM_23
#define IIC_SDA_GPIO GPIO_NUM_26
#define IIC_SCL_GPIO GPIO_NUM_27
#define SHT20_CRC_POLY 0x31
 
QueueHandle_t temperature_queue = NULL;
static i2c_master_bus_handle_t i2c_bus_handle = NULL;
static i2c_master_dev_handle_t sht20_handle = NULL;
uint8_t SHT20_CalculateCRC(const uint8_t *data, size_t length);

esp_err_t I2C_Init(void)
{
    i2c_master_bus_config_t bus_config =
    {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = GPIO_NUM_26,
        .scl_io_num = GPIO_NUM_27,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true
    };
    return i2c_new_master_bus(&bus_config, &i2c_bus_handle);
}

esp_err_t SHT20_Init(void)
{
    esp_err_t err = i2c_master_probe(i2c_bus_handle, 0x40, 1000);
    if (err != ESP_OK)
    {
        return err;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x40,
        .scl_speed_hz = 100000,
    };

    return i2c_master_bus_add_device(
        i2c_bus_handle,
        &dev_cfg,
        &sht20_handle
    );
}

esp_err_t SHT20_StartTemperature(void)
{
    uint8_t cmd = 0xF3;

    esp_err_t err = i2c_master_transmit(
        sht20_handle,
        &cmd,
        1,
        1000
);
    return err;
}

esp_err_t SHT20_ReadRawData(uint8_t *data)
{
    return i2c_master_receive(
        sht20_handle,
        data,
        3,
        1000
    );
}

esp_err_t SHT20_ReadTemperature(float *temperature)
{
    uint8_t data[3];

    esp_err_t err = SHT20_StartTemperature();
    if (err != ESP_OK)
    {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(100));

    err = SHT20_ReadRawData(data);
    if (err != ESP_OK)
    {
        return err;
    }

    uint8_t calculated_crc = SHT20_CalculateCRC(data, 2);

    if (calculated_crc != data[2])
    {
        printf("CRC CHECK FAILED\n");
        return ESP_ERR_INVALID_CRC;
    }

    uint16_t raw_temperature =
        ((uint16_t)data[0] << 8) | data[1];

    raw_temperature &= 0xFFFC;

    *temperature =
        -46.85f + 175.72f * (raw_temperature / 65536.0f);

    return ESP_OK;
}

esp_err_t SHT20_StartHumidity(void)
{
    uint8_t cmd = 0xF5;

    return i2c_master_transmit(
        sht20_handle,
        &cmd,
        1,
        1000
    );
}

esp_err_t SHT20_ReadHumidity(float *humidity)
{
    uint8_t data[3];

    // 1. 发送湿度测量命令 0xF5
    esp_err_t err = SHT20_StartHumidity();

    if (err != ESP_OK)
    {
        return err;
    }

    // 2. 等待 SHT20 完成湿度测量
    vTaskDelay(pdMS_TO_TICKS(40));

    // 3. 读取三个字节：MSB、LSB、CRC
    err = SHT20_ReadRawData(data);

    if (err != ESP_OK)
    {
        return err;
    }

    // 4. 计算 CRC-8
    uint8_t calculated_crc = SHT20_CalculateCRC(data, 2);

    // 5. 比较计算结果与传感器返回的 CRC
    if (calculated_crc != data[2])
    {
        printf("Humidity CRC CHECK FAILED\n");
        return ESP_ERR_INVALID_CRC;
    }

    // 6. 合并 MSB 和 LSB
    uint16_t raw_humidity =
        ((uint16_t)data[0] << 8) | data[1];

    // 7. 清除最低两个状态位
    raw_humidity &= 0xFFFC;

    // 8. 根据 SHT20 Datasheet 转换成相对湿度
    *humidity =
        -6.0f + 125.0f * (raw_humidity / 65536.0f);

    return ESP_OK;
}

uint8_t SHT20_CalculateCRC(const uint8_t *data, size_t length)
{
    uint8_t crc = 0x00;

    for (size_t i = 0; i < length; i++)
    {
        crc ^= data[i];

        for (int bit = 0; bit < 8; bit++)
        {
            if (crc & 0x80)
            {
                crc = (uint8_t)((crc << 1) ^ SHT20_CRC_POLY);
            }
            else
            {
                crc = (uint8_t)(crc << 1);
            }
        }
    }

    return crc;
}

void FAN_TASK(void *parameter)
{
    gpio_set_direction(FAN_GPIO, GPIO_MODE_OUTPUT);
    float received_temperature = 0.0f;
    int fan_state = 0;

    while(1)
    {
        if (xQueueReceive(temperature_queue, &received_temperature, pdMS_TO_TICKS(3000)) == pdTRUE)
        {
            if (received_temperature >= 30.0f)
            {
                gpio_set_level(FAN_GPIO, 1);
                fan_state = 1;
                printf("FAN ON\n");
            }
            else if (28 < received_temperature && received_temperature < 30)
            {
                gpio_set_level(FAN_GPIO, fan_state);

            }
            else 
            {
                gpio_set_level(FAN_GPIO, 0);
                fan_state = 0;
                printf("FAN OFF\n");
            }
        
            printf("Received Temperature:%.1f\n", received_temperature);
        }
        else{
            fan_state = 0;
            printf("sensor timeout\n");
            gpio_set_level(FAN_GPIO, 0);
        }
    /*  gpio_set_level(FAN_GPIO, 1);
        printf("FAN ON\n");
        vTaskDelay(pdMS_TO_TICKS(10000));
        gpio_set_level(FAN_GPIO, 0);
        printf("FAN OFF\n");
        vTaskDelay(pdMS_TO_TICKS(10000));*/
    }
}

void SENSOR_TASK(void *parameter)
{
    float temperature;
    float humidity;
    BaseType_t result;
    int *sensor_id = (int *)parameter;

    while (1)
    {
        esp_err_t err = SHT20_ReadTemperature(&temperature);
        esp_err_t err_h = SHT20_ReadHumidity(&humidity);

        // 两项都读取成功
        if (err == ESP_OK && err_h == ESP_OK)
        {
            printf("Temperature: %.2f C\n", temperature);
            printf("Humidity: %.2f %%\n", humidity);

            result = xQueueSend(temperature_queue, &temperature, 0);

            if (result == pdPASS)
            {
                printf("send successful: %.2f C\n", temperature);
            }
            else
            {
                printf("Queue send failed\n");
            }
        }
        else
        {
            if (err != ESP_OK)
            {
                printf("Temperature read failed: %s\n",
                       esp_err_to_name(err));
            }

            if (err_h != ESP_OK)
            {
                printf("Humidity read failed: %s\n",
                       esp_err_to_name(err_h));
            }
        }

        printf("SENSOR RUNNING = %d\n", *sensor_id);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

TaskHandle_t sensor_task_handle = NULL;

void app_main(void)
{
    static int sensor_id = 1;

    printf("DAY2 NEW PROGRAM START\n");

    if (I2C_Init() != ESP_OK)
    {
        printf("2C INIT is wrong\n");
        return;
    }

    if (SHT20_Init() != ESP_OK)
    {
        printf("SHT20 NOT FOUND\n");
        return;
    }
    else
    {
        printf("SHT20 FOUND\n");
    }

    temperature_queue = xQueueCreate(10, sizeof(float));
    if (temperature_queue == NULL)
    {
        printf("Queue creation failed\n");
        return;
    }

    xTaskCreate(FAN_TASK, "FAN_TASK", 2048, NULL, 3,  NULL);
    xTaskCreate(SENSOR_TASK, "Sensor_TASK", 2048, &sensor_id, 2, &sensor_task_handle);
    
    /*vTaskDelay(pdMS_TO_TICKS(5000));

    printf("SENSOR SUSPENDED\n");
    vTaskSuspend(sensor_task_handle);

    vTaskDelay(pdMS_TO_TICKS(10000));

    printf("SENSOR RESUMED\n");
    vTaskResume(sensor_task_handle);*/
}




