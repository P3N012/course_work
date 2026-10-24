#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "driver/rc522_spi.h"
#include "rc522.h"
#include "esp_log.h"


#define LED_ENTRY_GREEN 32
#define LED_ENTRY_RED   33
#define PIN_MOSI   23
#define PIN_MISO   19
#define PIN_SCLK   18
#define ENTRY_CS   5
#define SERVO_ENTRY_PIN 25
#define SERVO_CLOSED_US  500    // door closed, arm at about 0 degrees
#define SERVO_OPEN_US   1470    // door open, arm at about 90 degrees
#define RST 4
#define EXIT_CS 17
#define SD_CS 16

typedef struct {
    char uid[RC522_PICC_UID_STR_BUFFER_SIZE_MAX];
    bool valid;
}scan_event_t;

static QueueHandle_t scan_queue;

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

void leds_init(void) {
    gpio_reset_pin(LED_ENTRY_GREEN);
    gpio_set_direction(LED_ENTRY_GREEN, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_ENTRY_GREEN, 0);

    gpio_reset_pin(LED_ENTRY_RED);
    gpio_set_direction(LED_ENTRY_RED, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_ENTRY_RED, 0);
}

void servo_init(void) {
    ledc_timer_config_t timer_config = {
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_14_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 50,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_config));

    ledc_channel_config_t channel_config = {
        .gpio_num = SERVO_ENTRY_PIN,
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel_config));
}

void servo_set_pulse_us(uint32_t pulse_us) {
    uint32_t duty = (pulse_us * (1 << 14)) / 20000; // 20ms period for 50Hz
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0));
}

void door_open(void) {
    servo_set_pulse_us(SERVO_OPEN_US); // Open position
}

void door_close(void) {
    servo_set_pulse_us(SERVO_CLOSED_US); // Close position
}

void door_task(void *arg) {
    scan_event_t event;
    while (1) {
        if (xQueueReceive(scan_queue, &event, portMAX_DELAY)) {
            if (event.valid) {
                ESP_LOGI(TAG, "Door opened for tag: %s", event.uid);
                gpio_set_level(LED_ENTRY_GREEN, 1);
                door_open();
                vTaskDelay(pdMS_TO_TICKS(3000));
                door_close();
                gpio_set_level(LED_ENTRY_GREEN, 0);
            } else {
                ESP_LOGW(TAG, "Access denied for tag: %s", event.uid);
                gpio_set_level(LED_ENTRY_RED, 1);
                vTaskDelay(pdMS_TO_TICKS(1000));
                gpio_set_level(LED_ENTRY_RED, 0);
            }
        }
    }
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
    // and check that it returned ESP_OK
    if (rc522_picc_uid_to_str(&picc->uid, uid_str, sizeof(uid_str)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to convert UID to string");
        return;
    }
    ESP_LOGI(TAG, "Tag scanned: %s -> %s", uid_str,
        tag_is_valid(uid_str) ? "VALID" : "DENIED");
    
// TODO 3: in on_picc_state_changed, after you work out VALID/DENIED:
//         build a scan_event_t (copy uid_str into ev.uid, set ev.valid)
    scan_event_t ev;
    strncpy(ev.uid, uid_str, sizeof(ev.uid));
    ev.valid = tag_is_valid(uid_str);
    xQueueSend(scan_queue, &ev, 0);
//         The timeout is 0 on purpose: the callback must never wait.
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
        leds_init();
        servo_init();
        door_close();
        scan_queue = xQueueCreate(5, sizeof(scan_event_t));
        xTaskCreate(door_task, "door_task", 4096, NULL, 5, NULL);
        entry_reader_start();
    } else {
        ESP_LOGE(TAG, "Failed to setup SPI bus: %s", esp_err_to_name(value));
    }
}