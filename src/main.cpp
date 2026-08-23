#include <Arduino.h>
#include <memory.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_lcd_panel_ili9342.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_i2c.hpp>
#include <m5core2_power.hpp>
#include <ft6336.hpp>
#include <lvgl.h>
#include <SD.h>
#include <lvgl_sd_fs.hpp>
namespace arduino {}
using namespace arduino;


m5core2_power power(esp_i2c<1,21,22>::instance);
ft6336<320,280> touch(esp_i2c<1,21,22>::instance);

static uint8_t lcd_transfer_buffer1[320*24*2];
static uint8_t lcd_transfer_buffer2[320*24*2];
static lv_display_t* disp_drv;
static lv_indev_t * inp_dev;
static bool lvgl_init = false;
lv_obj_t * ui_Screen;
void lvgl_port_tp_read(lv_indev_t * indev, lv_indev_data_t * data)
{
    uint16_t x,y;
    if(touch.xy(&x,&y)) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PR;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}



static esp_lcd_panel_handle_t lcd_handle = nullptr;
static volatile int lcd_flush_ready_count = 0;
static bool lcd_flush_ready(esp_lcd_panel_io_handle_t panel_io,
                            esp_lcd_panel_io_event_data_t *edata,
                            void *user_ctx)
{
    if(lvgl_init) {
        lv_disp_flush_ready(disp_drv);
        ++lcd_flush_ready_count;
    }
    return true;
}

void lvgl_port_disp_flush(lv_display_t * disp, const lv_area_t * area, uint8_t * px_buf)
{
    if(lvgl_init) {
        Serial.printf("FLUSH (%d,%d)-(%d,%d)\n",area->x1,area->y1,area->x2,area->y2);
        esp_lcd_panel_draw_bitmap(lcd_handle,area->x1,area->y1,area->x2+1,area->y2+1,px_buf);
    }
}

// initialize the screen using the esp panel API
static void lcd_panel_init()
{
    spi_bus_config_t buscfg;
    memset(&buscfg, 0, sizeof(buscfg));
    buscfg.sclk_io_num = 18;
    buscfg.mosi_io_num = 23;
    buscfg.miso_io_num = -1;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = sizeof(lcd_transfer_buffer1) + 8;

    // Initialize the SPI bus on VSPI (SPI3)
    spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO);

    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config;
    memset(&io_config, 0, sizeof(io_config));
    io_config.dc_gpio_num = 15;
    io_config.cs_gpio_num = 5;
    io_config.pclk_hz = 40 * 1000 * 1000;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    io_config.spi_mode = 0;
    io_config.trans_queue_depth = 10;
    io_config.on_color_trans_done = lcd_flush_ready;
    // Attach the LCD to the SPI bus
    esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI3_HOST, &io_config, &io_handle);

    lcd_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config;
    memset(&panel_config, 0, sizeof(panel_config));
    panel_config.reset_gpio_num = -1;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    panel_config.rgb_endian = LCD_RGB_ENDIAN_BGR;
#else
    panel_config.color_space = ESP_LCD_COLOR_SPACE_BGR;
#endif
    panel_config.bits_per_pixel = 16;

    // Initialize the LCD configuration
    if (ESP_OK != esp_lcd_new_panel_ili9342(io_handle, &panel_config, &lcd_handle))
    {
        printf("Error initializing LCD panel.\n");
        while (1)
            ;
    }

    // Reset the display
    esp_lcd_panel_reset(lcd_handle);

    // Initialize LCD panel
    esp_lcd_panel_init(lcd_handle);
    //  Swap x and y axis (Different LCD screens may need different options)
    esp_lcd_panel_swap_xy(lcd_handle, false);
    esp_lcd_panel_set_gap(lcd_handle, 0, 0);
    esp_lcd_panel_mirror(lcd_handle, false, false);
    esp_lcd_panel_invert_color(lcd_handle, true);
    // Turn on the screen
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    esp_lcd_panel_disp_on_off(lcd_handle, true);
#else
    esp_lcd_panel_disp_off(lcd_handle, true);
#endif
}
void lvgl_print(lv_log_level_t level, const char * buf) {
    Serial.println(buf);
}
void setup()
{
    Serial.begin(115200);
    // setup
    power.initialize();
    lcd_panel_init();
    touch.initialize();
    for (auto gpio : (const uint8_t[]){18, 19, 23}) {
        *(volatile uint32_t*)(GPIO_PIN_MUX_REG[gpio]) |= FUN_DRV_M;
        gpio_pulldown_dis((gpio_num_t)gpio);
        gpio_pullup_en((gpio_num_t)gpio);
    }
    
    /*Serial.println("Initializing SD card");
    if(!SD.begin(4)) {
        Serial.println("SD card initialization failure");
        return;
    }*/
    
    Serial.println("Filesystem mounted");
    
    /* Initialize LVGL core */
    lv_init();
    lv_log_register_print_cb(lvgl_print);
    /* Initialize LVGL buffers */
     disp_drv = lv_display_create(320, 240);
    /* Change the following line to your display resolution */
     lv_display_set_buffers(disp_drv, lcd_transfer_buffer1, lcd_transfer_buffer2,sizeof(lcd_transfer_buffer1), LV_DISPLAY_RENDER_MODE_PARTIAL);
    /* Initialize the input device */
    inp_dev = lv_indev_create(); 
    lv_indev_set_type(inp_dev,LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(inp_dev,lvgl_port_tp_read);
    
    ui_Screen = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_Screen, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_bg_color(ui_Screen, lv_color_hex(0xC778FF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Screen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    // add controls here

}
void loop()
{
    static int ofrc = 0;
    if(ofrc!=lcd_flush_ready_count) {
        Serial.printf("Flush ready count: %d\n",lcd_flush_ready_count);
        ofrc = lcd_flush_ready_count;
    }
    static uint64_t update_ts = 0;
    if(pdTICKS_TO_MS(xTaskGetTickCount())>update_ts+13) {
        update_ts = pdTICKS_TO_MS(xTaskGetTickCount());
        touch.update();
        uint16_t x,y;
        if(touch.xy(&x,&y)) {
            printf("(%d, %d)\n",x,y);
        }
    }
    lv_timer_handler();
}