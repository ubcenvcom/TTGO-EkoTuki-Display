#include <TFT_eSPI.h>
#include <SPI.h>

#include "WiFi.h"
#include "esp_wpa2.h"

#include <Wire.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>

#include "esp_adc_cal.h"
// #include "bmp.h"

#include "ekotuki_logo.h"
#include "foli_logo.h"
#include "te_logo.h"
#include "tsp_logo.h"

#include "wifisetup.h"

#include <JPEGDecoder.h>

#ifndef TFT_DISPOFF
#define TFT_DISPOFF 0x28
#endif

#ifndef TFT_SLPIN
#define TFT_SLPIN   0x10
#endif

#define TFT_MOSI            19
#define TFT_SCLK            18
#define TFT_CS              5
#define TFT_DC              16
#define TFT_RST             23

#define TFT_BL          4  // Display backlight control pin

TFT_eSPI tft = TFT_eSPI(135, 240);

char buff[128];

int y=0;
int l=0;

DynamicJsonDocument doc(22*1024);

void jpegRender(int xpos, int ypos) {
  uint16_t *pImg;
  uint16_t mcu_w = JpegDec.MCUWidth;
  uint16_t mcu_h = JpegDec.MCUHeight;
  uint32_t max_x = JpegDec.width;
  uint32_t max_y = JpegDec.height;

  bool swapBytes = tft.getSwapBytes();
  tft.setSwapBytes(true);
  
  // Jpeg images are draw as a set of image block (tiles) called Minimum Coding Units (MCUs)
  // Typically these MCUs are 16x16 pixel blocks
  // Determine the width and height of the right and bottom edge image blocks
  uint32_t min_w = min(mcu_w, max_x % mcu_w);
  uint32_t min_h = min(mcu_h, max_y % mcu_h);

  // save the current image block size
  uint32_t win_w = mcu_w;
  uint32_t win_h = mcu_h;

  // record the current time so we can measure how long it takes to draw an image
  uint32_t drawTime = millis();

  // save the coordinate of the right and bottom edges to assist image cropping
  // to the screen size
  max_x += xpos;
  max_y += ypos;

  // Fetch data from the file, decode and display
  while (JpegDec.read()) {    // While there is more data in the file
    pImg = JpegDec.pImage ;   // Decode a MCU (Minimum Coding Unit, typically a 8x8 or 16x16 pixel block)

    // Calculate coordinates of top left corner of current MCU
    int mcu_x = JpegDec.MCUx * mcu_w + xpos;
    int mcu_y = JpegDec.MCUy * mcu_h + ypos;

    // check if the image block size needs to be changed for the right edge
    if (mcu_x + mcu_w <= max_x) win_w = mcu_w;
    else win_w = min_w;

    // check if the image block size needs to be changed for the bottom edge
    if (mcu_y + mcu_h <= max_y) win_h = mcu_h;
    else win_h = min_h;

    // copy pixels into a contiguous block
    if (win_w != mcu_w)
    {
      uint16_t *cImg;
      int p = 0;
      cImg = pImg + win_w;
      for (int h = 1; h < win_h; h++)
      {
        p += mcu_w;
        for (int w = 0; w < win_w; w++)
        {
          *cImg = *(pImg + w + p);
          cImg++;
        }
      }
    }

    // calculate how many pixels must be drawn
    uint32_t mcu_pixels = win_w * win_h;

    // draw image MCU block only if it will fit on the screen
    if (( mcu_x + win_w ) <= tft.width() && ( mcu_y + win_h ) <= tft.height())
      tft.pushImage(mcu_x, mcu_y, win_w, win_h, pImg);
    else if ( (mcu_y + win_h) >= tft.height())
      JpegDec.abort(); // Image has run off bottom of screen so abort decoding
  }

  tft.setSwapBytes(swapBytes);
}

int drawFFSJpeg(const char *filename, int xpos, int ypos) {
  fs::File jpegFile = SPIFFS.open(filename, "r");
 
  if (!jpegFile ) {
    Serial.print("ERROR: File \""); Serial.print(filename); Serial.println ("\" not found!");
    return -1;
  }

  boolean decoded = JpegDec.decodeFsFile(jpegFile);  // Pass the SD file handle to the decoder,  

  if (decoded) {
    jpegRender(xpos, ypos);
    return 0;
  }  
  return -1;
}

//! Long time delay, it is recommended to use shallow sleep, which can effectively reduce the current consumption
void espDelay(int ms)
{
#if 1
  delay(ms);  
#else
  esp_sleep_enable_timer_wakeup(ms * 1000);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH,ESP_PD_OPTION_ON);
  esp_wifi_stop();
  esp_light_sleep_start();
  esp_wifi_start();
#endif
}

void displayError(const char *msg)
{
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(2);  
  tft.drawString(msg, tft.width() / 2, tft.height() / 2);
  delay(1000);
}

int loadJson(const char *url)
{
  HTTPClient http;

  http.begin(url);
  int httpCode=http.GET();
  
  if (httpCode!=200) {   
    Serial.println("loadJSON failed");
    Serial.println(httpCode);
    http.end();
    return -1;
  }

  DeserializationError error=deserializeJson(doc, http.getStream());
  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.c_str());
    http.end();
    return -1;
  }

  return 0;
}

void loadBike()
{ 
  tft.fillRect(0, 0, 135, 240, TFT_BLACK);
  drawFFSJpeg("/foli_s.jpg", 0, 0);
  drawFFSJpeg("/follari.jpg", 0, 103);
 
  int r=loadJson("http://data.foli.fi/citybike/smoove");
  if (r<0) {    
    return;
  }
  
  JsonArray result=doc["result"];

  int count=result.size();

  for (JsonObject repo: result) {
    const char* name=repo["name"];        
    int bikes=repo["avl_bikes"];

    Serial.println(name);
    Serial.println(bikes);

    // Skip until we find our station
    if (strncmp(name, "11 ", 3)!=0)
      continue;

    tft.setTextColor(TFT_ORANGE);
    tft.setTextSize(6);     
    sprintf(buff, "%d", bikes);

    tft.drawString(buff, tft.width() / 2, tft.height() / 2);

    break;
  }
}

