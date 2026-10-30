#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_err.h"
#include "nvs_flash.h"

// 独立的 Wi-Fi 和 MQTT 模块
#include "wifi_app.h"
#include "MQTT_app.h"


// ==================== GPIO 配置 ====================

#define FAN_GPIO GPIO_NUM_15
#define IIC_SDA_GPIO GPIO_NUM_26
#define IIC_SCL_GPIO GPIO_NUM_27
#define SHT20_CRC_POLY 0x31


// ==================== 全局变量 ====================

QueueHandle_t temperature_queue = NULL;

static i2c_master_bus_handle_t i2c_bus_handle = NULL;
static i2c_master_dev_handle_t sht20_handle = NULL;

TaskHandle_t sensor_task_handle = NULL;

    
typedef struct
{
    float temperature;
    float humidity;
} SensorData_t;

static QueueHandle_t mqtt_queue = NULL;

// ==================== 函数声明 ====================

uint8_t SHT20_CalculateCRC(
    const uint8_t *data,
    size_t length
);


// ==================== I2C 初始化 ====================

esp_err_t I2C_Init(void)
{
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = IIC_SDA_GPIO,
        .scl_io_num = IIC_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true
    };

    return i2c_new_master_bus(
        &bus_config,
        &i2c_bus_handle
    );
}


// ==================== SHT20 初始化 ====================

esp_err_t SHT20_Init(void)
{
    esp_err_t err = i2c_master_probe(
        i2c_bus_handle,
        0x40,
        1000
    );

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


// ==================== 温度测量命令 ====================

esp_err_t SHT20_StartTemperature(void)
{
    uint8_t cmd = 0xF3;

    return i2c_master_transmit(
        sht20_handle,
        &cmd,
        1,
        1000
    );
}


// ==================== 读取原始数据 ====================

esp_err_t SHT20_ReadRawData(uint8_t *data)
{
    return i2c_master_receive(
        sht20_handle,
        data,
        3,
        1000
    );
}


// ==================== 读取温度 ====================

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

    uint8_t calculated_crc =
        SHT20_CalculateCRC(data, 2);

    if (calculated_crc != data[2])
    {
        printf("CRC CHECK FAILED\n");
        return ESP_ERR_INVALID_CRC;
    }

    uint16_t raw_temperature =
        ((uint16_t)data[0] << 8) | data[1];

    raw_temperature &= 0xFFFC;

    *temperature =
        -46.85f +
        175.72f * (raw_temperature / 65536.0f);

    return ESP_OK;
}


// ==================== 湿度测量命令 ====================

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


// ==================== 读取湿度 ====================

esp_err_t SHT20_ReadHumidity(float *humidity)
{
    uint8_t data[3];

    esp_err_t err = SHT20_StartHumidity();

    if (err != ESP_OK)
    {
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(40));

    err = SHT20_ReadRawData(data);

    if (err != ESP_OK)
    {
        return err;
    }

    uint8_t calculated_crc =
        SHT20_CalculateCRC(data, 2);

    if (calculated_crc != data[2])
    {
        printf("Humidity CRC CHECK FAILED\n");
        return ESP_ERR_INVALID_CRC;
    }

    uint16_t raw_humidity =
        ((uint16_t)data[0] << 8) | data[1];

    raw_humidity &= 0xFFFC;

    *humidity =
        -6.0f +
        125.0f * (raw_humidity / 65536.0f);

    return ESP_OK;
}


// ==================== CRC8 校验 ====================

uint8_t SHT20_CalculateCRC(
    const uint8_t *data,
    size_t length
)
{
    uint8_t crc = 0x00;

    for (size_t i = 0; i < length; i++)
    {
        crc ^= data[i];

        for (int bit = 0; bit < 8; bit++)
        {
            if (crc & 0x80)
            {
                crc = (uint8_t)(
                    (crc << 1) ^ SHT20_CRC_POLY
                );
            }
            else
            {
                crc = (uint8_t)(crc << 1);
            }
        }
    }

    return crc;
}


// ==================== 风扇任务 ====================

void FAN_TASK(void *parameter)
{
    gpio_set_direction(
        FAN_GPIO,
        GPIO_MODE_OUTPUT
    );

    float received_temperature = 0.0f;
    int fan_state = 0;

    while (1)
    {
        if (xQueueReceive(
                temperature_queue,
                &received_temperature,
                pdMS_TO_TICKS(3000)
            ) == pdTRUE)
        {
            if (received_temperature >= 30.0f)
            {
                gpio_set_level(FAN_GPIO, 1);
                fan_state = 1;

                printf("FAN ON\n");
            }
            else if (
                received_temperature > 28.0f &&
                received_temperature < 30.0f
            )
            {
                gpio_set_level(
                    FAN_GPIO,
                    fan_state
                );
            }
            else
            {
                gpio_set_level(FAN_GPIO, 0);
                fan_state = 0;

                printf("FAN OFF\n");
            }

            printf(
                "Received Temperature: %.1f\n",
                received_temperature
            );
        }
        else
        {
            fan_state = 0;

            printf("sensor timeout\n");

            gpio_set_level(FAN_GPIO, 0);
        }
    }
}


// ==================== 传感器任务 ====================

