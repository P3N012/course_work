#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/rc522_spi.h"
#include "rc522.h"
#include "esp_log.h"


#define PIN_MOSI   23
#define PIN_MISO   19
#define PIN_SCLK   18
#define ENTRY_CS   5
#define RST 4
#define EXIT_CS 17
#define SD_CS 16

#define SPI_HOST   SPI3_HOST   // TODO 0: your choice, SPI2_HOST or SPI3_HOST. Be ready to say why.

static const char *TAG = "ENTRANCE";

// TODO 1: write a function  esp_err_t spi_bus_setup(void)
esp_err_t spi_bus_setup(void) {
    spi_bus_config_t  config = {
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = PIN_MISO,
        .sclk_io_num = PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    return spi_bus_initialize(SPI_HOST, &config, SPI_DMA_CH_AUTO);
}
//   - declare an spi_bus_config_t variable
//   - set .mosi_io_num, .miso_io_num, .sclk_io_num
//   - set .quadwp_io_num and .quadhd_io_num to -1 (we're not using quad SPI)
//   - set .max_transfer_sz (hint: the SD card and RC522 both move small
//     chunks, so 4096 is plenty)
//   - call spi_bus_initialize(SPI_HOST, &cfg, SPI_DMA_CH_AUTO)
//   - return the result
static rc522_spi_config_t entry_config = {
    .host_id = SPI_HOST,
    .bus_config = NULL,
    .dev_config = {
    .spics_io_num = ENTRY_CS,
    .clock_speed_hz = 1000000,
},
    .rst_io_num = RST
};

// TODO 1: a static const array of valid tag strings, e.g.
//         static const char *valid_tags[] = { "AA BB CC DD" };
static const char *valid_tags[] = {"AA BB CC DD"};
//         (I expect the helper to format as "01 02 03 04" with spaces,
//          but check what your own log prints)
// TODO 2: write bool tag_is_valid(const char *uid_str)
// loop over valid_tags with strcmp, return true on a match
bool tag_is_valid(const char *uid_str)
{
    for (size_t i = 0; i < sizeof(valid_tags) / sizeof(valid_tags[0]); i++) {
        if (strcmp(uid_str, valid_tags[i]) == 0) {
            return true;
        }
    }
    return false;
}

static void on_picc_state_changed(void *arg, esp_event_base_t base, int32_t event_id, void *data)
{
    rc522_picc_state_changed_event_t *event = (rc522_picc_state_changed_event_t *)data;
    rc522_picc_t *picc = event->picc;
    if (picc->state != RC522_PICC_STATE_ACTIVE) {
        return;
    }

    // TODO 3: in on_picc_state_changed, after the ACTIVE check,
    //REPLACE the rc522_picc_print(picc) line with:
    char uid_str[RC522_PICC_UID_STR_BUFFER_SIZE_MAX];
    //- call rc522_picc_uid_to_str(&picc->uid, uid_str, sizeof(uid_str))
    rc522_picc_uid_to_str(&picc->uid, uid_str, sizeof(uid_str));
    // and check that it returned ESP_OK
    if (rc522_picc_uid_to_str(&picc->uid, uid_str, sizeof(uid_str)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to convert UID to string");
        return;
    }
    ESP_LOGI(TAG, "Tag scanned: %s -> %s", uid_str,
        tag_is_valid(uid_str) ? "VALID" : "DENIED");
}

static rc522_driver_handle_t entry_driver;
static rc522_handle_t entry_scanner;
// TODO: write this function
static void entry_reader_start(void)
{
    ESP_ERROR_CHECK(rc522_spi_create(&entry_config, &entry_driver));
    ESP_ERROR_CHECK(rc522_driver_install(entry_driver));
    // 3. build an rc522_config_t with .driver = entry_driver,
    //    then rc522_create(&that_config, &entry_scanner)
    rc522_config_t scanner_config = {
        .driver = entry_driver,
        
    };
    ESP_ERROR_CHECK(rc522_create(&scanner_config, &entry_scanner));
    ESP_ERROR_CHECK(rc522_register_events(entry_scanner, RC522_EVENT_PICC_STATE_CHANGED, on_picc_state_changed, NULL));
    ESP_ERROR_CHECK(rc522_start(entry_scanner));
    //
    // Wrap each call in ESP_ERROR_CHECK(...) for now.
};

 void deselect_other_devices(void){
//   for GPIO 17 (exit reader CS) and GPIO 16 (SD card CS):
//     - gpio_reset_pin(pin)
//     - gpio_set_direction(pin, GPIO_MODE_OUTPUT)
//     - gpio_set_level(pin, 1)
    gpio_reset_pin(EXIT_CS);
    gpio_set_direction(EXIT_CS, GPIO_MODE_OUTPUT);
    gpio_set_level(EXIT_CS, 1);

    gpio_reset_pin(SD_CS);
    gpio_set_direction(SD_CS, GPIO_MODE_OUTPUT);
    gpio_set_level(SD_CS, 1);
}

void app_main(void)
{
    // TODO 2: call spi_bus_setup() and store the return value
    esp_err_t value = spi_bus_setup();
    // TODO 3: if it returned ESP_OK, ESP_LOGI "SPI bus ready"
    //         otherwise ESP_LOGE and print the error name
    //         (hint: esp_err_to_name())
    if (value == ESP_OK) {
        ESP_LOGI(TAG, "SPI bus ready");
        esp_log_level_set("rc522", ESP_LOG_NONE);
        deselect_other_devices();
        entry_reader_start();
    } else {
        ESP_LOGE(TAG, "Failed to setup SPI bus: %s", esp_err_to_name(value));
    }
}