void loadBusStop(const char *stop)
{
  int l=0;

  sprintf(buff, "http://data.foli.fi/siri/sm/%s", stop);
  
  int r=loadJson(buff);
  if (r<0) {    
    return;
  }

  const char *status=doc["status"];

  time_t stime=doc["servertime"]; // GMT
  stime+=(3*60*60);

  struct tm tm = *gmtime(&stime);

  tft.fillScreen(TFT_WHITE);

  tft.setTextColor(TFT_BLACK);
  
  tft.setTextSize(3);
  tft.fillRect(0,0, 240, 26, TFT_ORANGE);  
  tft.drawString(stop, 26, 8);

  tft.setTextSize(2);
  sprintf(buff, "%02d:%02d", tm.tm_hour, tm.tm_min);
  tft.drawString(buff, 110, 16);
  
  tft.setTextSize(2);
  tft.setCursor(0, 28);

  JsonArray result=doc["result"];
  int count=result.size();

  for (JsonObject repo: result) {    
    const char* ref=repo["lineref"];    
    const char* dest=repo["destinationdisplay"];    
    int dep=repo["expectedarrivaltime"];
    dep+=(3*60*60);      
    int mon=repo["monitored"];       
    int m=(dep-stime)/60;

    if (m>2) {
      if (mon==1)
        tft.setTextColor(TFT_OLIVE);
      else
        tft.setTextColor(TFT_BLACK);
      sprintf(buff, "  %02dm %s", m, ref);
    } else {
      tft.setTextColor(TFT_OLIVE);
      sprintf(buff, "  NYT %s", ref);
    }
    tft.println(buff);

    if (l>=7)
      break;

    l++;
  }

  drawFFSJpeg("/foli_s.jpg", 0, 160);
}


int wifiSetup()
{
  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);

  int c=0;

#ifdef WIFI_IS_WPA
  esp_wifi_sta_wpa2_ent_set_identity((uint8_t *)EAP_IDENTITY, strlen(EAP_IDENTITY)); //provide identity
  esp_wifi_sta_wpa2_ent_set_username((uint8_t *)EAP_IDENTITY, strlen(EAP_IDENTITY)); //provide username --> identity and username is same
  esp_wifi_sta_wpa2_ent_set_password((uint8_t *)EAP_PASSWORD, strlen(EAP_PASSWORD)); //provide password
  esp_wpa2_config_t config = WPA2_CONFIG_INIT_DEFAULT(); //set config settings to default
  esp_wifi_sta_wpa2_ent_enable(&config); //set config settings to enable function
#endif
    
  WiFi.begin(WIFI_SID, WIFI_PWD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    c++;
    if (c>10)
      return -1;
  }

  return 0;
}

void setup()
{
  Serial.begin(115200);
  Serial.println("Start");

  tft.init();
  tft.setRotation(0); 
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(0, 0);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(1);

  if (wifiSetup()<0) {
    Serial.println("WiFi: Failed");    
    tft.println("WiFi: Failed");
    delay(1000);
  } else {
    Serial.println("WiFi: OK");
    Serial.println(WiFi.localIP());    
    tft.println("WiFi: OK");
    tft.println(WiFi.localIP());
  }  
    
  tft.println("SPIFFS");

  if(!SPIFFS.begin(true)){
    Serial.println("SPIFFS Failed");
    tft.println("SPIFFS: FAIL");
    delay(1000);
  } else {
    tft.println("SPIFFS: OK");
  }
  
  tft.setSwapBytes(true);
  logos_bw();
}

void logos_bw()
{
  y=0;
  tft.fillScreen(TFT_BLACK);
  tft.drawXBitmap(2, y, ekotuki_logo_bits, 128, 64, TFT_WHITE);
  tft.drawXBitmap(2, y+64, foli_logo_bits, 128, 64, TFT_WHITE);
  tft.drawXBitmap(2, y+128, te_logo_bits, 128, 64, TFT_WHITE);
  tft.drawXBitmap(2, y+192, tsp_logo_bits, 128, 64, TFT_WHITE);
  delay(1000);
}

void logoSpons()
{
  tft.fillScreen(TFT_WHITE);
  drawFFSJpeg("/tsp_s.jpg", 0, 0);
  drawFFSJpeg("/te_s.jpg", 0, 78);
  drawFFSJpeg("/lsjh_s.jpg", 0, 156);
}

void logoEkotukiTurku()
{
  tft.fillScreen(TFT_WHITE);
  drawFFSJpeg("/ekotuki_s.jpg", 0, 0);      
  drawFFSJpeg("/turku_s.jpg", 0, 80);
}

int page(int l)
{
  switch (l) {
    case 0:
      logoSpons();
    break;
    case 1:
      loadBusStop("144");
    break;
    case 2:
      loadBusStop("151");
    break;
    case 3:
      loadBike();
    break;
    case 4:
      logoEkotukiTurku();
    break;
    default:
    delay(5000);
    return -1;
  }
  delay(5000);
  return 0;
}

void loop()
{
  int hv;

  if (WiFi.status()==WL_CONNECTED) {
    if (page(l)==0)
      l++;
    else
      l=0;
  } else {
    page(0);
    tft.println("WiFi: Failed");
    delay(8000);
    wifiSetup();
  }

  hv=hallRead();
  Serial.print("HAL: ");
  Serial.println(hv); 
}
