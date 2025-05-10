#include "esp_err.h"

#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>

#include "camera.h"

void app_main(void *) {
	camera_init();
	camera_shoot();
}
	
