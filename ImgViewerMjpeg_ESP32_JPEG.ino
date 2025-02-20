/*******************************************************************************
 * ESP32 SIMD Motion JPEG Image Viewer
 * This is a simple Motion JPEG image viewer example using ESP32 SIMD
 * Image Source: https://www.pexels.com/video/earth-rotating-video-856356/
 * cropped: x: 598 y: 178 width: 720 height: 720 resized: 240x240
 * ffmpeg -i "Pexels Videos 3931.mp4" -ss 0 -t 20.4s -vf "reverse,setpts=0.5*PTS,fps=10,vflip,hflip,rotate=90,crop=720:720:178:598,scale=240:240:flags=lanczos" -q:v 11 earth.mjpeg
 *
 * Dependent libraries:
 * ESP32_JPEG: https://github.com/esp-arduino-libs/ESP32_JPEG.git
 *
 * Setup steps:
 * 1. Change your LCD parameters in Arduino_GFX setting       
 * 2. Upload Motion JPEG file
 *   FFat/LittleFS:
 *     upload FFat (FatFS) data with ESP32 Sketch Data Upload:
 *     ESP32: https://github.com/lorol/arduino-esp32fs-plugin
 *   SD:
 *     Most Arduino system built-in support SD file system.

 * command for ffmpeg: ffmpeg -i hakari_jjk.mp4 -vf "fps=30" -q:v 9 -preset ultrafast hakari_jjk.mjpeg
 *                     ffmpeg -i hakari_jjk.mp4 -ss 0 -t 16.3s -vf "fps=30" -q:v 9 -preset ultrafast hakari_jjk.mjpeg

 ******************************************************************************/
#define ROOT "/root"
#define MJPEG_FILENAME ROOT "/hakari_jjk.mjpeg"
#define MJPEG_OUTPUT_SIZE (170 * 320 * 2)          // memory for a output image frame
#define MJPEG_BUFFER_SIZE (MJPEG_OUTPUT_SIZE / 8) // memory for a single JPEG frame

/*******************************************************************************
 * Start of Arduino_GFX setting
 *
 * Arduino_GFX try to find the settings depends on selected board in Arduino IDE
 * Or you can define the display dev kit not in the board list
 * Defalult pin list for non display dev kit:
 * ESP32 various dev board     : CS:  5, DC: 27, RST: 33, BL: 22, SCK: 18, MOSI: 23, MISO: nil
 * ESP32-C3 various dev board  : CS:  7, DC:  2, RST:  1, BL:  3, SCK:  4, MOSI:  6, MISO: nil
 * ESP32-S2 various dev board  : CS: 34, DC: 38, RST: 33, BL: 21, SCK: 36, MOSI: 35, MISO: nil
 * ESP32-S3 various dev board  : CS: 40, DC: 41, RST: 42, BL: 48, SCK: 36, MOSI: 35, MISO: nil
 ******************************************************************************/
#include <Arduino_GFX_Library.h>

#define GFX_BL DF_GFX_BL // default backlight pin, you may replace DF_GFX_BL to actual backlight pin

/* More dev device declaration: https://github.com/moononournation/Arduino_GFX/wiki/Dev-Device-Declaration */
#if defined(DISPLAY_DEV_KIT)
Arduino_GFX *gfx = create_default_Arduino_GFX();
#else /* !defined(DISPLAY_DEV_KIT) */

/* More data bus class: https://github.com/moononournation/Arduino_GFX/wiki/Data-Bus-Class */
Arduino_DataBus *bus = create_default_Arduino_DataBus();

/* More display class: https://github.com/moononournation/Arduino_GFX/wiki/Display-Class */
// Arduino_GFX *gfx = new Arduino_ILI9341(bus, DF_GFX_RST, 3 /* rotation */, false /* IPS */);
Arduino_GFX *gfx = new Arduino_ST7789(bus, 42 /* RST */, 0 /* rotation */, true /* IPS */, 170, 320, 35, 0, 35, 0);

#endif /* !defined(DISPLAY_DEV_KIT) */
/*******************************************************************************
 * End of Arduino_GFX setting
 ******************************************************************************/

#include <FFat.h>
#include <LittleFS.h>
#include <SPIFFS.h>
#include <SD.h>
#include <SD_MMC.h>

#include "MjpegClass.h"
static MjpegClass mjpeg;

/* variables */
static int total_frames = 0;
static unsigned long total_read_video = 0;
static unsigned long total_decode_video = 0;
static unsigned long total_show_video = 0;
static unsigned long start_ms, curr_ms;
static int16_t x = -1, y = -1, w = -1, h = -1;

void setup()
{
  Serial.begin(115200);
  // Serial.setDebugOutput(true);
  // while(!Serial);
  Serial.println("Arduino_GFX Motion JPEG SIMD Decoder Image Viewer example");

#ifdef GFX_EXTRA_PRE_INIT
  GFX_EXTRA_PRE_INIT();
#endif

  // Init Display
  if (!gfx->begin())
  {
    Serial.println("gfx->begin() failed!");
  }
  gfx->fillScreen(BLACK);

#ifdef GFX_BL
  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, HIGH);
