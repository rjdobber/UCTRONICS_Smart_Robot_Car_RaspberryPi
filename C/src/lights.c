#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "lights.h"
#include "ws2811.h"
#include "pwm_common.h"

#define STRIP_TYPE              WS2811_STRIP_RGB    // WS2812/SK6812RGB integrated chip+leds
//#define STRIP_TYPE            WS2811_STRIP_GBR    // WS2812/SK6812RGB integrated chip+leds
//#define STRIP_TYPE            SK6812_STRIP_RGBW   // SK6812RGBW (NOT SK6812RGB)
#define WIDTH                   4
#define HEIGHT                  4
#define LED_COUNT               (WIDTH * HEIGHT)
#define TARGET_FREQ             WS2811_TARGET_FREQ
#define DMA                     10
#define GPIO_PIN                18

int width = WIDTH;
int height = HEIGHT;
int led_count = LED_COUNT;
unsigned long color = 0xFF0000;
unsigned int brightness = 100;

ws2811_t ledstring =
{
  .freq = TARGET_FREQ,
  .dmanum = DMA,
  .channel =
  {
    [0] =
    {
      .gpionum = GPIO_PIN,
      .count = LED_COUNT,
      .invert = 0,
      .brightness = 100,
      .strip_type = STRIP_TYPE,
    },
    [1] =
    {
      .gpionum = 0,
      .count = 0,
      .invert = 0,
      .brightness = 0,
    },
  },
};

ws2811_led_t *matrix;

unsigned long grb_colour_table[] =
{
  0xFF0000,  // green
  0x00FF00,  // red
  0x0000FF,  // blue
  0xFFFF00,  //yellow
  0xFF00FF,
  0x00FFFF,  //pink
  0xFFFFFF,  // white
  //0x000000,
};

void getLedSta(unsigned char disWarning, unsigned char client_Connected) {

  static unsigned long previous_time = 0;
  static unsigned long now_time = 0;
  static unsigned long time_stamp = 0;
  static unsigned char flag = 0;
  static unsigned char colour_index = 0;

  if (!disWarning) {
    if (!client_Connected) {
      if (!flag) {
        flag = 1;
        previous_time = get_pwm_timestamp();
      }
      now_time = get_pwm_timestamp();
      time_stamp = now_time - previous_time;
      if (time_stamp > 500000) {
        time_stamp = 0;
        flag = 0;
        colour_index = (1 + colour_index) % 7;
        //GRB_work(3, grb_colour_table[colour_index], 50);
        GRB_MultiColour_work(3, 100 );
      }
    }
  }
}

void GRB_work(unsigned int ledNum, unsigned long colour, int brightness ) {
  printf("GRB_work: %d, 0x%.6lx, %d\n", ledNum, colour, brightness);
  int i = ledNum-1;
  ledstring.channel[0].brightness = brightness;
  for (i = 0; i < ledNum; i++) {
    ledstring.channel[0].leds[i] = colour;
  }
  ws2811_render(&ledstring) ;
}

void GRB_MultiColour_work(unsigned int ledNum, int brightness ) {
  int i = ledNum-1;
  static int colour_index = 0;
  printf("GRB_MultiColour_work: %d, 0x%.6lx, %d\n", ledNum, grb_colour_table[colour_index], brightness);

  ledstring.channel[0].brightness = brightness;
  for (i = 0; i < ledNum; i++) {
    colour_index = (1 + colour_index) % 7;
    ledstring.channel[0].leds[i] = grb_colour_table[colour_index];
  }
  ws2811_render(&ledstring) ;
}

void GRBInit() {
  ws2811_return_t ret;
  matrix = malloc(sizeof(ws2811_led_t) * width * height);
  if ((ret = ws2811_init(&ledstring)) != WS2811_SUCCESS)
  {
    fprintf(stderr, "ws2811_init failed: %s\n", ws2811_get_return_t_str(ret));
  }
}

unsigned long getColor() {
    return color;
}

unsigned int getBrightness() {
    return brightness;
}

void setColor(unsigned long _color) {
    color = _color;
}

void setBrightness(unsigned int _brightness) {
    brightness = _brightness;
}
