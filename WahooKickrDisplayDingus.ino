# define cVERSION "20231229"
// Check and set the default values for rider Weight in Kilograms, FTP in watts - enter FTP in increments of 5!
// These defaults can be changed and saved directly thru the Dingus interface
// by pressing both buttons at once and following the prompts.   Those changes
// are saved directly to the flash memory of the ESP32, and will remain in effect
// unless you reload this program.
#define defaultWEIGHT 67
#define defaultFTP 180
// RESOLUTION
// 170 x 320 ... LILYGO T-Display-S3 ESP32-S3
// 135 x 240 ... LILYGO TTGO T-Display ESP32
#define RESOLUTION_X 320
#define RESOLUTION_Y 170
/*********************************************************************************************************
* Wahoo Kickr SmartBike Display Dingus
* Jay Wheeler 
* First posted to git at https://github.com/ejwheeler3/WahooKickrDisplayDingus on Dec 23, 2023
*
* Displays gears, grade, power in watts or W/KG and Power Zone info on a LILYGO T-Display-S3 ESP32-S3.
*
* Watts / W/KG toggles using the boot button - this is the bottom right button when using the 
* display in landscape mode.
*
* Adapted from other working versions by Nikolaus Kirchsteige, Gareth Hay and others
* Also adopts approaches by Volos Projects on Youtube, in particular the use of a sprite to get
* rid of annoying display flickering thru the use of Sprite update rather than piecemeal direct 
* updates of various screen elements - see https://www.youtube.com/watch?v=sRGXQg7028A
*

Known Bugs:
+ tilt lock / grade does not display correctly until the lock is toggled on the bike

To do:
+ add support for cadence?
+ add support for heart monitor(s)?

Change Log:
12/24/2023 - removed or commented out the bulk of the serial debug statements, as it came to my attention
            that these really slow down program execution.  Left only those tied to the initial handshake.
            Move the gearing graphic closer to the upper left corner, and moving the gearing text over.
            Further tightened up the horizontal spacing of  gear graphic.  Fixed the code to display both
            front and rear gear in red when out of gears.  Cleaned up the gear code somewhat. 
            Added vertical and horizontal dividers between the various fields.
12/25/2023 - replaced top down view of gears with side view
12/26/2023 - fine tuned gear side view.   Misc code cleanup, added message to display if connection fails, 
          previously that info went only to the serial out.   Misc code cleanup.  Converted
          currentGrade variable from String to Float to allow color coding the grade readout.
          Color changes set at -2, 3, 8 and 13%.
12/27/2023 - further fine tuning of side gear view to make chain more visible, color coding on bottom three gears
          chain is now green for large chain ring, sky blue for lower chain ring, overridden to yellow for 1:3
          orange for 1:2 and red for 1:1.  Converted seconds counter to Elapsed Time, formatted for HH:MM:SS.
          Added ability to toggle between instantaneous power, 3s average, and 10s average using upper button.  
          3 seconds is the default.
          Screen now "flashes" to acknowledge button press on either top or bottom button.
12/28/2023 - added mode to input weight and ftp (by pressing both buttons simultaneously).
          Added functionality to save and restore setting for FTP, weight, power mode and average mode based on
          tutorial by Rui Santos: https://www.youtube.com/watch?v=MxL1PqS2JR0&list=PLl9I6wb_jp15AZgPhSATzCawb8GuSduxv&index=7
12/29/2023 - modified eeprom save logic to only save after the user changes weight/ftp - at which point it will also
          save the currently selected power mode (instant/3s or 10s) and watts vs W/KG.  Originally I had it save each
          time the user toggle power mode or watts vs W/KG - but on furhter reading, learned that ESP32 only guarantees
          10,000 to 100,000 write cycles to the flash memory - plenty for the user to update weight/ftp, but perhaps a problem
          if the user constantly toggles power modes.
**********************************************************************************************************/

#include "BLEDevice.h"
#include <TFT_eSPI.h> // Graphics and font library for ST7735 driver chip
#include <SPI.h>
#include <EEPROM.h> // to store settings between power cycles
#define EEPROM_SIZE 6 // 1 byte for power mode (watts or w/kg), 1 byte for avg mode (instant, 3s, 10s), 2 bytes each for weight and ftp
TFT_eSPI tft = TFT_eSPI();  // Invoke library, pins defined in User_Setup.h
// define a sprite to cover the whole screen, avoids flickr - s
TFT_eSprite imgSprite = TFT_eSprite(&tft);


#define POWERTEXT_Y  68
// BUTTONs
#define BOTTOM_BUTTON_PIN 0
#define TOP_BUTTON_PIN 14

