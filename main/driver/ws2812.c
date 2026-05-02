#include "ws2812.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"

static const char *TAG = "ws2812";
static rmt_channel_handle_t rmt_chan = NULL;
static rmt_encoder_handle_t encoder = NULL;

// WS2812 timing: T0H=0.4us, T0L=0.85us, T1H=0.8us, T1L=0.45us, RESET>280us
// At 10MHz resolution (100ns tick):
//   T0H = 4 ticks, T0L = 9 ticks, T1H = 8 ticks, T1L = 5 ticks

int ws2812_init(void) {
    rmt_tx_channel_config_t chan_cfg = {
        .gpio_num = WS2812_GPIO,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,  // 10MHz
        .mem_block_symbols = 64,
        .trans_queue_depth = 1,
    };
    esp_err_t ret = rmt_new_tx_channel(&chan_cfg, &rmt_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "RMT channel init failed: %s", esp_err_to_name(ret));
        return -1;
    }

    rmt_bytes_encoder_config_t enc_cfg = {
        .bit0 = {
            .level0 = 1, .duration0 = 4,   // T0H 0.4us
            .level1 = 0, .duration1 = 9,   // T0L 0.85us (approx)
        },
        .bit1 = {
            .level0 = 1, .duration0 = 8,   // T1H 0.8us
            .level1 = 0, .duration1 = 5,   // T1L 0.45us (approx)
        },
        .flags.msb_first = 1,
    };
    ret = rmt_new_bytes_encoder(&enc_cfg, &encoder);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Encoder init failed: %s", esp_err_to_name(ret));
        return -1;
    }

    rmt_enable(rmt_chan);
    ws2812_off();
    ESP_LOGI(TAG, "WS2812 initialized on GPIO%d", WS2812_GPIO);
    return 0;
}

void ws2812_set_color(uint8_t r, uint8_t g, uint8_t b) {
    if (!rmt_chan || !encoder) return;
    // WS2812 expects GRB byte order
    uint8_t grb[3] = { g, r, b };
    rmt_transmit_config_t tx_cfg = { .loop_count = 0 };
    rmt_transmit(rmt_chan, encoder, grb, 3, &tx_cfg);
    rmt_tx_wait_all_done(rmt_chan, -1);
}

void ws2812_off(void) {
    ws2812_set_color(0, 0, 0);
}
