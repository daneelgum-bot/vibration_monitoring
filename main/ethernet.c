#include "esp_eth.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"

static const char *TAG = "ETH";
static esp_eth_handle_t s_eth_handle = NULL;

static void eth_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    if (event_id == ETHERNET_EVENT_CONNECTED) {
        ESP_LOGI(TAG, "Ethernet Link Up");
    } else if (event_id == ETHERNET_EVENT_DISCONNECTED) {
        ESP_LOGI(TAG, "Ethernet Link Down");
    }
}

static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
   esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(event->esp_netif, &ip_info);
   ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&ip_info.ip));
}

void ethernet_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *eth_netif = esp_netif_new(&cfg);
    assert(eth_netif);

    // Настройка MAC (пины и тактовый сигнал)
    eth_esp32_emac_config_t esp32_mac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    esp32_mac_config.smi_gpio.mdc_num = 23;    // Ваш MDC
    esp32_mac_config.smi_gpio.mdio_num = 18;   // Ваш MDIO
    esp32_mac_config.interface = EMAC_DATA_INTERFACE_RMII;
    
    // Выбор источника тактового сигнала 50 МГц
    // Вариант 1: ESP32 генерирует 50 МГц на GPIO17 (для LAN8720)
    esp32_mac_config.clock_config.rmii.clock_mode = EMAC_CLK_OUT;
    esp32_mac_config.clock_config.rmii.clock_gpio = 17;
    
    // Вариант 2: если у вас внешний осциллятор на GPIO0 — раскомментируйте:
    // esp32_mac_config.clock_config.rmii.clock_mode = EMAC_CLK_EXT_IN;
    // esp32_mac_config.clock_config.rmii.clock_gpio = 0;

    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&esp32_mac_config, &mac_config);

    // Настройка PHY (LAN8720)
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr = 1;           // Адрес вашего PHY (чаще всего 1, иногда 0)
    phy_config.reset_gpio_num = -1;    // Если нет пина сброса, то -1
    // Встроенная функция для LAN8720
   esp_eth_phy_t *phy = esp_eth_phy_new_lan87xx(&phy_config);

    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    ESP_ERROR_CHECK(esp_eth_driver_install(&eth_config, &s_eth_handle));
    ESP_ERROR_CHECK(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(s_eth_handle)));

    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, NULL));

    ESP_ERROR_CHECK(esp_eth_start(s_eth_handle));
    ESP_LOGI(TAG, "Ethernet initialized");
}