#include "WiFi.h"
#include "esp_camera.h"
#include "Arduino.h"
#include "soc/soc.h"          // Disable brownout problems
#include "soc/rtc_cntl_reg.h" // Disable brownout problems
#include "driver/rtc_io.h"
#include <SPIFFS.h>
#include <FS.h>
#include <Firebase_ESP_Client.h>
#include <WiFiManager.h>
// Provide the token generation process info.
#include <addons/TokenHelper.h>
#include "config.h"



uint32_t lastCatEntered = 0;
uint32_t lastPictureForAutoExposure = 0;
uint16_t catEnterInvall = 1000;

uint16_t numCorrectionFrames = 0;

bool takePicture = false;
// Define Firebase Data objects
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig configF;

void initWiFi()
{
  WiFiManager wifiManager;
  bool connected = wifiManager.autoConnect("AutoConnectAP");
  Serial.println("Connected to WiFi.");
}

void initCamera()
{
  // OV2640 camera module
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound())
  {
    config.frame_size = FRAMESIZE_VGA; // we dont need much quality
    config.jpeg_quality = 10;
    config.fb_count = 2;
  }
  else
  {
    config.frame_size = FRAMESIZE_SVGA; // turn quality down al the way
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }
  // Camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK)
  {
    Serial.printf("Camera init failed with error 0x%x", err);
    ESP.restart();
  }
}

void catEnttered()
{
  if (millis() - lastCatEntered < catEnterInvall || digitalRead(BUTTON_PIN) == 0)
    return;
  lastCatEntered = millis();
  struct tm timeinfo;
  takePicture = true;
  Serial.println("Cat entered");
}

void setup()
{
  // Serial port for debugging purposes
  Serial.begin(115200);
  initWiFi();
  // Turn-off the 'brownout detector'
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  initCamera();

  // Firebase
  //  Assign the api key
  configF.api_key = API_KEY;
  // Assign the user sign in credentials
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  // Assign the callback function for the long running token generation task
  configF.token_status_callback = tokenStatusCallback; // see addons/TokenHelper.h

  Firebase.begin(&configF, &auth);
  Firebase.reconnectWiFi(true);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(BUTTON_PIN, catEnttered, FALLING); // attach interrupt that will trigger the picture upload process

  sensor_t *s = esp_camera_sensor_get();
  s->set_vflip(s, -1);
  s->set_gain_ctrl(s, 1);     // auto gain on
  s->set_exposure_ctrl(s, 1); // auto exposure on
  s->set_awb_gain(s, 1);      // Auto White Balance enable (0 or 1)
  s->set_brightness(s, -2);
}

void loop()
{
  if ((lastPictureForAutoExposure + 600000 * 5 < millis()) ||                    // make an correction frame every 5 minutes
      (lastPictureForAutoExposure + 5000 < millis() && numCorrectionFrames < 5)) // if the camera hasnt corrected now make a correction every 5 seconds
  {
    numCorrectionFrames++;
    lastPictureForAutoExposure = millis();
    camera_fb_t *fb = NULL; // pointer
    fb = esp_camera_fb_get();
    delay(5);
    Serial.println("calibrating exposure...");
    esp_camera_fb_return(fb);
  }

  if (Firebase.ready() && takePicture)
  {
    takePicture = false;
    String filename = (String("cat") + String(millis()) + ".jpg"); // make a filename from the runtime
    camera_fb_t *fb = NULL; // pointer
    bool ok = 0;            // Boolean indicating if the picture has been taken correctly
    // Take a photo with the camera
    Serial.println("Taking a photo...");
    fb = esp_camera_fb_get(); // take the photo
    if (!fb) // check if everything worked
    {
      Serial.println("Camera capture failed");
      return;
    }
    else
    {
      delay(1);
      Serial.print("Uploading picture... "); 
      if (Firebase.Storage.upload(&fbdo, STORAGE_BUCKET_ID, fb->buf, fb->len, filename, "image/jpeg")) // send the photo to firebase
      {
        Serial.printf("\nDownload URL: %s\n", fbdo.downloadURL().c_str());
      }
      else
      {
        Serial.println(fbdo.errorReason()); 
      }
    }
  esp_camera_fb_return(fb); // return the framebuffer for reuse
  delay(1000);
  }
}
