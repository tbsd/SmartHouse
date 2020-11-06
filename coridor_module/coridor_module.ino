#if LWIP_FEATURES && !LWIP_IPV6

#define HAVE_NETDUMP 0

#ifndef STASSID
#define STASSID "********************"
#define STAPSK  "********************"
#endif

#include <ESP8266WiFi.h>
#include <lwip/napt.h>
#include <lwip/dns.h>
#include <dhcpserver.h>
#include "NTPClient.h"
#include "WiFiUdp.h"

#define NAPT 1000
#define NAPT_PORT 10

#if HAVE_NETDUMP

#include <NetDump.h>

void dump(int netif_idx, const char* data, size_t len, int out, int success) {
  (void)success;
  Serial.print(out ? F("out ") : F(" in "));
  Serial.printf("%d ", netif_idx);

  {
    netDump(Serial, data, len);
  }
}
#endif

#include <NeoPixelSegmentBus.h>
#include <NeoPixelBrightnessBus.h>
#include <NeoPixelAnimator.h>
#include <NeoPixelBus.h>

#define MOVE_S1 16
#define MOVE_S2 5
#define LIGHTS 0
#define LESSER_LIGHTS 15 // actually data flows through RX pin somehow

#define LED_COUNT 23

bool IsLightsOn = false; 
int TimeToIncreaseLights = 50 * 1000;
int TimeToCheckLightness = 10 * 60 * 1000;
bool IsLesserLightsOn = false;
bool IsDay = false;
double NightTimer; 
int ConnectAttemptsCount = 30;
NeoPixelBus<NeoGrbwFeature, NeoSk6812Method> strip(LED_COUNT, LESSER_LIGHTS);
const long utcOffsetInSeconds = 3 * 60 * 60;
WiFiUDP NtpUDP;
NTPClient TimeClient(NtpUDP, "pool.ntp.org", utcOffsetInSeconds, 9 * 60 * 1000);

void(* resetFunc) (void) = 0;
void switchLesserLights();
void triggerRelay(int pin);

void setup() {
  delay(100);
  Serial.begin(115200);
  Serial.printf("\n\nNAPT Range exte22222nder\n");
  Serial.printf("Heap on start: %d\n", ESP.getFreeHeap());

#if HAVE_NETDUMP
  phy_capture = dump;
#endif

  // first, connect to STA so we can get a proper local DNS server
  WiFi.mode(WIFI_STA);
  WiFi.begin(STASSID, STAPSK);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(1000);
    ++attempts;
    if (attempts == ConnectAttemptsCount) {
    Serial.println("Resetting because can't connect to wifi");
    resetFunc();
    }
  }
  Serial.printf("\nSTA: %s (dns: %s / %s)\n",
                WiFi.localIP().toString().c_str(),
                WiFi.dnsIP(0).toString().c_str(),
                WiFi.dnsIP(1).toString().c_str());

  // give DNS servers to AP side
  dhcps_set_dns(0, WiFi.dnsIP(0));
  dhcps_set_dns(1, WiFi.dnsIP(1));

  WiFi.softAPConfig(  // enable AP, with android-compatible google domain
    IPAddress(172, 217, 28, 254),
    IPAddress(172, 217, 28, 254),
    IPAddress(255, 255, 255, 0));
  WiFi.softAP("☆" STASSID "☆", STAPSK);
  Serial.printf("AP: %s\n", WiFi.softAPIP().toString().c_str());

  Serial.printf("Heap before: %d\n", ESP.getFreeHeap());
  err_t ret = ip_napt_init(NAPT, NAPT_PORT);
  Serial.printf("ip_napt_init(%d,%d): ret=%d (OK=%d)\n", NAPT, NAPT_PORT, (int)ret, (int)ERR_OK);
  if (ret == ERR_OK) {
    ret = ip_napt_enable_no(SOFTAP_IF, 1);
    Serial.printf("ip_napt_enable_no(SOFTAP_IF): ret=%d (OK=%d)\n", (int)ret, (int)ERR_OK);
    if (ret == ERR_OK) {
      Serial.printf("WiFi Network '%s' with same password is now NATed behind '%s'\n", STASSID "extender", STASSID);
    }
  }
  Serial.printf("Heap after napt init: %d\n", ESP.getFreeHeap());
  if (ret != ERR_OK) {
    Serial.printf("NAPT initialization failed\n");
  }
  pinMode(MOVE_S1, INPUT_PULLUP);
  pinMode(MOVE_S2, INPUT_PULLUP);
  pinMode(LIGHTS, OUTPUT);
  digitalWrite(LIGHTS, LOW);

  strip.Begin();
  strip.ClearTo(RgbwColor(0, 0, 0, 0));   
  strip.Show();  

  TimeClient.begin();
  TimeClient.forceUpdate();
  delay(2000);
  if (TimeClient.getEpochTime() < 1604143340) {
    Serial.println("Resetting because of time bug");
    resetFunc();
  }
  Serial.println("Configuration done.");
}

#else

void setup() {
  Serial.begin(115200);
  Serial.printf("\n\nNAPT not supported in this configuration\n");
}

#endif

void loop() { 
  delay(10); 
  static long lastLightCheck = 0;
  if (millis() + TimeToCheckLightness > lastLightCheck && !IsLightsOn && !IsLesserLightsOn) {
    TimeClient.update();
    int hours = TimeClient.getHours();    
    IsDay = hours >= 8 && hours < 23;
    lastLightCheck = millis();
  }
  bool moveS1 = digitalRead(MOVE_S1);
  bool moveS2 = digitalRead(MOVE_S2);
  if (moveS1 || moveS2) {
    if (!IsLightsOn && !IsLesserLightsOn) {
      if (IsDay) {
        triggerRelay(LIGHTS);
        IsLightsOn = true;
      } else {
        switchLesserLights();
        NightTimer = millis();
      }
    }
  } else {
    if (IsLightsOn) {
      triggerRelay(LIGHTS);
      IsLightsOn = false;
    }
    if (IsLesserLightsOn) {
      switchLesserLights();
    }
  }
  if (IsLesserLightsOn && millis() - NightTimer >= TimeToIncreaseLights) {
    if (!IsLightsOn) {
      triggerRelay(LIGHTS);
      IsLightsOn = true;
      delay(5);
      switchLesserLights();
    }
  }
}

void triggerRelay(int pin) {
  digitalWrite(pin, HIGH);
  delay(150);
  digitalWrite(pin, LOW);
}

void switchLesserLights() {
  int maxLight = 255;
  if (IsLesserLightsOn) {
    IsLesserLightsOn = false;
    for (int brightness = maxLight; brightness >= 0; --brightness) {
      strip.ClearTo(RgbwColor(0, 0, 0, brightness));
      strip.Show();
      delay(10);
    };
  } else {
    IsLesserLightsOn = true;
    for (int brightness = 0; brightness <= maxLight; ++brightness) {
      strip.ClearTo(RgbwColor(0, 0, 0, brightness));
      strip.Show();
      delay(10);
    }
    }
  }