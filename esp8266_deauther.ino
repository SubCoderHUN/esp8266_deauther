/*
   ===========================================
      Copyright (c) 2018 Stefan Kremser
             github.com/spacehuhn
   ===========================================
 */

extern "C" {
    // Please follow this tutorial:
    // https://github.com/spacehuhn/esp8266_deauther/wiki/Installation#compiling-using-arduino-ide
    // And be sure to have the right board selected
  #include "user_interface.h"
}

#include "EEPROMHelper.h"

#include <ArduinoJson.h>
#if ARDUINOJSON_VERSION_MAJOR != 5
// The software was build using ArduinoJson v5.x
// version 6 is still in beta at the time of writing
// go to tools -> manage libraries, search for ArduinoJSON and install version 5
#error Please upgrade/downgrade ArduinoJSON library to version 5!
#endif // if ARDUINOJSON_VERSION_MAJOR != 5

#include "oui.h"
#include "language.h"
#include "functions.h"
#include "Settings.h"
#include "Names.h"
#include "SSIDs.h"
#include "Scan.h"
#include "Attack.h"
#include "CLI.h"
#include "DisplayUI.h"
#include "A_config.h"
#include "webfiles.h"

#include "LED.h"
#include "DisplayUI.h"

// Run-Time Variables //
LED led;
Settings settings;
Names    names;
SSIDs    ssids;
Accesspoints accesspoints;
Stations     stations;
Scan   scan;
Attack attack;
CLI    cli;
DisplayUI displayUI;

#include "wifi.h"

uint32_t autosaveTime = 0;
uint32_t currentTime  = 0;

bool booted = false;

void setup() {
    // for random generator
    randomSeed(os_random());

    // start serial
    Serial.begin(115200);
    Serial.println();

    // start SPIFFS
    prnt(SETUP_MOUNT_SPIFFS);
    bool spiffsError = !SPIFFS.begin();
    prntln(spiffsError ? SETUP_ERROR : SETUP_OK);

    // Start EEPROM
    EEPROMHelper::begin(EEPROM_SIZE);

#ifdef FORMAT_SPIFFS
    prnt(SETUP_FORMAT_SPIFFS);
    SPIFFS.format();
    prntln(SETUP_OK);
#endif // ifdef FORMAT_SPIFFS

#ifdef FORMAT_EEPROM
    prnt(SETUP_FORMAT_EEPROM);
    EEPROMHelper::format(EEPROM_SIZE);
    prntln(SETUP_OK);
#endif // ifdef FORMAT_EEPROM

    // Format SPIFFS when in boot-loop
    if (spiffsError || !EEPROMHelper::checkBootNum(BOOT_COUNTER_ADDR)) {
        prnt(SETUP_FORMAT_SPIFFS);
        SPIFFS.format();
        prntln(SETUP_OK);

        prnt(SETUP_FORMAT_EEPROM);
        EEPROMHelper::format(EEPROM_SIZE);
        prntln(SETUP_OK);

        EEPROMHelper::resetBootNum(BOOT_COUNTER_ADDR);
    }

    // get time
    currentTime = millis();

    // load settings
    #ifndef RESET_SETTINGS
    settings.load();
    #else // ifndef RESET_SETTINGS
    settings.reset();
    settings.save();
    #endif // ifndef RESET_SETTINGS

    // set mac address
    wifi_set_macaddr(STATION_IF, (uint8_t*)settings.getWifiSettings().mac_st);
    wifi_set_macaddr(SOFTAP_IF, (uint8_t*)settings.getWifiSettings().mac_ap);

    // start WiFi
    WiFi.mode(WIFI_OFF);
    wifi_set_opmode(STATION_MODE);
    wifi_set_promiscuous_rx_cb([](uint8_t* buf, uint16_t len) {
        scan.sniffer(buf, len);
    });

    // start display
    if (settings.getDisplaySettings().enabled) {
        displayUI.setup();
        displayUI.mode = displayUI.DISPLAY_MODE::INTRO;
    }

    // copy web files to SPIFFS
    copyWebFiles(false);

    // load everything else
    names.load();
    ssids.load();
    cli.load();

    // create scan.json
    scan.setup();

    // set channel
    setWifiChannel(settings.getWifiSettings().channel);

    // load Wifi settings: SSID, password,...
    loadWifiConfigDefaults();

    // dis/enable serial command interface
    if (settings.getCLISettings().enabled) {
        cli.enable();
    } else {
        prntln(SETUP_SERIAL_WARNING);
        Serial.flush();
        Serial.end();
    }

    // start access point/web interface
    if (settings.getWebSettings().enabled) startAP();

    // STARTED
    prntln(SETUP_STARTED);

    // version
    prntln(DEAUTHER_VERSION);

    // setup LED
    led.setup();
}

void fpm_wakup_cb_func1(void)  {
  wifi_fpm_close(); // disable force sleep function
}

void light_sleep(){
   wifi_station_disconnect();
   wifi_set_opmode_current(NULL_MODE);
   wifi_fpm_set_sleep_type(LIGHT_SLEEP_T); // set sleep type, the above    posters wifi_set_sleep_type() didnt seem to work for me although it did let me compile and upload with no errors 
   wifi_fpm_open(); // Enables force sleep
   //gpio_pin_wakeup_enable(GPIO_ID_PIN(2), GPIO_PIN_INTR_LOLEVEL); // GPIO_ID_PIN(2) corresponds to GPIO2 on ESP8266-01 , GPIO_PIN_INTR_LOLEVEL for a logic low.
   //wifi_fpm_set_wakeup_cb(fpm_wakup_cb_func1); // Set wakeup callback
   wifi_fpm_do_sleep(0xFFFFFFF); // Sleep for longest possible time
 }
void loop() {
    currentTime = millis();
    displayUI.update();
    if(!displayUI.hibernation)
     {
       led.update();    // update LED color
       attack.update(); // run attacks
       cli.update();    // read and run serial input
       scan.update();   // run scan
       ssids.update();  // run random mode, if enabled
     }
     
     if(!attack.isRunning() && displayUI.hibernation) //For energy saving, hibernation
    {
      if(led.ledenabled)
      {
        led.setMode(HIBERNATE);
      }
        light_sleep();
    }
     if(!AP_HIDDEN)
     wifiUpdate();    // manage access point


    // auto-save
    if (settings.getAutosaveSettings().enabled
        && (currentTime - autosaveTime > settings.getAutosaveSettings().time)) {
        autosaveTime = currentTime;
        names.save(false);
        ssids.save(false);
        settings.save(false);
    }

    if (!booted) {
        booted = true;
        EEPROMHelper::resetBootNum(BOOT_COUNTER_ADDR);
#ifdef HIGHLIGHT_LED
        displayUI.setupLED();
#endif // ifdef HIGHLIGHT_LED
    }
}
