# define cVERSION "20240107"
# define demoMode false // true or false, if true put dingus in a demo loop, and will not care if kickr is available

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
12/31/2023 - factored / simplified the power code using arrays.
          Enhanced the power chart with zone markers, markers for avg and max power.
          Max / Avg power in stats now color codes based on zone, and toggles between watts / wkg like the main power display
          Improved the demo loop.
          Changed the power zone thresholds to match Zwift, including color coding.   Added option for Rouvy power zones.
01/03/2024 - the power model toggle now include % FTP - so it cycles based on the bottom button from Watts, WKg, % FTP. 
           -checks that settings have actually changed before writing to eeprom.
           - added logic to prevent accidentally cycling from the last input setting screen directly back into the first
           - added logic that detects when no power input (presumably you are coasting or finished cycling) has been
             received for three seconds, and when that happens, toggles a summary screen with max and avg power 
             (in your choice of Watts/WKg/% FTP) in large print.  Regular display resumes when you start pedaling.
01/07/2024 - extended logic to keep track of max and avg for instant, 3 second and 10 second.  Reorganized screen layout to put the
             power chart in the center, gear graphic in the lower left, grade in the upper left and power plus avg/max as the entire
             right side.

**********************************************************************************************************/

#include "BLEDevice.h"
#include <TFT_eSPI.h> // Graphics and font library for ST7735 driver chip
#include <SPI.h>
#include <EEPROM.h> // to store settings between power cycles
#define EEPROM_SIZE 6 // 1 byte for power mode (watts or w/kg), 1 byte for avg mode (instant, 3s, 10s), 2 bytes each for weight and ftp
TFT_eSPI tft = TFT_eSPI();  // Invoke library, pins defined in User_Setup.h
// define a sprite to cover the whole screen, avoids flickr - s
TFT_eSprite imgSprite = TFT_eSprite(&tft);

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
int currentPower = 0;
int currentChainRing = 1;
int currentCassette = 1;
int numFrontGears = 1;
int numRearGears = 1;

uint8_t arr[32];
int tilt_lock = 0;
#define powerWatts 0
#define powerWKG 1
#define powerFTP 2
int intWattsOrWKgOrFTP = powerWatts; // 0 = watts, 1 = W/KG, 2 = FTP
#define avgInstant 0
#define avg3Secs 1
#define avg10Secs 2
int  powerAvgMode = avg3Secs; // 0 = instant 1 = 3s, 2 = 10s
int maxPowerArray[3]; // keep track of max power for instant, 3s and 10s
long powerObservationsArray[3];
float powerCumulativeTotalArray[3];

long lastPowerObservationSecond = 0;
long lastNonZeroPower = 0;
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


// FTP Power Zone Stuff
// power zones, based on Zwift - see https://zwiftinsider.com/power-zone-colors
#define numPowerZones 6
int powerZoneThreshold[numPowerZones] = {0, 59, 75, 89, 104, 118};
String powerZoneDescription[numPowerZones] = {"Z1 Active Recovery", "Z2 Endurance", "Z3 Tempo", "Z4 Lactate Threshold", "Z5 VO2 Max", "Z6 Anaerobic "};
uint32_t powerZoneColor[numPowerZones] = {TFT_LIGHTGREY,TFT_BLUE,TFT_GREEN,TFT_YELLOW,TFT_ORANGE,TFT_RED};

// if you want to use Rouvy zones, uncomment the following, and comment out the zwift definitions above
// power zones based on Rouvy - see https://support.rouvy.com/hc/en-us/articles/360018830597-Power-Zones
//#define numPowerZones 7
//int powerZoneThreshold[numPowerZones] = {0, 55, 75, 90, 105, 120, 150};
//String powerZoneDescription[numPowerZones] = {"Z1 Active Recovery", "Z2 Endurance", "Z3 Tempo", "Z4 Lactate Threshold", "Z5 VO2 Max", "Z6 Anaerobic ", "Z7 Neuromuscular"};
//uint32_t powerZoneColor[numPowerZones] = {TFT_PURPLE,TFT_BLUE,TFT_CYAN,TFT_GREEN,TFT_GREENYELLOW,TFT_ORANGE,TFT_RED};


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
  for (int i = 0; i < maxSecsToAvg; i++){
    avgBySecond[i] = 0;
  }
  // init max power array
  for (int i = 0; i < 3; i++) maxPowerArray[i] = 0;

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


