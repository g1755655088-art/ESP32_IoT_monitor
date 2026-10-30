#include <stdio.h>
#include "mqtt_app.h"
#include "mqtt_client.h"

static bool mqtt_connected = false;
static esp_mqtt_client_handle_t mqtt_client = NULL;

static void mqtt_event_handler(
    void *handler_args,
    esp_event_base_t base,
    int32_t event_id,
    void *event_data
)
{
    // 根据 event_id 处理不同事件
    if (event_id == MQTT_EVENT_CONNECTED)
    {
        mqtt_connected = true;
        printf("MQTT CONNECT\n");
    }
    else if (event_id == MQTT_EVENT_DISCONNECTED)
    {
        mqtt_connected = false;
        printf("MQTT DISCONNECT\n");
    }
}

void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtt://broker.hivemq.com",
    };

     mqtt_client = esp_mqtt_client_init(&mqtt_cfg);

    if (mqtt_client == NULL)
    {
        printf("MQTT INIT FAILED\n");
        return;
    }   

    esp_err_t ret = esp_mqtt_client_register_event(
        mqtt_client,
        ESP_EVENT_ANY_ID,
        mqtt_event_handler,
        NULL
    );

    if (ret != ESP_OK)
    {
        printf("MQTT EVENT REGISTER FAILED\n");
        esp_mqtt_client_destroy(mqtt_client);
        mqtt_client = NULL; //彻底销毁
    return;
    }

    ret = esp_mqtt_client_start(mqtt_client);

    if (ret == ESP_OK)
    {
        printf("MQTT start success\n");
    }
    else
    {
        printf("MQTT start Failed\n");
        esp_mqtt_client_destroy(mqtt_client);
        mqtt_client = NULL;
    }
}

bool mqtt_app_publish(const char *topic, const char *payload)
{
    if (!mqtt_connected || mqtt_client == NULL)
    {
        return false;
    }

    int msg_id = esp_mqtt_client_publish(
        mqtt_client,
        topic,
        payload,
        0,
        1,
        1
    );

    if (msg_id < 0)
    {
        return false;
    }
    else {
        return true;
    }
}