/* BLE Services --------------------------------------------------------------*/
// Gearing
// BLE Service (UUID is case sensitive)
static BLEUUID serviceGearingUUID("a026ee0d-0a7d-4ab3-97fa-f1500f9feb8b");
static BLEUUID charGearingUUID("a026e03a-0a7d-4ab3-97fa-f1500f9feb8b");
//for currentGrade
static BLEUUID serviceGradeUUID("a026ee0b-0a7d-4ab3-97fa-f1500f9feb8b");
static BLEUUID charGradeUUID("a026e037-0a7d-4ab3-97fa-f1500f9feb8b");
// Power Measurement
static BLEUUID servicePowerUUID("00001818-0000-1000-8000-00805f9b34fb");
static BLEUUID charPowerUUID("00002a63-0000-1000-8000-00805f9b34fb");
/* --------------------------------------------------------------------------*/
static boolean doConnect = false;
static boolean connected = false;
static boolean doScan = false;
static BLERemoteCharacteristic* pRemoteCharacteristic;
static BLERemoteCharacteristic* pRemoteCharacteristic2;
static BLERemoteCharacteristic* pRemoteCharacteristic3;
static BLEAdvertisedDevice* myDevice;
float currentGrade = 0;
float currentPower = 0;
int currentChainRing = 1;
int currentCassette = 1;
int numFrontGears = 1;
int numRearGears = 1;

uint8_t arr[32];
int tilt_lock = 0;
int intWattsOrWKG = 0; // 0 = watts, 1 = W/KG
int  powerAvgMode = 1; // 0 = instant 1 = 3s, 2 = 10s
long maxPower = 0;
long powerObservations = 0;
float powerCumulativeTotal = 0;
long lastPowerObservationSecond = 0;
long intraSecondObservations = 0;
long intraSecondCumulativeTotal = 0;
#define maxSecsToAvg 12
float avgBySecond[maxSecsToAvg];

#define inputNormalMode 0
#define inputWeightMode 1
#define inputFTPMode 2
int inputMode = 0; // 0 for normal mode, 1 for weight input, 2 for ftp input
int inputWeightKGs = defaultWEIGHT;
int inputFTP = defaultFTP;
#define minWeight 25
#define maxWeight 255
#define minFTP 50
#define maxFTP 500



/***************************************************************************************************************************

  Callback and Bluetooth Stuff

***************************************************************************************************************************/


class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {
  }

  void onDisconnect(BLEClient* pclient) {
    connected = false;
    Serial.println("onDisconnect");
  }
};

