#include <stdio.h>
#include "esp_check.h"
#include "bsp/esp-bsp.h"
#include "camera.h"

//Routine to set a pixel in a framebuffer. Not used by the below code.
//You can use this for debugging or other purposes. It sets one pixel in the
//LCD framebuffer to a certain rgb value. R, G and B need to be [0-255],
//x and y must be between 0 and 239, inclusive.
void fb_set_pixel(lcd_fb_t *fb, int x, int y, int r, int g, int b) {
	if (x<0 || x>=fb->width) return;
	if (y<0 || y>=fb->height) return;
	//r8g8b8 to r5g6b5
	uint16_t c=((r>>3)<<11)|((g>>2)<<5)|(b>>3);
	c=(c<<8)|(c>>8); //swap bytes
	uint16_t *p=(uint16_t*)fb->buf;
	p[x+y*fb->width]=c; //set pixel
}

/*
Trivial cam-lcd example. Initializes everything and grabs frames from the camera and
sends it to the LCD.
*/

void app_main(void) {
	// The camera and other parts need I2C to work. We initialize it here.
	ESP_ERROR_CHECK(bsp_i2c_init());

	// The ESP32-P4-EYE is shipped with 1.54inch ST7789 display controller.
	// It features 16-bit colors and 240x240 resolution. We initialize it here.

	// The configuration. It only needs a SPI max transfer size. Frames are sent in chunks of 10
	// lines, so the SPI driver only needs small internal buffers to copy them into.
	bsp_display_config_t disp_cfg={
		.max_transfer_sz = BSP_LCD_H_RES * 10 * sizeof(uint16_t)
	};
	esp_lcd_panel_handle_t panel;
	esp_lcd_panel_io_handle_t panel_io;
	// Create the display driver and initialize it.
	ESP_ERROR_CHECK(bsp_display_new(&disp_cfg, &panel, &panel_io));
	// Turn it on and set full backlight.
	// ToDo: how to turn on backlight?
	ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

	// Start the camera. Frames we get from it are already scaled down to 240x240
	// 16-bit RGB565 pixels, ready to send to the LCD. (See camera.c for the details.)
	ESP_ERROR_CHECK(camera_init());

	while(1) {
		// Grab a frame from the camera...
		lcd_fb_t *fb = camera_get_frame();
		// ...and draw it on the LCD.
		ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, 1, 1, fb->width, fb->height, fb->buf));
	}
}