void SENSOR_TASK(void *parameter)
{
    float temperature;
    float humidity;

    BaseType_t result;

    int *sensor_id = (int *)parameter;

    while (1)
    {
        esp_err_t err =
            SHT20_ReadTemperature(&temperature);

        esp_err_t err_h =
            SHT20_ReadHumidity(&humidity);

        if (err == ESP_OK && err_h == ESP_OK)
        {
            printf(
                "Temperature: %.2f C\n",
                temperature
            );

            printf(
                "Humidity: %.2f %%\n",
                humidity
            );

            result = xQueueSend(
                temperature_queue,
                &temperature,
                0
            );

            SensorData_t data = {
                .temperature = temperature,
                .humidity = humidity
            };
            
            if(mqtt_queue != NULL)
            {
                BaseType_t mqtt_result = xQueueOverwrite(
                    mqtt_queue,
                    &data
                );
            }

            if (result == pdPASS)
            {
                printf(
                    "send successful: %.2f C\n",
                    temperature
                );
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
                printf(
                    "Temperature read failed: %s\n",
                    esp_err_to_name(err)
                );
            }

            if (err_h != ESP_OK)
            {
                printf(
                    "Humidity read failed: %s\n",
                    esp_err_to_name(err_h)
                );
            }
        }

        printf(
            "SENSOR RUNNING = %d\n",
            *sensor_id
        );

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
}
// ==================== 网络任务 ====================

void NETWORK_TASK(void *pvParameters)
{
   while(1)
   {
    bool connected = wifi_wait_connected(10000);


    if (connected)
    {
        printf("WiFi Ready\n");
        break;
    }
   }

   mqtt_app_start();
   vTaskDelete(NULL);
}

// ==================== MQTT任务 ====================

void MQTT_TASK(void *pvParameters)
{
    SensorData_t received_data;

    const char *topic = "esp32/monitor/sensor";

    while (1)
    {
        // 1. 等待并接收传感器数据
        BaseType_t result = xQueueReceive(
            mqtt_queue,
            &received_data,
            portMAX_DELAY
        );

        if (result == pdPASS)
        {
            // 2. 打印接收到的温湿度
            printf(
                "MQTT received: %.2f C, %.2f %%\n",
                received_data.temperature,
                received_data.humidity
            );

            // 3. 创建 JSON 字符串缓冲区
            char payload[128];

            // 4. 将温湿度转换成 JSON
            int len = snprintf(
                payload,
                sizeof(payload),
                "{\"temperature\":%.2f,\"humidity\":%.2f}",
                received_data.temperature,
                received_data.humidity
            );

            // 5. 检查格式化是否成功
            if (len < 0 || (size_t)len >= sizeof(payload))
            {
                printf("JSON format failed\n");
                continue;
            }

            // 6. 发布 MQTT 消息
            bool success = mqtt_app_publish(
                topic,
                payload
            );

            // 7. 检查发布调用结果
            if (success)
            {
                printf("MQTT publish request successful\n");
            }
            else
            {
                printf("MQTT publish failed\n");
            }
        }
    }
}

// ==================== 主函数 ====================

void app_main(void)
{
    static int sensor_id = 1;

    printf("ESP32 IoT Monitor Starting\n");

    // 1. 初始化 NVS
    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(ret);

    // 2. 初始化 Wi-Fi
    wifi_init_sta();

    // 注意：删除原来这里的 mqtt_app_start()
    // MQTT 由 NETWORK_TASK 在 Wi-Fi 获得 IP 后启动

    // 3. 初始化 I2C
    if (I2C_Init() != ESP_OK)
    {
        printf("I2C INIT failed\n");
        return;
    }

    // 4. 初始化 SHT20
    if (SHT20_Init() != ESP_OK)
    {
        printf("SHT20 NOT FOUND\n");
        return;
    }

    printf("SHT20 FOUND\n");

    // 5. 创建温度队列：供 FAN_TASK 使用
    temperature_queue = xQueueCreate(
        10,
        sizeof(float)
    );

    if (temperature_queue == NULL)
    {
        printf("Temperature Queue creation failed\n");
        return;
    }

    // 6. 创建 MQTT 队列：保存最新一组温湿度
    mqtt_queue = xQueueCreate(
        1,
        sizeof(SensorData_t)
    );

    if (mqtt_queue == NULL)
    {
        printf("MQTT Queue Create Failed\n");
        // 后续不要启动依赖该队列的 MQTT 发布任务
    }

    // 7. 创建 MQTT 发布任务
    if (mqtt_queue != NULL)
    {
        BaseType_t mqtt_task_result = xTaskCreate(
            MQTT_TASK,
            "MQTT_TASK",
            4096,
            NULL,
            4,
            NULL
        );

    if (mqtt_task_result != pdPASS)
    {
        printf("MQTT_TASK creation failed\n");
    }
}

    // 7. 创建 FAN 任务
    BaseType_t fan_result = xTaskCreate(
        FAN_TASK,
        "FAN_TASK",
        2048,
        NULL,
        3,
        NULL
    );

    if (fan_result != pdPASS)
    {
        printf("FAN_TASK creation failed\n");
        return;
    }

    // 8. 创建传感器任务
    BaseType_t sensor_result = xTaskCreate(
        SENSOR_TASK,
        "SENSOR_TASK",
        2048,
        &sensor_id,
        2,
        &sensor_task_handle
    );

    if (sensor_result != pdPASS)
    {
        printf("SENSOR_TASK creation failed\n");
        return;
    }

    // 9. 创建网络任务
    BaseType_t network_result = xTaskCreate(
        NETWORK_TASK,
        "NETWORK_TASK",
        4096,
        NULL,
        5,
        NULL
    );

    if (network_result != pdPASS)
    {
        printf("NETWORK_TASK creation failed\n");
    }
}