/* =====================================================================================================================*/
bool connectToServer() {
    Serial.print("Forming a connection to ");
    Serial.println(myDevice->getAddress().toString().c_str());
    
    BLEClient*  pClient  = BLEDevice::createClient();
    Serial.println(" - Created client");

    pClient->setClientCallbacks(new MyClientCallback());

    // Connect to the remove BLE Server.
    pClient->connect(myDevice);  // if you pass BLEAdvertisedDevice instead of address, it will be recognized type of peer device address (public or private)
    Serial.println(" - Connected to server");

    /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/
    // Obtain a reference to the gearubg service in the remote BLE server.
    BLERemoteService* pRemoteService = pClient->getService(serviceGearingUUID);
    if (pRemoteService == nullptr) {
      Serial.print("Failed to find our service UUID: ");
      Serial.println(serviceGearingUUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(" - Found our gearing service");
    // Obtain a reference to the characteristic in the service of the remote BLE server.
    pRemoteCharacteristic = pRemoteService->getCharacteristic(charGearingUUID);
    if (pRemoteCharacteristic == nullptr) {
      Serial.print("Failed to find our characteristic UUID: ");
      Serial.println(charGearingUUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(" - Found our gearing characteristic");
    // Read the value of the characteristic.
    if(pRemoteCharacteristic->canRead()) {
      std::string value = pRemoteCharacteristic->readValue();
      Serial.print("The gearing characteristic value was: ");
      Serial.println(value.c_str());
    }
    if(pRemoteCharacteristic->canNotify())
    {
         pRemoteCharacteristic->registerForNotify(notifyCallbackGearing);
    }
    /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/
    // tilt
    // Obtain a reference to the tilt servicein the remote BLE server.
    pRemoteService = pClient->getService(serviceGradeUUID);
    if (pRemoteService == nullptr) {
      Serial.print("Failed to find our service UUID: ");
      Serial.println(serviceGearingUUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(" - Found our tilt service");
    // Obtain a reference to the characteristic in the service of the remote BLE server.
    pRemoteCharacteristic2 = pRemoteService->getCharacteristic(charGradeUUID);
    if (pRemoteCharacteristic2 == nullptr) {
      Serial.print("Failed to find our Tilt characteristic UUID: ");
      Serial.println(charGradeUUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(" - Found our tilt characteristic");
    // Read the value of the characteristic.
    if(pRemoteCharacteristic2->canRead()) {
      std::string value = pRemoteCharacteristic2->readValue();
      std::copy(value.begin(), value.end(), std::begin(arr));
      Serial.print("currentGrade or lock first read : ");
      calcTileAndLock(arr, value.size());
    }
    if(pRemoteCharacteristic2->canNotify()) {
         pRemoteCharacteristic2->registerForNotify(notifyCallbackGrade);       
    }
    /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/
    // Power
    // Obtain a reference to the power servicein the remote BLE server.
    pRemoteService = pClient->getService(servicePowerUUID);
    if (pRemoteService == nullptr) {
      Serial.print("Failed to find our Power UUID: ");
      Serial.println(servicePowerUUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(" - Found our Power service");
    // Obtain a reference to the characteristic in the service of the remote BLE server.
    pRemoteCharacteristic3 = pRemoteService->getCharacteristic(charPowerUUID);
    if (pRemoteCharacteristic3 == nullptr) {
      Serial.print("Failed to find our Power characteristic UUID: ");
      Serial.println(charPowerUUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(" - Found our Power characteristic");
    // Read the value of the characteristic.
    if(pRemoteCharacteristic2->canRead()) {
      std::string value = pRemoteCharacteristic2->readValue();
      std::copy(value.begin(), value.end(), std::begin(arr));
      Serial.print("currentGrade or lock first read : ");
      calcTileAndLock(arr, value.size());
    }
    if(pRemoteCharacteristic3->canNotify()) {
         pRemoteCharacteristic3->registerForNotify(notifyCallbackPower);       
    }
    /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/
    connected = true;
    return true;
}

/**------------------------------------------------------------------------------------------------------------------------
 * Scan for BLE servers and find the first one that advertises the service we are looking for.
 */
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
 /**
   * Called for each advertising BLE server.
   */
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    Serial.print("BLE Advertised Device found: ");
    Serial.println(advertisedDevice.toString().c_str());
    // We have found a device, let us now see if it contains the service we are looking for.
    if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(serviceGearingUUID)) {
      BLEDevice::getScan()->stop();
      myDevice = new BLEAdvertisedDevice(advertisedDevice);
      doConnect = true;
      doScan = true;
    } // Found our server
  } // onResult
}; // MyAdvertisedDeviceCallbacks


void setup() {
  Serial.begin(115200);
  Serial.println("Starting Arduino BLE Client application...");
  EEPROM.begin(EEPROM_SIZE);
  restoreEEPROMSettings();

  pinMode(BOTTOM_BUTTON_PIN, INPUT_PULLUP);
  pinMode(TOP_BUTTON_PIN, INPUT_PULLUP);

    // init tracking structure for avg power
  for (int i = 0; i++; i < maxSecsToAvg){
    avgBySecond[i] = 0;
  }
  BLEDevice::init("");
  tft.init();
  tft.setRotation(1);

  tft.setTextSize(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_SKYBLUE, TFT_BLACK);
  tft.drawString("Wahoo Kickr Display Dingus", 0, 0, 4);
  tft.drawString("Weight: " + String(inputWeightKGs) + " KGs    FTP: " + String(inputFTP) + " watts", 0,40, 2);
  tft.drawString("If no connection after 20 seconds,", 0,60,2);
  tft.drawString("try resetting your dingus", 0,80,2);
  tft.drawString("Connecting....", 0, 100, 4);
  tft.drawString("Jay Wheeler v" + String(cVERSION), 0, RESOLUTION_Y - 16, 2);
  

  // Retrieve a Scanner and set the callback we want to use to be informed when we
  // have detected a new device.  Specify that we want active scanning and start the
  // scan to run for 5 seconds.
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  pBLEScan->start(5, false);

  imgSprite.createSprite(RESOLUTION_X, RESOLUTION_Y);  // will be used for screen updates, prevents flicker
} // End of setup.

void restoreEEPROMSettings(){
  // these two are safe to read even on a virgin board, as 0 is a valid state
  powerAvgMode = EEPROM.read(0);
  intWattsOrWKG = EEPROM.read(1);
  // weight and ftp can be in the range 25 to 500, which is too big for one byte, so...
  inputWeightKGs = EEPROM.read(2);
  if (inputWeightKGs < minWeight ) inputWeightKGs = defaultWEIGHT;
  if (inputWeightKGs > maxWeight ) inputWeightKGs = defaultWEIGHT;
  inputFTP = EEPROM.read(3) * 5;
  if (inputFTP < minFTP) inputFTP = defaultFTP;
  if (inputFTP > maxFTP) inputFTP = defaultFTP;

}
void saveEEPROMSettings(){
  EEPROM.write(0,powerAvgMode);
  EEPROM.write(1,intWattsOrWKG);
  EEPROM.write(2,inputWeightKGs);
  // FTP must in be increments of 5
  // which allows dividing by 5, which will fit any reasonable
  // FTP value into a byte - these locations are only 8 bits
  EEPROM.write(3,int(inputFTP/5));
  EEPROM.commit();
}
/*========================================================================================================================*/
void loop() {
  
 
  // test graphics - change condition to 1 == 1 in the following and run the sketch with your kickr bike off to exercise the graphics
  // essentially a demo mode

  if (0 == 1) {
    numFrontGears = 2;
    numRearGears = 11;
    currentPower = 100;
    for (int fg = 1; fg < 3; fg++) {
      for (int rg = 1; rg < 12; rg++) {
        currentChainRing = fg;
        currentCassette = rg;
        numFrontGears = 2;
        numRearGears = 11;
        currentPower++;
        powerCumulativeTotal= 1000;
        powerObservations = 7;
        if (currentPower == 200) currentPower = 100;
        avgBySecond[0] = currentPower;
        avgBySecond[1] = currentPower + 5;
        avgBySecond[2] = currentPower + 10;
        currentGrade = rg; // why not?
        checkButtons();
        updateDisplayViaSprite();
        delay (500);
      }
    }
    // end demo loop
  } else {
  // NORMAL OPERATION, needs to connect to a kickr bike to do anything
  
    // If the flag "doConnect" is true then we have scanned for and found the desired
    // BLE Server with which we wish to connect.  Now we connect to it.  Once we are 
    // connected we set the connected flag to be true.
    if (doConnect == true) {
      
      tft.setTextColor(TFT_SKYBLUE, TFT_BLACK);
      if (connectToServer()) {
        //Serial.println("We are now connected to the BLE Server.");
        tft.drawString("Wahoo Services Connected.", 0, 78, 4);
        //tft.drawXBitmap(155, 0, zwiftlogo, 100, 100, TFT_BLACK, TFT_ORANGE);  
        delay(1000);
        tft.fillScreen(TFT_BLACK);
        //tft.drawXBitmap(155, 0, zwiftlogo, 100, 100, TFT_BLACK, TFT_ORANGE);
      } else {
        //Serial.println("We have failed to connect to the server; there is nothin more we will do.");
        tft.fillScreen(TFT_BLACK);
        tft.drawString("Unable to connect.", 0, 0, 4);
        tft.drawString("Try rebooting your dingus.", 0, 30, 4);
      }
      doConnect = false;
    }

    // If we are connected to a peer BLE Server, update the characteristic each time we are reached
    // with the current time since boot.
    if (connected) {
        String newValue = "Time since boot: " + String(millis()/1000);
      // Set the characteristic's value to be the array of bytes that is actually a string.
      pRemoteCharacteristic->writeValue(newValue.c_str(), newValue.length());
      updateDisplayViaSprite();

    }else if(doScan){
      BLEDevice::getScan()->start(0);  // this is just eample to start scan after disconnect, most likely there is better way to do it in arduino
    }
    checkButtons();
    delay(100); // Delay a 1/10 second
  } // normal loop
} // End of loop
/*========================================================================================================================*/
void updateDisplayViaSprite(){
  // the subroutines below update a screen sized "sprite" - the actual update to the physical screen
  // happens in one step at the end of this routine - this gets rid of flicker

  long currentSecond = long(millis()/1000);
  if (currentPower == 0 && currentSecond != lastPowerObservationSecond) {
    updateAvgPowerArray(0);
    lastPowerObservationSecond = currentSecond;
  }
  // note that the dingus can be in one of three modes - the normal mode, or when both buttons are pressed together
  // the mode to input weight followed by the mode to input FTP
  if (inputMode == inputNormalMode) {
    imgSprite.fillRect(0,0,RESOLUTION_X, RESOLUTION_Y, TFT_BLACK);
    imgSprite.setTextColor(TFT_SKYBLUE, TFT_BLACK);
    updateGrade();
    updateGearGraphic();
    updatePower();
    updateStats();
    drawDividers();
  }
  if (inputMode == inputWeightMode) {
    imgSprite.fillRect(0,0,RESOLUTION_X, RESOLUTION_Y, TFT_DARKGREEN);
    imgSprite.setTextColor(TFT_WHITE, TFT_DARKGREEN);
    imgSprite.drawString("Weight in KGs: " + String(inputWeightKGs), 10,10,4 );
    imgSprite.drawString("Use buttons to increase / decrease weight.", 20, 70, 2);
    imgSprite.drawString("Input range is " + String(minWeight) + " to " + String(maxWeight) + " KGs", 20, 90, 2);
    imgSprite.drawString("Press both buttons to move to FTP setting.", 20, 110, 2);

  }
  if (inputMode == inputFTPMode) {
    imgSprite.fillRect(0,0,RESOLUTION_X, RESOLUTION_Y, TFT_DARKCYAN);
    imgSprite.setTextColor(TFT_WHITE, TFT_DARKCYAN);
    imgSprite.drawString("FTP in Watts: " + String(inputFTP), 10,10,4 );
    imgSprite.drawString("Use buttons to increase / decrease FTP.", 20, 70, 2);
    imgSprite.drawString("Input range is " + String(minFTP) + " to " + String(maxFTP) + " watts", 20, 90, 2);
    imgSprite.drawString("Press both buttons to save settings", 20, 110, 2);
    imgSprite.drawString("and return to normal mode.", 20, 130, 2);
  }

  imgSprite.pushSprite(0,0);  // update the physical screen
}
/* end void loop()---------------------------------------------------------------------------------------------------------*/
void checkButtons() {
  /*
  #define inputNormalMode 0
  #define inputWeightMode 1
  #define inputFTPMode 2
  */

  int intBottomButtonState = digitalRead(BOTTOM_BUTTON_PIN); // watts or w/kg
  int intTopButtonState = digitalRead(TOP_BUTTON_PIN);

  // simultaneous button presses - toggles dingus thru the modes for normal uses, ftp input or weight input
  if ((intTopButtonState == LOW) && (intBottomButtonState == LOW) && (inputMode == inputNormalMode) ) {
    inputMode = inputWeightMode;
    delay(50);
  } else if ((intTopButtonState == LOW) && (intBottomButtonState == LOW) && (inputMode == inputWeightMode) ) {
    inputMode = inputFTPMode;
    delay(50);
  } else if ((intTopButtonState == LOW) && (intBottomButtonState == LOW) && (inputMode == inputFTPMode) ) {
    // exiting input mode, save state
    saveEEPROMSettings();
    inputMode = inputNormalMode;
    delay(50);
  } else {  //single or no press - meaning depends on mode
    if (intBottomButtonState == LOW)
    {
      if (inputMode == inputNormalMode) {
        // did use a bool, but switched to int to make saving state clearer
        // toggle watts or w/kg
        intWattsOrWKG++;
        if (intWattsOrWKG > 1) intWattsOrWKG = 0;
        // uncomment the following line if you want to persistently save power mode and watts/wkg changes each time they
        // are made - but be aware that ESP32 manufacturer only claims 10,000 writes - if you fiddle with these setting
        // throughout your rides, say, 10x per ride, and ride daily, you would only be looking at 3 years before you reach that 10k
        // otherwise, whatever settings are in effect for power mode and watts vs w/kg get save when you update your ftp / weight
        // saveEEPROMSettings()  
        delay(50); // only want this for normal mode, in input mode, we want a held button to register
      } 
      if (inputMode == inputWeightMode) { 
        // decrease weight
        inputWeightKGs--;
        inputWeightKGs = max(inputWeightKGs, minWeight);

      }
      if (inputMode == inputFTPMode) {
        //decrease ftp
        inputFTP = inputFTP - 5;
        inputFTP = max(inputFTP, minFTP);
      }
    }
        
    if (intTopButtonState == LOW)
    {
      if (inputMode == inputNormalMode) {
        // rotate instant, 3s or 10s power
        powerAvgMode++;
        if (powerAvgMode > 2) powerAvgMode = 0;
        // saveEEPROMSettings() // see comment above
        delay(50);  // only want this for normal mode, in input mode, we want a held button to register
      }
      if (inputMode == inputWeightMode){
        // increase weight
        inputWeightKGs++;
        inputWeightKGs = min(inputWeightKGs, maxWeight);
      } 
      if (inputMode == inputFTPMode){
        // increase ftp
        inputFTP = inputFTP + 5;
        inputFTP = min(inputFTP, maxFTP);
      }
    }
  } // end single or no buttom press
}
/* end void checkButtons()-----------------------------------------------------------------------------------*/
void updateStats() {
  // update the stats in the lower left corner, starting with timer since dingus boot
  //imgSprite.drawString( String(int(millis()/1000)) , 0, 150, 2); // but the time since boot in lower left corner in tiny type
  // update the small max and average power stats
  imgSprite.fillRect( 5, 100, RESOLUTION_X/2 - 10, 40, TFT_BLACK);
  imgSprite.drawString("Max Power: " + String(maxPower), 5, 100, 2);
  if (powerObservations > 0 ) imgSprite.drawString("Avg Power: " + String(long(powerCumulativeTotal/powerObservations)), 5, 115, 2);
  imgSprite.drawString("Weight: " + String(inputWeightKGs) + "  FTP: " + String(inputFTP), 5, 130, 2);
  long secsSinceStart = long(millis()/1000);
  int hours = int(secsSinceStart / 3600);
  int minutes = int((secsSinceStart / 60) % 60);
  int seconds = int(secsSinceStart % 60);
  char buffer[12];

  if (hours > 0) {
    sprintf(buffer, "%02d:%02d:%02d", hours, minutes, seconds);
  } else {
    sprintf(buffer, "%02d:%02d", minutes, seconds);
  }
  imgSprite.drawString("Elapsed Time: " + String(buffer) , 5, 150, 2); // but the time since boot in lower left corner in tiny type
}
/*-------------------------------------------------------------------------------------------------------------------*/
void drawDividers() {
  imgSprite.fillRect(RESOLUTION_X/2-8, 0,2, RESOLUTION_Y, TFT_DARKGREY); // draw the middle vertical divider
  imgSprite.fillRect(RESOLUTION_X/2-8, (POWERTEXT_Y) - 4, (RESOLUTION_X/2) + 8 - 20, 2, TFT_DARKGREY); // draw a right horizontal divider
  imgSprite.fillRect(RESOLUTION_X - 21, 0, 1 , RESOLUTION_Y, TFT_DARKGREY); // draw a left vertical divider
  imgSprite.fillRect(0, 96 , (RESOLUTION_X/2) + 8 - 20, 2, TFT_DARKGREY); // draw a left  horizontal divider
}
/*-------------------------------------------------------------------------------------------------------------------*/
void updateGearGraphic() {
  // Created 12/25/2023 - displays gears as a side view graphic, rather than top down.
  //              Includes primitive jockey ring animation
  // these should all be even numbers, since they get divided by 2
  #define rearGearX 28
  #define rearGearY 42
  #define rearMinSize 6
  #define rearGearSpacing 1
  #define frontGearX 110
  #define frontGearY 50
  #define frontMinSize  20
  #define frontGearSpacing 10
  #define jockeyWheelSize 4
  //#define numRearGears 11
  //#define numChainRings 2
  uint32_t fillColor = TFT_LIGHTGREY; // cog and jockey wheeler color
  // default chain color is green, but override for lowest gears
  uint32_t highLightColor = TFT_GREEN;
  if (currentChainRing == 1) highLightColor = TFT_SKYBLUE;
  if (currentCassette == 3 && currentChainRing == 1) highLightColor = TFT_YELLOW;
  if (currentCassette == 2 && currentChainRing == 1) highLightColor = TFT_ORANGE;
  if (currentCassette == 1 && currentChainRing == 1) highLightColor = TFT_RED;
  imgSprite.fillRect(0,0,(RESOLUTION_X/2) - 8, 95, TFT_BLACK);
  //gear text
  imgSprite.setTextColor(highLightColor, TFT_BLACK);
  imgSprite.drawString(String(currentChainRing) + ":" + String(currentCassette), 0,0, 4);  
  imgSprite.setTextColor(TFT_SKYBLUE, TFT_BLACK);
  // rear cassette
  for (int i = numRearGears - 1; i >= 0; i --){
      fillColor = TFT_LIGHTGREY;
      if (i == currentCassette - 1) fillColor = highLightColor;
      imgSprite.drawCircle(rearGearX, rearGearY,  ((numRearGears - i) * rearGearSpacing) + rearMinSize, fillColor);
      //delay(100);
  }
  // front chainrings
  for (int i = 0; i < numFrontGears; i ++){
      fillColor = TFT_LIGHTGREY;
      if (i == currentChainRing -1) fillColor = highLightColor;
      imgSprite.drawCircle(frontGearX, frontGearY, (i*frontGearSpacing) + frontMinSize, fillColor);
      imgSprite.drawCircle(frontGearX, frontGearY, (i*frontGearSpacing) + frontMinSize-1, fillColor);
      //delay(100);
  }
  // top chain - note I double this line up for visibility
  imgSprite.drawLine(rearGearX, rearGearY - (((numRearGears - currentCassette + 1) * rearGearSpacing) + rearMinSize)  - 1,   frontGearX,   frontGearY - (((currentChainRing -1) * frontGearSpacing) + frontMinSize) - 1, highLightColor);
  imgSprite.drawLine(rearGearX, rearGearY - (((numRearGears - currentCassette + 1) * rearGearSpacing) + rearMinSize),   frontGearX,   frontGearY - (((currentChainRing -1) * frontGearSpacing) + frontMinSize), highLightColor);
  
   //upper jockey wheel
  int upperJockeyX = rearGearX + (numRearGears * rearGearSpacing);
  int upperJockeyY = rearGearY + (numRearGears * rearGearSpacing) + rearMinSize + 4;
  imgSprite.drawCircle(upperJockeyX, upperJockeyY, jockeyWheelSize, TFT_LIGHTGREY);
  // chain rear gear to upper jockey wheel, doubled
  imgSprite.drawLine(rearGearX, rearGearY + (rearMinSize/2) + (((numRearGears -(currentCassette -1))* rearGearSpacing)) + 3  , upperJockeyX, upperJockeyY - (jockeyWheelSize / 2) - 2, highLightColor);
  imgSprite.drawLine(rearGearX, rearGearY + (rearMinSize/2) + (((numRearGears -(currentCassette -1))* rearGearSpacing)) + 2  , upperJockeyX, upperJockeyY - (jockeyWheelSize / 2) - 1, highLightColor);
  // lower jockey wheel - this one moves according to gear combo
  int lowerJockeyX = upperJockeyX - numRearGears +  (2 * (numRearGears - currentCassette));
  int lowerJockeyY = upperJockeyY + (jockeyWheelSize * 2);
  if (currentChainRing == 1) lowerJockeyY = lowerJockeyY + jockeyWheelSize;
  imgSprite.drawCircle(lowerJockeyX, lowerJockeyY, jockeyWheelSize, TFT_LIGHTGREY);
  // upper to lower jockey wheel, doubled
  imgSprite.drawLine(upperJockeyX, upperJockeyY + (jockeyWheelSize/2) + 2, lowerJockeyX, lowerJockeyY - (jockeyWheelSize/2) - 2, highLightColor);
  imgSprite.drawLine(upperJockeyX, upperJockeyY + (jockeyWheelSize/2) + 1, lowerJockeyX, lowerJockeyY - (jockeyWheelSize/2) - 1, highLightColor);
  // lower jockey wheel to bottom of front gear, doubled up again for visibility
  imgSprite.drawLine(lowerJockeyX, lowerJockeyY + jockeyWheelSize + 1,   frontGearX,   frontGearY + (((currentChainRing -1) * frontGearSpacing) + frontMinSize) + 1, highLightColor);
  imgSprite.drawLine(lowerJockeyX, lowerJockeyY + jockeyWheelSize,   frontGearX,   frontGearY + (((currentChainRing -1) * frontGearSpacing) + frontMinSize), highLightColor);
}

/*------------------------------------------------------------------------------------------------------------*/
void updateGrade(void) {
  // if the bike is locked, display that in red, otherwise display the grade
  if (tilt_lock) {
    imgSprite.fillRect(RESOLUTION_X/2, 0, RESOLUTION_X - int(RESOLUTION_X/2), int(RESOLUTION_X/2), TFT_BLACK);
    imgSprite.setTextColor(TFT_RED, TFT_BLACK);
    imgSprite.drawString("Tilt",int(RESOLUTION_X/2), 0, 4);
    imgSprite.drawString("Locked",int(RESOLUTION_X/2), 25, 4);
    imgSprite.drawString("    ", 240, 50, 2);
  } else {
      uint32_t gradeColor = TFT_SKYBLUE;
      if (currentGrade < -2) {
        gradeColor = TFT_GREEN;
      } else if (currentGrade < 3) {
        gradeColor = TFT_SKYBLUE;
      } else if (currentGrade < 8) {
        gradeColor = TFT_YELLOW;
      } else if (currentGrade < 13) {
        gradeColor = TFT_ORANGE;
      } else {
        // greater than 14
        gradeColor = TFT_RED;
        // may want to add blinking red or other distinguishing
        // above 18 %?
      }
      char buffer[7];
      dtostrf(currentGrade, 5,1, buffer);
      imgSprite.fillRect(RESOLUTION_X/2, 0, RESOLUTION_X - int(RESOLUTION_X/2), int(RESOLUTION_X/2), TFT_BLACK);
      imgSprite.setTextColor(gradeColor, TFT_BLACK);
      imgSprite.drawString(String(buffer), int(RESOLUTION_X/2), 0, 7);
      imgSprite.drawString("% Grade", 240, 50, 2);
    }
  imgSprite.setTextColor(TFT_SKYBLUE, TFT_BLACK); // reset text color
}
/*------------------------------------------------------------------------------------------------------------*/
void updatePower(void) {

      float p;
      imgSprite.fillRect(RESOLUTION_X/2, POWERTEXT_Y, RESOLUTION_X - RESOLUTION_X/2, RESOLUTION_Y - POWERTEXT_Y, TFT_BLACK);
      
      String unitPrefix = "Inst.  ";
      p = currentPower;
      if (powerAvgMode == 1) {
        unitPrefix = "3s  ";
        p = (avgBySecond[0] + avgBySecond[1] + avgBySecond[2])/3;
      } else if (powerAvgMode == 2) {
        unitPrefix = "10s  ";
        // this is ugly, but for loop addition was causing problems for some reason...
        p = (avgBySecond[0] + avgBySecond[1] + avgBySecond[2] + avgBySecond[3] + avgBySecond[4] + avgBySecond[5] + avgBySecond[6] + avgBySecond[7] + avgBySecond[8] + avgBySecond[9])/10;
      }

      //figure out power zone, based on algorithm from cyclistshub.com
      String powerZone = "--";
      String powerDesc = "--";

      uint32_t zChartColor = TFT_DARKGREEN;
    
      float flPercent = 100 * p / float(inputFTP);
      // note that in the switch logic below, I pad out the powerDesc string to 19
      // places - this allows it to redraw without artifacts and without resorting
      // to fillRect() approach, which causes flickering
      if (flPercent > 150) {
        powerZone = "Z7"; // 
        powerDesc = "Neuromuscular Power";
        zChartColor = 0xF8C0; // red
      } else if (flPercent > 120) {
        powerZone = "Z6"; //
        powerDesc = "Anaerobic Capacity";
        zChartColor = 0xEC00; // yellow red
      } else if (flPercent > 105) {
        powerZone = "Z5";  // 
        powerDesc = "VO2 Max";
        zChartColor = 0xF740; // Yellow
      } else if (flPercent > 90) {
        powerZone = "Z4"; // 
        powerDesc = "Lactate Threshold";
        zChartColor = 0xB6A0; // Yellow Green
      } else if (flPercent > 75) {
        powerZone = "Z3"; // 
        powerDesc = "Tempo";
        zChartColor = 0x0EE0; // Brighter green
      } else if (flPercent > 55) {
        powerZone = "Z2"; // 
        powerDesc = "Endurance";
        zChartColor = 0x0707; // light green
      } else if (flPercent > 0) {
        powerZone = "Z1"; // 
        powerDesc = "Active Recovery";
        zChartColor = 0x077D; // blue green
      } else {
        powerZone = "";
        powerDesc = "Coasting/Stopped";
        zChartColor = TFT_DARKGREY;
      }

      imgSprite.setTextColor(zChartColor, TFT_BLACK);
      
      //Serial.println("powerZone:" + powerZone + " p:" + String(p));
      

      float scalingFactor = RESOLUTION_Y / (1.5 * inputFTP);
      int chartHeight = int(scalingFactor * p);
      chartHeight = min(RESOLUTION_Y, chartHeight);
      chartHeight = max(1, chartHeight); // in case power is zero

      // deal with the ftp power bar display on the far right
      imgSprite.fillRect(RESOLUTION_X - 20, 0, 20, RESOLUTION_Y, TFT_BLACK);
      imgSprite.fillRect(RESOLUTION_X - 20, RESOLUTION_Y - chartHeight, 20, chartHeight, zChartColor);

      

      //imgSprite.drawString(powerZone, RESOLUTION_X - 20, RESOLUTION_Y - 20, 2);
      imgSprite.drawString(powerZone + " " + powerDesc, RESOLUTION_X/2, RESOLUTION_Y-20, 2);
      // Button Toggle
      char buffer[7];
      if (intWattsOrWKG == 0) {
        //Watts mode
        dtostrf(p, 5,0,buffer);
        //Serial.println("Buffer:" + String(buffer) + ":");
        imgSprite.drawString(String(buffer), RESOLUTION_X/2 - 10,70, 7);
        imgSprite.drawString(unitPrefix + "Watts",RESOLUTION_X/2, 120, 4); 
      }
      else
      {
        // W/KG mode
        float flPowerToWeight = p/float(inputWeightKGs);
        //Serial.println("W/KG" + String(flPowerToWeight));
        dtostrf(flPowerToWeight, 5,2,buffer);
        //Serial.println("Buffer:" + String(buffer) + ":");
        imgSprite.drawString(String(buffer), RESOLUTION_X/2 - 10,70, 7);
        imgSprite.drawString(unitPrefix + "W/KG",RESOLUTION_X/2, 120, 4);
      }
  imgSprite.setTextColor(TFT_SKYBLUE, TFT_BLACK);
}
/*---------------------------------------------------------------------------------------------*/
// roll second averages down, bumping off the older and adding the newest average second observation
void updateAvgPowerArray(float newestAvgSec) {
  for (int i = maxSecsToAvg - 1; i--; i > 0){
        avgBySecond[i] = avgBySecond[i - 1];
      }
    avgBySecond[0] = newestAvgSec;
}
/*------------------------------------------------------------------------------------------------------------*/
void calcTileAndLock(uint8_t *pData, size_t length){
  /*for (int i = 0; i < length; i++) {
    Serial.print(pData[i], HEX);
    Serial.print(" "); //separate values with a space
  }
  Serial.println("");
  */
    //lock, unlock update
  if(length == 3 && pData[0] == 0xfd && pData[1] == 0x33) {
    tilt_lock = pData[2] == 0x01;
  }

  //currentGrade update
  if(length == 4 && pData[0] == 0xfd && pData[1] == 0x34){
    char s[10] = {};
    if(pData[3] < 0x80)
    {
      currentGrade = (float)(pData[3] << 8 | pData[2]) / 100;
    }
    else // negative grade
    {
      uint16_t tmp16 = 0xffff - (pData[3] << 8 | pData[2]);
      currentGrade = -1 * (float)tmp16 / 100;
    }
  }
}
/*-------------------------------------------------------------------------------------------------------------
  These last sections handle the callbacks when bluetooth sends an updated power, grade, lock or gearing update
  Bad idea to have serial outputs in these routines, as these get called constantly and the serial write is 
  slow.    If you turn on debug output, turns them back off when finished.
------------------------------------------------------------------------------------------------------------*/
static void notifyCallbackPower(BLERemoteCharacteristic *pBLERemoteCharacteristic, uint8_t *pData, size_t length, bool isNotify)
{
  uint16_t tmp16 = (pData[3] << 8 | pData[2]);
  currentPower = (float)(tmp16);
  long currentSecond = long(millis()/1000);
  if (currentPower > maxPower) maxPower = currentPower;

  if (currentPower > 0) {
    // are we still tracking within the "current" second - if so update intra second #s
    if (lastPowerObservationSecond == currentSecond ) {
      intraSecondObservations++;
      intraSecondCumulativeTotal += long(tmp16);
    } else {
      // we have moved onto a new second - bump the second track of power observations, including our structure for calculating moving avg

      powerObservations++;
      if (intraSecondObservations > 0) {
        powerCumulativeTotal += float(intraSecondCumulativeTotal/intraSecondObservations);
        updateAvgPowerArray(intraSecondCumulativeTotal/intraSecondObservations);
      } else {
        updateAvgPowerArray(0);
      }
      // reset intra second varaibles
      intraSecondObservations = 1;
      intraSecondCumulativeTotal = long(tmp16);
      lastPowerObservationSecond = currentSecond;
    }
  }
}
/*------------------------------------------------------------------------------------------------------------*/
static void notifyCallbackGrade(
  BLERemoteCharacteristic* pBLERemoteCharacteristic2,
  uint8_t* pData,
  size_t length,
  bool isNotify) {

  //Serial.print("noty2 : ");
  /*for(int i=0; i < length; i++) {
    Serial.print(String(i)+":");
    Serial.println(String(pData[i]));
  }*/

  calcTileAndLock(pData, length); 
}
/*------------------------------------------------------------------------------------------------------------*/
static void notifyCallbackGearing(
  BLERemoteCharacteristic* pBLERemoteCharacteristic,
  uint8_t* pData,
  size_t length,
  bool isNotify) {
  // Gears
  // 2 ... # Selected Gear in Front
  // 3 ... # Selected Gear in Rear
  // 4 ... # Gears in Front
  // 5 ... # Gears in Rear

  if(length < 5) // brake garbage data
    return ;

  currentChainRing = (int)(1+pData[2]);
  currentCassette = (int)(1+pData[3]);
  numFrontGears = int(pData[4]);
  numRearGears = int(pData [5]);
}
/*------------------------------------------------------------------------------------------------------------*/
