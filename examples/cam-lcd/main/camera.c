#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "bsp/esp-bsp.h"
#include "linux/videodev2.h"
#include "esp_video_device.h"
#include "driver/ppa.h"
#include "camera.h"

/*
Camera code for the ESP32-P4-EYE. This devkit has a MIPI-CSI camera (OV2710) which 
is driven through the esp_video component using a an api that resembles V4L2 as used
in the Linux kernel. The camera outputs 1280x720 frames; we use the PPA (Pixel 
Processing Accelerator) to scale a centered square of these down to the 240x240 of the LCD.

Note that this code works; you should not have to change anything here to get the
project to function properly.
*/

// Aborts with a readable message if a (V4L2) call returns an error.
#define CHECK(x) do { if ((x)<0) { printf("%s failed: %s\n", #x, strerror(errno)); abort(); } } while(0)

// Number of buffers the camera driver captures into.
#define CAM_BUF_COUNT 2
// Camera resolution; must match the sensor format selected in sdkconfig.defaults.
#define CAM_W 1280
#define CAM_H 720
// We take a 640x640 square from the middle of the camera image and scale it by 6/16
// to the 240x240 LCD. (The PPA can only scale in steps of 1/16.)
#define CROP 640

static int fd;
static uint8_t *cam_buf[CAM_BUF_COUNT];
static lcd_fb_t fb={.width=BSP_LCD_H_RES, .height=BSP_LCD_V_RES};
static ppa_client_handle_t ppa;
static ppa_srm_oper_config_t srm;

esp_err_t camera_init(void) {
	// Power up the camera and register it with the esp_video framework. After this,
	// the camera is available as a V4L2 device.
	bsp_camera_cfg_t cam_cfg={0};
	ESP_RETURN_ON_ERROR(bsp_camera_start(&cam_cfg), "camera", "camera start failed");
	fd=open(BSP_CAMERA_DEVICE, O_RDONLY);
	CHECK(fd);

	// Ask for 16-bit RGB565 data. The ISP converts the raw sensor data for us.
	struct v4l2_format fmt={
		.type=V4L2_BUF_TYPE_VIDEO_CAPTURE,
		.fmt.pix={.width=CAM_W, .height=CAM_H, .pixelformat=V4L2_PIX_FMT_RGB565},
	};
	CHECK(ioctl(fd, VIDIOC_S_FMT, &fmt));

	// Let the driver allocate capture buffers and map them into our address space.
	struct v4l2_requestbuffers req={
		.count=CAM_BUF_COUNT,
		.type=V4L2_BUF_TYPE_VIDEO_CAPTURE,
		.memory=V4L2_MEMORY_MMAP,
	};
	CHECK(ioctl(fd, VIDIOC_REQBUFS, &req));
	for (int i=0; i<CAM_BUF_COUNT; i++) {
		struct v4l2_buffer buf={.type=V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory=V4L2_MEMORY_MMAP, .index=i};
		CHECK(ioctl(fd, VIDIOC_QUERYBUF, &buf));
		cam_buf[i]=mmap(NULL, buf.length, PROT_READ|PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
		CHECK(ioctl(fd, VIDIOC_QBUF, &buf));
	}
	int type=V4L2_BUF_TYPE_VIDEO_CAPTURE;
	CHECK(ioctl(fd, VIDIOC_STREAMON, &type));

	// Framebuffer for the LCD. The PPA needs this to be cache-line aligned. It is in PSRAM, so the
	// LCD SPI driver copies it while drawing and we can overwrite it as soon as the draw call returns.
	size_t fb_size=fb.width*fb.height*sizeof(uint16_t);
	fb.buf=heap_caps_aligned_calloc(64, 1, fb_size, MALLOC_CAP_SPIRAM|MALLOC_CAP_DMA);
	ESP_RETURN_ON_FALSE(fb.buf, ESP_ERR_NO_MEM, "camera", "no memory for framebuffer");

	// Set up the PPA to crop and scale the camera image to the LCD framebuffer.
	ppa_client_config_t ppa_cfg={.oper_type=PPA_OPERATION_SRM};
	ESP_RETURN_ON_ERROR(ppa_register_client(&ppa_cfg, &ppa), "camera", "PPA init failed");
	srm=(ppa_srm_oper_config_t){
		.in={
			.pic_w=CAM_W, .pic_h=CAM_H,
			.block_w=CROP, .block_h=CROP,
			.block_offset_x=(CAM_W-CROP)/2, .block_offset_y=(CAM_H-CROP)/2,
			.srm_cm=PPA_SRM_COLOR_MODE_RGB565,
		},
		.out={
			.buffer=fb.buf, .buffer_size=fb_size,
			.pic_w=fb.width, .pic_h=fb.height,
			.srm_cm=PPA_SRM_COLOR_MODE_RGB565,
		},
		.scale_x=(float)BSP_LCD_H_RES/CROP,
		.scale_y=(float)BSP_LCD_V_RES/CROP,
	};
	return ESP_OK;
}

lcd_fb_t *camera_get_frame(void) {
	// Grab a frame from the camera...
	struct v4l2_buffer buf={.type=V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory=V4L2_MEMORY_MMAP};
	CHECK(ioctl(fd, VIDIOC_DQBUF, &buf));
	// ...scale it down into the LCD framebuffer...
	srm.in.buffer=cam_buf[buf.index];
	ESP_ERROR_CHECK(ppa_do_scale_rotate_mirror(ppa, &srm));
	// ...and give the buffer back to the camera driver so it can re-use it.
	CHECK(ioctl(fd, VIDIOC_QBUF, &buf));
	// Swap the bytes of each pixel, as the LCD wants big-endian RGB565. (The PPA
	// can only byte-swap its input, which would mangle the pixels before scaling.)
	uint16_t *p=(uint16_t*)fb.buf;
	for (int i=0; i<fb.width*fb.height; i++) p[i]=(p[i]<<8)|(p[i]>>8);
	return &fb;
}