#endif

  // if (!FFat.begin(false, ROOT))
  // if (!LittleFS.begin(false, ROOT, 80000000L))
  // if (!SPIFFS.begin(false, ROOT))
  SPI.begin(36 /* CLK */, 37 /* D0/MISO */, 35 /* CMD/MOSI */);
  if (!SD.begin(38 /* CS */, SPI, 80000000L, ROOT))
  // pinMode(10 /* CS */, OUTPUT);
  // digitalWrite(SD_CS, HIGH);
  // SD_MMC.setPins(12 /* CLK */, 11 /* CMD/MOSI */, 13 /* D0/MISO */);
  // if (!SD_MMC.begin(ROOT, true /* mode1bit */, false /* format_if_mount_failed */, SDMMC_FREQ_DEFAULT))
  // SD_MMC.setPins(12 /* CLK */, 11 /* CMD/MOSI */, 13 /* D0/MISO */, 14 /* D1 */, 15 /* D2 */, 10 /* D3/CS */);
  // if (!SD_MMC.begin(ROOT, false /* mode1bit */, false /* format_if_mount_failed */, SDMMC_FREQ_HIGHSPEED))
  {
    Serial.println(F("ERROR: File System Mount Failed!"));
    gfx->println(F("ERROR: File System Mount Failed!"));
  }
  else
  {
    uint8_t *mjpeg_buf = (uint8_t *)aligned_alloc(16, MJPEG_BUFFER_SIZE);
    if (!mjpeg_buf)
    {
      Serial.println(F("mjpeg_buf malloc failed!"));
    }
    else
    {
      uint16_t *output_buf = (uint16_t *)aligned_alloc(16, MJPEG_OUTPUT_SIZE);
      if (!output_buf)
      {
        Serial.println(F("output_buf malloc failed!"));
      }
      else
      {
        while (true) // Vòng lặp vô hạn để phát lại video từ đầu sau khi kết thúc
        {
          // Serial.println(F("MJPEG start"));

          start_ms = millis();
          curr_ms = millis();
          if (!mjpeg.setup(
                  MJPEG_FILENAME, mjpeg_buf,
                  output_buf, MJPEG_OUTPUT_SIZE, true /* useBigEndian */))
          {
            Serial.println(F("mjpeg.setup() failed!"));
            break; // Thoát khỏi vòng lặp nếu setup thất bại
          }
          else
          {
            total_frames = 0;
            total_read_video = 0;
            total_decode_video = 0;
            total_show_video = 0;
            x = -1;

            while (mjpeg.readMjpegBuf())
            {
              // Read video
              total_read_video += millis() - curr_ms;
              curr_ms = millis();

              // Play video
              mjpeg.decodeJpg();
              total_decode_video += millis() - curr_ms;
              curr_ms = millis();

              if (x == -1)
              {
                w = mjpeg.getWidth();
                h = mjpeg.getHeight();
                x = (w > gfx->width()) ? 0 : ((gfx->width() - w) / 2);
                y = (h > gfx->height()) ? 0 : ((gfx->height() - h) / 2);
              }
              gfx->draw16bitBeRGBBitmap(x, y, output_buf, w, h);
              total_show_video += millis() - curr_ms;

              curr_ms = millis();
              total_frames++;
            }
            int time_used = millis() - start_ms;
            // Serial.println(F("MJPEG end"));

            float fps = 1000.0 * total_frames / time_used;
            // Serial.printf("Arduino_GFX ESP32 SIMD MJPEG decoder\n\n");
            // Serial.printf("Frame size: %d x %d\n", mjpeg.getWidth(), mjpeg.getHeight());
            // Serial.printf("Total frames: %d\n", total_frames);
            // Serial.printf("Time used: %d ms\n", time_used);
            Serial.printf("Average FPS: %0.1f\n", fps);
            // Serial.printf("Read MJPEG: %lu ms (%0.1f %%)\n", total_read_video, 100.0 * total_read_video / time_used);
            // Serial.printf("Decode video: %lu ms (%0.1f %%)\n", total_decode_video, 100.0 * total_decode_video / time_used);
            // Serial.printf("Show video: %lu ms (%0.1f %%)\n", total_show_video, 100.0 * total_show_video / time_used);

            // gfx->setCursor(0, 0);
            // gfx->printf("Arduino_GFX ESP32 SIMD MJPEG decoder\n\n");
            // gfx->printf("Frame size: %d x %d\n", mjpeg.getWidth(), mjpeg.getHeight());
            // gfx->printf("Total frames: %d\n", total_frames);
            // gfx->printf("Time used: %d ms\n", time_used);
            // gfx->printf("Average FPS: %0.1f\n", fps);
            // gfx->printf("Read MJPEG: %lu ms (%0.1f %%)\n", total_read_video, 100.0 * total_read_video / time_used);
            // gfx->printf("Decode video: %lu ms (%0.1f %%)\n", total_decode_video, 100.0 * total_decode_video / time_used);
            // gfx->printf("Show video: %lu ms (%0.1f %%)\n", total_show_video, 100.0 * total_show_video / time_used);

            mjpeg.close();
          }
          delay(1);
        }
      }
    }
  }
}

void loop()
{
}