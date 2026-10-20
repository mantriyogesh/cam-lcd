#pragma once
#include <stdint.h>
#include "esp_err.h"

// Header for camera related code.
// Note you don't need to change anything here to make the code work.

// Simple framebuffer description, so we have something to pass around.
// Pixels are 16-bit RGB565, big-endian, as the LCD wants them.
typedef struct {
	uint8_t *buf;
	int width;
	int height;
} lcd_fb_t;

// Powers up the camera and starts streaming.
esp_err_t camera_init(void);

// Waits for the next camera frame and returns it, cropped and scaled to the
// 240x240 LCD size. The frame stays valid until the next call.
lcd_fb_t *camera_get_frame(void);