/*========================================================================================================================*/
void loop() {
  
 
  // test graphics - change condition to 1 == 1 in the following and run the sketch with your kickr bike off to exercise the graphics
  // essentially a demo mode

  if (demoMode) {
    Serial.println("Top of demo loop");
    numFrontGears = 2;
    numRearGears = 11;

    
    //currentPower = 50;
    for (int fg = 1; fg < 3; fg++) {
      for (int rg = 1; rg < 12; rg++) {
        currentChainRing = fg;
        currentCassette = rg;
        numFrontGears = 2;
        numRearGears = 11;
        if (rg % 2 == 0) {
          currentPower += 30;
        } else {
          currentPower -= 15;
        }
        if (currentPower > int(1.6 * inputFTP)) currentPower = 50;
        maxPowerArray[avgInstant] = max(int(maxPowerArray[avgInstant]),int(currentPower));
        maxPowerArray[avg3Secs] = int(maxPowerArray[avgInstant] * 0.9);
        maxPowerArray[avg10Secs] = int(maxPowerArray[avgInstant] * 0.82);
        powerCumulativeTotalArray[avgInstant]= 1000;
        powerObservationsArray[avgInstant] = 7;
        powerCumulativeTotalArray[avg3Secs]= 900;
        powerObservationsArray[avg3Secs] = 7;
        powerCumulativeTotalArray[avg10Secs]= 850;
        powerObservationsArray[avg10Secs] = 7;
        for (int i = 0; i < 10; i++) avgBySecond[i] = currentPower + 2;
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
        tft.drawString("Wahoo Services Connected.", 0, 100, 4);
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
void restoreEEPROMSettings(){
  // these two are safe to read even on a virgin board, as 0 is a valid state
  powerAvgMode = EEPROM.read(0);
  intWattsOrWKgOrFTP = EEPROM.read(1);
  // weight and ftp can be in the range 25 to 500, which is too big for one byte, so...
  inputWeightKGs = EEPROM.read(2);
  if (inputWeightKGs < minWeight ) inputWeightKGs = defaultWEIGHT;
  if (inputWeightKGs > maxWeight ) inputWeightKGs = defaultWEIGHT;
  inputFTP = EEPROM.read(3) * 5;
  if (inputFTP < minFTP) inputFTP = defaultFTP;
  if (inputFTP > maxFTP) inputFTP = defaultFTP;
}
/*-------------------------------------------------------------------------------------------------------------------*/
void saveEEPROMSettings(){

  int oldpowerAvgMode = EEPROM.read(0);
  int oldintWattsOrWKgOrFTP = EEPROM.read(1);
  int oldinputWeightKGs = EEPROM.read(2);
  int oldinputFTP = EEPROM.read(3) * 5;

  if (oldpowerAvgMode != powerAvgMode || oldintWattsOrWKgOrFTP != intWattsOrWKgOrFTP
      || oldinputWeightKGs != inputWeightKGs || oldinputFTP != inputFTP) {
        EEPROM.write(0,powerAvgMode);
        EEPROM.write(1,intWattsOrWKgOrFTP);
        EEPROM.write(2,inputWeightKGs);
        // FTP must in be increments of 5
        // which allows dividing by 5, which will fit any reasonable
        // FTP value into a byte - these locations are only 8 bits
        EEPROM.write(3,int(inputFTP/5));
        EEPROM.commit();
      }
}
/*-------------------------------------------------------------------------------------------------------------------*/
void updateDisplayViaSprite(){
  // the subroutines below update a screen sized "sprite" - the actual update to the physical screen
  // happens in one step at the end of this routine - this gets rid of flicker

  long currentSecond = long(millis()/1000);

  //  NORMAL MODE - not updating ftp or weight

  if (currentPower == 0 && currentSecond != lastPowerObservationSecond) {
    updateAvgPowerArray(0);
    lastPowerObservationSecond = currentSecond;
  }
  // note that the dingus can be in one of three modes - the normal mode, or when both buttons are pressed together
  // the mode to input weight followed by the mode to input FTP
  if (inputMode == inputNormalMode) {
    
    setDefaultTextColor();
    imgSprite.fillRect(0,0,RESOLUTION_X, RESOLUTION_Y, TFT_BLACK);
    if (currentSecond > lastNonZeroPower + 3 and ! demoMode) {
      // it has been at least 3 sconds since the last non-zero power, so show the summary screen
      
      updateCoastingStats();
    } else {
      // we are cranking away
      updateGrade();
      updateGearGraphic();
      updatePower();
      //updateStats();
      drawDividers();
    }
  }

  // UPDATING WEIGHT MODE

  if (inputMode == inputWeightMode) {
    imgSprite.fillRect(0,0,RESOLUTION_X, RESOLUTION_Y, TFT_DARKGREEN);
    imgSprite.setTextColor(TFT_WHITE, TFT_DARKGREEN);
    imgSprite.drawString("Weight in KGs: " + String(inputWeightKGs), 10,10,4 );
    imgSprite.drawString("Use buttons to increase / decrease weight.", 20, 70, 2);
    imgSprite.drawString("Input range is " + String(minWeight) + " to " + String(maxWeight) + " KGs", 20, 90, 2);
    imgSprite.drawString("Press both buttons to move to FTP setting.", 20, 110, 2);

  }

    // UPDATING FTP MODE

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
  static long lastInputToggleTime;


  int intBottomButtonState = digitalRead(BOTTOM_BUTTON_PIN); // watts or w/kg
  int intTopButtonState = digitalRead(TOP_BUTTON_PIN);

  // simultaneous button presses - toggles dingus thru the modes for normal uses, ftp input or weight input
  if ((intTopButtonState == LOW) && (intBottomButtonState == LOW)) {
    long currentTime = millis();
    // make sure it has been at least three seconds since the last toggle between setting inputs, so we don't 
    // chase out tail immediately toggling back to inputs
    if (currentTime > lastInputToggleTime + 3000) {
      lastInputToggleTime = currentTime;

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
      }
    } // currentTime > lastinputToggleTime + 3000
  } else {  //single or no press - meaning depends on mode
    if (intBottomButtonState == LOW)
    {
      if (inputMode == inputNormalMode) {
        // toggles thru Watts, W/KG FTP
        intWattsOrWKgOrFTP++;
        if (intWattsOrWKgOrFTP > powerFTP) intWattsOrWKgOrFTP = powerWatts;
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

/*-----------------------------------------------------------------------------------------------------------*/
void updateCoastingStats() {
  // draw an outer board based on the max power, with a nested inner border based on the average
  imgSprite.fillRect(0,0,RESOLUTION_X, RESOLUTION_Y, getColorBasedOnFTPPct(100 * maxPowerArray[powerAvgMode] /inputFTP));
  if (powerObservationsArray[powerAvgMode] > 0) {
    imgSprite.fillRect(3,3,RESOLUTION_X - 3, RESOLUTION_Y - 3, getColorBasedOnFTPPct(100 * avgDurationPower()/inputFTP));
  }

  // inset a smaller rectangle
  imgSprite.fillRect(7, 7, RESOLUTION_X - 14, RESOLUTION_Y - 14, TFT_BLACK);
  setTextColorBasedOnFTPPct(100 * maxPowerArray[powerAvgMode] /inputFTP);
  imgSprite.drawString("Max " + getDurationMode() + ": "  + getPowerInSelectedUnits(maxPowerArray[powerAvgMode]) + " " + getWattsOrWKGLabel() , 14, 10, 4);

  setTextColorBasedOnFTPPct(100 * avgDurationPower()/inputFTP);
  imgSprite.drawString("Avg " + getDurationMode() + ": " + getPowerInSelectedUnits(avgDurationPower()) + " " + getWattsOrWKGLabel(), 14, 50, 4);

  setDefaultTextColor();
  // pedaling time should always be instant
  imgSprite.drawString("Pedaling: " + secondsToFormattedTime(powerObservationsArray[avgInstant]), 14, 90, 4);
  imgSprite.drawString("Weight: " + String(inputWeightKGs) + "  FTP: " + String(inputFTP), 14, 130, 4);
}


/*-------------------------------------------------------------------------------------------------------------------*/
int avgDurationPower() {
  // returns the average instant, 3s or 10s power, depending on selected mode.  Returns 0 if no observations yet
  // for selected mode
  if (powerObservationsArray[powerAvgMode] == 0) {
    return 0;
  } else {
    return (powerCumulativeTotalArray[powerAvgMode]/powerObservationsArray[powerAvgMode]);
  }

}
/*-------------------------------------------------------------------------------------------------------------------*/
String secondsToFormattedTime(long secs) {
  int hours = int(secs / 3600);
  int minutes = int((secs / 60) % 60);
  int seconds = int(secs % 60);
  char buffer[12];
  if (hours > 0) {
    sprintf(buffer, "%02d:%02d:%02d", hours, minutes, seconds);
  } else {
    sprintf(buffer, "%02d:%02d", minutes, seconds);
  }
  return String(buffer);
}
void drawDividers() {
  imgSprite.drawLine(RESOLUTION_X/2 - 10, 0, RESOLUTION_X/2 - 10, RESOLUTION_Y, TFT_DARKGREY); //draw left border of chart
  imgSprite.drawLine(RESOLUTION_X/2 + 10, 0, RESOLUTION_X/2 + 10, RESOLUTION_Y, TFT_DARKGREY); // draw right border of chart
  
  imgSprite.drawLine(0, 68, RESOLUTION_X/2 - 10, 66, TFT_DARKGREY); // draw a left  horizontal divider
  imgSprite.drawLine(RESOLUTION_X/2 + 10 + 40, 96, RESOLUTION_X - 40, 96, TFT_DARKGREY); // draw a right partial horizontal divider
}
/*-------------------------------------------------------------------------------------------------------------------*/
void updateGearGraphic() {
  // Created 12/25/2023 - displays gears as a side view graphic, rather than top down.
  //              Includes primitive jockey ring animation
  // these should all be even numbers, since they get divided by 2
  // 1/5/2024 - moved to lower right

  #define rearGearX 28
  #define rearGearY 118 // was 42
  #define rearMinSize 6
  #define rearGearSpacing 1
  #define frontGearX 110
  #define frontGearY 128 // was 50
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
  //gear text
  imgSprite.setTextColor(highLightColor, TFT_BLACK);
  imgSprite.drawString(String(currentChainRing) + ":" + String(currentCassette), 0, 72, 4);  
  setDefaultTextColor();
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
  //Grade moved to upper right corner
  // if the bike is locked, display that in red, otherwise display the grade
  if (tilt_lock) {
    imgSprite.setTextColor(TFT_RED, TFT_BLACK);
    imgSprite.drawString("Tilt",0, 0, 4);
    imgSprite.drawString("Locked",0, 25, 4);
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
        // greater than 13
        gradeColor = TFT_RED;
        // may want to add blinking red or other distinguishing
        // above 13 %?
      }
      char buffer[7];
      dtostrf(currentGrade, 5,1, buffer);
      imgSprite.setTextColor(gradeColor, TFT_BLACK);
      imgSprite.drawString("% Grade", 0, 0, 2);
      imgSprite.drawString(String(buffer), 0, 16, 7);
      
    }
  setDefaultTextColor(); // reset text color
}
/*------------------------------------------------------------------------------------------------------------*/
void updatePower(void) {
    // moved to upper left
      
      
      String unitPrefix = getDurationMode();
      int p;
      p = 0;
      if (powerAvgMode == avg3Secs) {
        for (int i = 0; i < 3; i++) p = p + avgBySecond[i];
        p = p / 3;
      } else if (powerAvgMode == avg10Secs) {
        for (int i = 0; i < 10; i++) p = p + avgBySecond[i];
        p = p / 10;
      } else {
        // instant power
        p = currentPower;
      }
      // convert whichever type of power is being displayed to FTP percent      
      int ftpPct = int((100 * p) / inputFTP);
      String powerDesc = getPowerZoneDesc(ftpPct);
      setTextColorBasedOnFTPPct(ftpPct);
      imgSprite.drawString(powerDesc, RESOLUTION_X/2 + 18, 0, 2);

      imgSprite.drawString(getPowerInSelectedUnits(p), RESOLUTION_X/2  + 16 ,14, 7);
      imgSprite.drawString(unitPrefix + getWattsOrWKGLabel() ,RESOLUTION_X/2 + 24 , 64, 4); 
      

      // MAX and AVG Power
      setTextColorBasedOnFTPPct(100 * maxPowerArray[powerAvgMode]/inputFTP);
      imgSprite.drawString("Max:" + getPowerInSelectedUnits(maxPowerArray[powerAvgMode]) , RESOLUTION_X/2  + 24, 100, 4);

      //

      setTextColorBasedOnFTPPct(100 * avgDurationPower()/inputFTP);
      imgSprite.drawString("Avg:" + getPowerInSelectedUnits(avgDurationPower()), RESOLUTION_X/2  + 24, 124, 4);

      setDefaultTextColor();
      //imgSprite.drawString("Weight: " + String(inputWeightKGs) + "  FTP: " + String(inputFTP), 5, 130, 2);
      long secsSinceStart = long(millis()/1000);
      
      imgSprite.drawString("Elapsed: " + secondsToFormattedTime(secsSinceStart) , RESOLUTION_X/2  + 24, RESOLUTION_Y - 15, 2); // but the time since boot in lower left corner in tiny type





      drawPowerChart(ftpPct);
  setDefaultTextColor();
}
/*---------------------------------------------------------------------------------------------*/

String getDurationMode(){
       if (powerAvgMode == avg3Secs) {
        return ("3s");
      } else if (powerAvgMode == avg10Secs) {
        return ("10s");
      } else {
        // instant power
        return ("Inst.");
      }
}
/*---------------------------------------------------------------------------------------------*/
String getPowerInSelectedUnits(int p) {
  //expects p to be power, returns formatted power either in watts or w/kg based on the 
  // user toggled selection from the lower button
  char buffer[7];
  float ratio;
  if (intWattsOrWKgOrFTP == powerWatts) {
    //Watts mode
    dtostrf(p, 5,0,buffer);
  } else if (intWattsOrWKgOrFTP == powerWKG) {
    // W/KG mode
    ratio = p/float(inputWeightKGs);
    dtostrf(ratio, 5,2,buffer);
  } else {
    //FTP mode
    ratio = 100 * p/float(inputFTP);
    dtostrf(ratio, 5,0,buffer);

  }
  return String(buffer);
}
/*---------------------------------------------------------------------------------------------*/
String getWattsOrWKGLabel(){
  if (intWattsOrWKgOrFTP == powerWatts) {
    return "Watts";
  } else if (intWattsOrWKgOrFTP == powerWKG) {
    return "W/KG";
  } else {
    // FTP mode
    return "% FTP";
  }
}
/*---------------------------------------------------------------------------------------------*/
void drawPowerChart(int ftpPct) {
  // 1/5/2024 - moved to center of display
  //Serial.println("drawPowerChart, ftpPct:" + String(ftpPct));
  // expects power as integer pct of FTP
  uint32_t zChartColor = getColorBasedOnFTPPct(ftpPct);
  int h = getChartHeightBasedOnFTP(ftpPct);
  // deal with the ftp power bar display on the far right
  imgSprite.fillRect(RESOLUTION_X/2 - 10, 0, 20, RESOLUTION_Y, TFT_BLACK);
  imgSprite.fillRect(RESOLUTION_X/2 - 10, RESOLUTION_Y - h, 20, h, zChartColor);

  // draw indicators for the power thresholds, we don't need the 0th threshold
  for (int i = 1; i < numPowerZones; i++) setPowerZoneIndicator(powerZoneThreshold[i]);

  // now set a triangle markers for max power and average power
  // convert max power (in watts) to ftp pct to be compatible with the chart
  // max triangle is point down
  int topY = max(RESOLUTION_Y - getChartHeightBasedOnFTP(int((100 * maxPowerArray[powerAvgMode])/inputFTP)),0);
  int bottomY = min(topY + 10, RESOLUTION_Y);
  imgSprite.fillTriangle(RESOLUTION_X/2 - 8, topY, RESOLUTION_X/2 + 8, topY, RESOLUTION_X/2, bottomY, getColorBasedOnFTPPct(int((100 * maxPowerArray[powerAvgMode])/inputFTP)));

  if (powerObservationsArray[powerAvgMode] > 0){
    // avg power (in motion) is powerCumulativeTotalArray[powerAvgMode]/powerObservationsArray[powerAvgMode] - convert to FTP to work with the chart
    // average triangle is point up
    topY = max(RESOLUTION_Y - getChartHeightBasedOnFTP(int(100 * avgDurationPower()/inputFTP)), 0);
    bottomY = min(topY + 10, RESOLUTION_Y);
    if (avgDurationPower() > currentPower) {
      imgSprite.fillTriangle(RESOLUTION_X/2 - 8, bottomY, RESOLUTION_X/2 + 8, bottomY, RESOLUTION_X/2, topY, 
        getColorBasedOnFTPPct(int(100 * avgDurationPower()/inputFTP)));
    } else {
      // currentPower is greater than average power, which means the average marker will be superimposed on the power bar - so make it black to standout
      imgSprite.fillTriangle(RESOLUTION_X/2 - 8, bottomY, RESOLUTION_X/2 + 8, bottomY, RESOLUTION_X/2, topY, TFT_BLACK);
    }
  }
  
}
/*---------------------------------------------------------------------------------------------*/
void setPowerZoneIndicator(int ftpPct) {
  //Serial.println("setPowerZoneIndicator, ftpPct:" + String(ftpPct));
  // draw a pair of marking indicators, 4 pixels long on the left and right side of the power chart
  // 1/5/2024 - moved to center of dingus, with the power chart centered on resolution_x/2 +- 10 pixels
  int y = RESOLUTION_Y - getChartHeightBasedOnFTP(ftpPct);
  imgSprite.drawLine(RESOLUTION_X/2 - 10, y, RESOLUTION_X/2 - 7, y, getColorBasedOnFTPPct(ftpPct));
  imgSprite.drawLine(RESOLUTION_X/2 + 7, y, RESOLUTION_X/2 + 10, y, getColorBasedOnFTPPct(ftpPct));
}
/*---------------------------------------------------------------------------------------------*/
int getChartHeightBasedOnFTP(int ftpPct){
   //chart will cap out at the max threshold, plus 10

  // start with 100% FTP scaling, but bump that if maxpower exceeds

  float chartMaxFTP = max(float(100.0), float(100 * maxPowerArray[powerAvgMode]/inputFTP));
  float scalingFactor = float(RESOLUTION_Y) / chartMaxFTP;
  int h = int(scalingFactor * ftpPct);
  //Serial.println("getChartHeightBasedOnFTP, ftpPct:" + String(ftpPct) + " h:" + String(h));
  h = min(RESOLUTION_Y, h);  // in case we calc a height greater than the resolution
  h = max(1, h); // in case power is zero
  return h;
}
/*---------------------------------------------------------------------------------------------*/
void setTextColorBasedOnFTPPct(int ftpPct){
  //Serial.println("setTextColorBasedOnFTPPct, ftpPct:" + String(ftpPct));
  imgSprite.setTextColor(getColorBasedOnFTPPct(ftpPct), TFT_BLACK);
}
/*---------------------------------------------------------------------------------------------*/
void setDefaultTextColor() {
  imgSprite.setTextColor(TFT_SKYBLUE, TFT_BLACK);
}
/*---------------------------------------------------------------------------------------------*/
uint32_t getColorBasedOnFTPPct(int ftpPct) {
  //Serial.println("getColorBasedOnFTPPct, ftpPct:" + String(ftpPct));
  for (int i = numPowerZones - 1; i >= 0; i--) {
    if (ftpPct > powerZoneThreshold[i]) {
      //Serial.println("getColorBasedOnFTPPct return:" + String(powerZoneThreshold[i]));
      return powerZoneColor[i];
    }
  }
  return TFT_DARKGREY;
}
/*---------------------------------------------------------------------------------------------*/

String getPowerZoneDesc(int ftpPct) {
  //Serial.println("getPowerZoneDesc, ftpPct:" + String(ftpPct));
  for (int i = numPowerZones - 1; i >= 0; i--) {
    if (ftpPct > powerZoneThreshold[i]) {
      //Serial.println("getPowerZoneDesc return:" + powerZoneDescription[i]);
      return powerZoneDescription[i];
    }
  }
  return "Coasting / Stopped";
}

/*---------------------------------------------------------------------------------------------*/
// roll second averages down, bumping off the older and adding the newest average second observation
void updateAvgPowerArray(float newestAvgSec) {

  if (newestAvgSec > 0){
    powerObservationsArray[avgInstant]++;
    powerCumulativeTotalArray[avgInstant] += newestAvgSec;
  }
  for (int i = maxSecsToAvg - 1; i> 0; i--){
        avgBySecond[i] = avgBySecond[i - 1];
      }
    avgBySecond[0] = newestAvgSec;
  // update the 3 second max, but only if all of the seconds spanned are non-zero
  int n =0;
  int p = 0;
  for (int i = 0; i < 3; i++) {
    if (avgBySecond[i] > 0) n++;
    p = p + avgBySecond[i];
  }
  if (n == 3 ) {
    if (int(p/3) > maxPowerArray[avg3Secs]) maxPowerArray[avg3Secs] = int(p/3);
    powerObservationsArray[avg3Secs]++;
    powerCumulativeTotalArray[avg3Secs] += int(p/3);
  }
  // ditto the 10 second max
  p = 0;
  n = 0;
  for (int i = 0; i < 10; i++) {
    if (avgBySecond[i] > 0) n++;
    p = p + avgBySecond[i];
  }
  if (n == 10) {
    if (int(p/10) > maxPowerArray[avg10Secs]) maxPowerArray[avg10Secs] = int(p/10);
    powerObservationsArray[avg10Secs]++;
    powerCumulativeTotalArray[avg10Secs] += int(p/10);
  }

}
/*------------------------------------------------------------------------------------------------------------*/
void calcTileAndLock(uint8_t *pData, size_t length){
  /*for (int i = 0; i < length; i++) {
    //Serial.print(pData[i], HEX);
    //Serial.print(" "); //separate values with a space
  }
  //Serial.println("");
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
  currentPower = (int)(pData[3] << 8 | pData[2]);
  long currentSecond = long(millis()/1000);
  if (currentPower > maxPowerArray[avgInstant]) maxPowerArray[avgInstant] = currentPower;

  if (currentPower > 0) {
    lastNonZeroPower = currentSecond;
    // are we still tracking within the "current" second - if so update intra second #s
    if (lastPowerObservationSecond == currentSecond ) {
      intraSecondObservations++;
      intraSecondCumulativeTotal += long(currentPower);
    } else {
      // we have moved onto a new second - bump the second track of power observations, including our structure for calculating moving avg
      
      if (intraSecondObservations > 0) {
        updateAvgPowerArray(intraSecondCumulativeTotal/intraSecondObservations);
      } else {
        updateAvgPowerArray(0);
      }
      // reset intra second varaibles
      intraSecondObservations = 1;
      intraSecondCumulativeTotal = currentPower;
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
