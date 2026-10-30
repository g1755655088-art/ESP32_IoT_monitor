#include <stdio.h>
#include <stdint.h>
#include "wifi_app.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "sdkconfig.h"

// Wi-Fi 事件组
static EventGroupHandle_t s_wifi_event_group;

#define WIFI_CONNECTED_BIT BIT0


// Wi-Fi 和 IP 事件处理函数
static void event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data
)
{
    // Wi-Fi STA 启动
    if (event_base == WIFI_EVENT &&
        event_id == WIFI_EVENT_STA_START)
    {
        printf("WIFI START EVENT\n");

        ESP_ERROR_CHECK(esp_wifi_connect());
    }

    // Wi-Fi 断开连接
    else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        wifi_event_sta_disconnected_t *event =
            (wifi_event_sta_disconnected_t *)event_data;

        printf("WIFI DISCONNECTED, reason = %d\n",
               event->reason);

        // 清除 Wi-Fi 连接标志
        xEventGroupClearBits(
            s_wifi_event_group,
            WIFI_CONNECTED_BIT
        );

        // 尝试重新连接 Wi-Fi
        ESP_ERROR_CHECK(esp_wifi_connect());
    }

    // Wi-Fi 成功获取 IP
    else if (event_base == IP_EVENT &&
             event_id == IP_EVENT_STA_GOT_IP)
    {
        printf("WIFI GOT IP\n");

        // 设置连接成功标志
        xEventGroupSetBits(
            s_wifi_event_group,
            WIFI_CONNECTED_BIT
        );
    }
}


// Wi-Fi 初始化函数
void wifi_init_sta(void)
{
    // 1. 创建事件组
    s_wifi_event_group = xEventGroupCreate();
    configASSERT(s_wifi_event_group != NULL);

    // 2. 初始化网络基础设施
    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 3. 创建 Wi-Fi STA 网络接口
    esp_netif_create_default_wifi_sta();

    // 4. 初始化 Wi-Fi 驱动
    wifi_init_config_t wifi_cfg =
        WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));

    // 5. 声明事件注册句柄
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;

    // 6. 注册 Wi-Fi 事件
    ESP_ERROR_CHECK(
        esp_event_handler_instance_register(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            &event_handler,
            NULL,
            &instance_any_id
        )
    );

    // 7. 注册获取 IP 事件
    ESP_ERROR_CHECK(
        esp_event_handler_instance_register(
            IP_EVENT,
            IP_EVENT_STA_GOT_IP,
            &event_handler,
            NULL,
            &instance_got_ip
        )
    );

    // 8. 配置 Wi-Fi 名称和密码
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_ESP_WIFI_SSID,
            .password = CONFIG_ESP_WIFI_PASSWORD,
        },
    };

    // 9. 设置 Wi-Fi Station 模式
    ESP_ERROR_CHECK(
        esp_wifi_set_mode(WIFI_MODE_STA)
    );

    // 10. 应用 Wi-Fi 配置
    ESP_ERROR_CHECK(
        esp_wifi_set_config(
            WIFI_IF_STA,
            &wifi_config
        )
    );

    // 11. 启动 Wi-Fi
    ESP_ERROR_CHECK(esp_wifi_start());
}

bool wifi_wait_connected(uint32_t timeout_ms)
{
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group, 
        WIFI_CONNECTED_BIT,
        pdFALSE, 
        pdFALSE, 
        pdMS_TO_TICKS(timeout_ms)
    );
    
    if (bits & WIFI_CONNECTED_BIT)
    {
    return true;
    }
    else
    {
    return false;
    }
}

