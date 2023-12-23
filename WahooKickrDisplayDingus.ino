/*********************************************************************************************************
* Wahoo Kickr SmartBike Display Dingus
* Jay Wheeler Last Update: 12/23/2023
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
* Be sure to update the WEIGHT and FTP constants to product appropriate  W/KG and power zones for you.

Known Bugs:
+ tilt lock / grade does not display correctly until the lock is toggled on the bike
+ front chain ring should go to red when ratio is 1 to 1

To Do:
+ add power smoothing 
+ line add distinguishing lines between fields New Line 
+ calculate max and average power, % of ftp, display somewhere in small font
+ dynamically set number of gears

**********************************************************************************************************/

#include "BLEDevice.h"
#include <TFT_eSPI.h> // Graphics and font library for ST7735 driver chip
#include <SPI.h>
TFT_eSPI tft = TFT_eSPI();  // Invoke library, pins defined in User_Setup.h
// define a sprite to cover the whole screen, avoids flickr - s
TFT_eSprite img = TFT_eSprite(&tft);

// Rider Weight in Kilograms, FTP in watts
#define WEIGHT 67
#define FTP 180

// RESOLUTION
// 170 x 320 ... LILYGO T-Display-S3 ESP32-S3
// 135 x 240 ... LILYGO TTGO T-Display ESP32
#define RESOLUTION_X 320
#define RESOLUTION_Y 170
// BUTTONs
#define BOTTOM_BUTTON_PIN 0
#define TOP_BUTTON_PIN 35


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

String reargear = ("0");
String frontgear = ("0");
String currentGrade = ("0.0");
float currentPower = 0;
int fg, rg;
uint8_t arr[32];
int tilt_lock = 1;
int negative = 0;
int intLastButtonState;
bool bolToggleScreen = false;

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

    /*------------------------------------------------------------------------------------------------*/
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
    /*------------------------------------------------------------------------------------------------*/
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
      calc_tilt(arr, value.size());
    }
    if(pRemoteCharacteristic2->canNotify()) {
         pRemoteCharacteristic2->registerForNotify(notifyCallbackGrade);       
    }
    /*------------------------------------------------------------------------------------------------*/
    // Power
    // Obtain a reference to the power servicein the remote BLE server.
    pRemoteService = pClient->getService(servicePowerUUID);
    if (pRemoteService == nullptr) {
      Serial.print("Failed to find our Power UUID: ");
      Serial.println(servicePowerUUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(" - Found our Poower service");
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
      calc_tilt(arr, value.size());
    }
    if(pRemoteCharacteristic3->canNotify()) {
         pRemoteCharacteristic3->registerForNotify(notifyCallbackPower);       
    }

    /*------------------------------------------------------------------------------------------------*/
    connected = true;
    return true;
}

/**
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
  BLEDevice::init("");
  tft.init();
  tft.setRotation(1);

  tft.setTextSize(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_SKYBLUE, TFT_BLACK);
  tft.drawString("Wahoo Kickr Display Dingus", 0, 0, 4);
  tft.drawString("Gear/Grade/Power Display", 0, 52, 4);
  tft.drawString("Jay Wheeler Dec 23, 2023", 0, RESOLUTION_Y - 16, 2);
  tft.drawString("Connecting....", 0, 78, 4);

  // Retrieve a Scanner and set the callback we want to use to be informed when we
  // have detected a new device.  Specify that we want active scanning and start the
  // scan to run for 5 seconds.
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  pBLEScan->start(5, false);

  img.createSprite(RESOLUTION_X, RESOLUTION_Y);
} // End of setup.

void loop() {

  // If the flag "doConnect" is true then we have scanned for and found the desired
  // BLE Server with which we wish to connect.  Now we connect to it.  Once we are 
  // connected we set the connected flag to be true.
  if (doConnect == true) {
    if (connectToServer()) {
      Serial.println("We are now connected to the BLE Server.");
      tft.setTextColor(TFT_SKYBLUE, TFT_BLACK);
      tft.drawString("Connected :-)   ", 0, 78, 4);
      //tft.drawXBitmap(155, 0, zwiftlogo, 100, 100, TFT_BLACK, TFT_ORANGE);      
      tft.fillScreen(TFT_BLACK);
      //tft.drawXBitmap(155, 0, zwiftlogo, 100, 100, TFT_BLACK, TFT_ORANGE);
    } else {
      Serial.println("We have failed to connect to the server; there is nothin more we will do.");
    }
    doConnect = false;
  }

  // If we are connected to a peer BLE Server, update the characteristic each time we are reached
  // with the current time since boot.
  if (connected) {
      String newValue = "Time since boot: " + String(millis()/1000);
  //  Serial.println("Setting new characteristic value to \"" + newValue + "\"");
    
     // Set the characteristic's value to be the array of bytes that is actually a string.
    pRemoteCharacteristic->writeValue(newValue.c_str(), newValue.length());

    updateDisplayViaSprite();
    

    
  }else if(doScan){
    BLEDevice::getScan()->start(0);  // this is just eample to start scan after disconnect, most likely there is better way to do it in arduino
  }
  
  delay(100); // Delay a 1/10 second
} // End of loop

void updateDisplayViaSprite(){
  // the subroutines below update a screen sized "sprite" - the actual update to the physical screen
  // happens in one step at the end of this routine - this gets rid of flicker
  checkButtonState();
  img.drawString( String(int(millis()/1000)) , 0, 150, 2); // but the time since boot in lower left corner in tiny type
  update_gear();
  update_grade();
  update_front();
  update_rear();
  update_power();
  img.pushSprite(0,0);  // update the physical screen
}

void checkButtonState() {
  int intButtonState = digitalRead(BOTTOM_BUTTON_PIN);
  if (intLastButtonState != intButtonState)
  {
    // Debounce Time
    delay(50);
    if (intButtonState == LOW)
    {
      // Press Action
      // Toggle Button State
      bolToggleScreen = !bolToggleScreen;
    }
    else
    {
      // Release Action, none needed here
    }
    intLastButtonState = intButtonState;
  }
}

void update_front(void)
{
  static int pre_pos = -1;
  if(pre_pos == fg) return;

  uint32_t fillColor = TFT_GREEN;
  if (frontgear == "1" && reargear == "1") {
    // we are out of gears
    fillColor = TFT_RED;
  }
  if(fg == 1) {
    img.fillRect(10, 70, 5, 50, fillColor);
    img.fillRect(17, 60, 5, 70, TFT_BLACK);
    img.drawRect(17, 60, 5, 70, TFT_WHITE);
  }
  else{
    img.fillRect(10, 70, 5, 50, TFT_BLACK);
    img.drawRect(10, 70, 5, 50, TFT_WHITE);
    img.fillRect(17, 60, 5, 70, fillColor);
  }
  pre_pos = fg;
}

void clear_cassette(int i)
{
  i--;
   img.fillRect(35 + i*6, 60 + i*2, 5, 70 - i*4, TFT_BLACK);
   img.drawRect(35 + i*6, 60 + i*2, 5, 70 - i*4, TFT_WHITE);
}

void fill_cassette(int i)
{
  i--;

  if (frontgear == "1" && reargear == "1") {
    // we are out of gears
    img.fillRect(35 + i*6, 60 + i*2, 5, 70 - i*4, TFT_RED);
  } else {
    img.fillRect(35 + i*6, 60 + i*2, 5, 70 - i*4, TFT_GREEN);
  }
}
void update_rear(void)
{
  static int pre_rg = -1;
  if(pre_rg == -1)
  {
    for(int i = 0 ; i < 12 ; i++)
      //img.drawRect(40 + i*10,60 + i*2,5,70 - i*4, TFT_WHITE);
      img.drawRect(35 + i*6,60 + i*2,5,70 - i*4, TFT_WHITE);
    pre_rg = 3;
  }  
  clear_cassette(pre_rg);
  fill_cassette(rg); 
  pre_rg = rg;
}

void update_gear(void){

  String gearRatio = String(frontgear) + ":" + String(reargear) + "   ";

  if (frontgear == "1" && reargear == "1") {
    // we are out of gears
    img.setTextColor(TFT_RED, TFT_BLACK);
  }

  img.drawString(gearRatio, 0, 0, 4);  
  img.setTextColor(TFT_SKYBLUE, TFT_BLACK);
}

void update_grade(void) {
  // if the bike is locked, display that in red, otherwise display the grade
  if (tilt_lock) {
    img.fillRect(RESOLUTION_X/2, 0, RESOLUTION_X - int(RESOLUTION_X/2), int(RESOLUTION_X/2), TFT_BLACK);
    img.setTextColor(TFT_RED, TFT_BLACK);
    img.drawString("Tilt",int(RESOLUTION_X/2), 0, 4);
    img.drawString("Locked",int(RESOLUTION_X/2), 25, 4);
    img.drawString("    ", 240, 50, 2);
    img.setTextColor(TFT_SKYBLUE, TFT_BLACK);
  } else {
      img.fillRect(RESOLUTION_X/2, 0, RESOLUTION_X - int(RESOLUTION_X/2), int(RESOLUTION_X/2), TFT_BLACK);
      img.drawString(currentGrade, int(RESOLUTION_X/2), 0, 7);
      img.drawString("% Grade", 240, 50, 2);
    }
}


void update_power(void) {
 
      //figure out power zone, based on algorithm from cyclistshub.com
      String powerZone = "--";
      String powerDesc = "--";

      uint32_t zChartColor = TFT_DARKGREEN;
    
      float flPercent = 100 * currentPower / float(FTP);
      // note that in the switch logic below, I pad out the powerDesc string to 19
      // places - this allows it to redraw without artifacts and without resorting
      // to fillRect() approach, which causes flickering
      if (flPercent > 150) {
        powerZone = "Z7"; // 
        powerDesc = "Neuromuscular Power";
        zChartColor = 0xF8C0; // red
      } else if (flPercent > 120) {
        powerZone = "Z6"; //
        powerDesc = "Anaerobic Capacity ";
        zChartColor = 0xEC00; // yellow red
      } else if (flPercent > 105) {
        powerZone = "Z5";  // 
        powerDesc = "VO2 Max            ";
        zChartColor = 0xF740; // Yellow
      } else if (flPercent > 90) {
        powerZone = "Z4"; // 
        powerDesc = "Lactate Threshold  ";
        zChartColor = 0xB6A0; // Yellow Green
      } else if (flPercent > 75) {
        powerZone = "Z3"; // 
        powerDesc = "Tempo              ";
        zChartColor = 0x0EE0; // Brighter green
      } else if (flPercent > 55) {
        powerZone = "Z2"; // 
        powerDesc = "Endurance          ";
        zChartColor = 0x0707; // light green
      } else if (flPercent > 0) {
        powerZone = "Z1"; // 
        powerDesc = "Active Recovery    ";
        zChartColor = 0x077D; // blue green
      } else {
        powerZone = "  ";
        powerDesc = "Coasting/Stopped   ";
        zChartColor = TFT_DARKGREY;
      }

      img.setTextColor(zChartColor, TFT_BLACK);
      
      Serial.println("powerZone:" + powerZone + " currentPower:" + String(currentPower));
      img.fillRect(RESOLUTION_X/2, 70, RESOLUTION_X - RESOLUTION_X/2, RESOLUTION_Y - 70, TFT_BLACK);

      float scalingFactor = RESOLUTION_Y / (1.5 * FTP);
      int chartHeight = int(scalingFactor * currentPower);
      chartHeight = min(RESOLUTION_Y, chartHeight);
      chartHeight = max(1, chartHeight); // in case power is zero

      Serial.println("ChartHeight:" + String(chartHeight));

      img.fillRect(RESOLUTION_X - 20, 0, 20, RESOLUTION_Y, TFT_BLACK);
      img.fillRect(RESOLUTION_X - 20, RESOLUTION_Y - chartHeight, 20, chartHeight, zChartColor);

      img.drawString(powerZone, RESOLUTION_X/2, 120, 4);
      img.drawString(powerDesc, RESOLUTION_X/2, RESOLUTION_Y-20, 2);
      // Button Toggle
      char buffer[7];
      
      
      if (bolToggleScreen) {
        //Watts mode
        dtostrf(currentPower, 5,0,buffer);
        Serial.println("Buffer:" + String(buffer) + ":");
        img.drawString(String(buffer), RESOLUTION_X/2,70, 7);
        img.drawString("Watts",220, 120, 4); 
      }
      else
      {
        // W/KG mode
        float flPowerToWeight = currentPower/float(WEIGHT);
        //Serial.println("W/KG" + String(flPowerToWeight));
        dtostrf(flPowerToWeight, 5,2,buffer);
        Serial.println("Buffer:" + String(buffer) + ":");
        img.drawString(String(buffer), RESOLUTION_X/2,70, 7);
        img.drawString("W/KG",220, 120, 4);
      }
  img.setTextColor(TFT_SKYBLUE, TFT_BLACK);
  
}

void calc_tilt(uint8_t *pData, size_t length){

  
  for (int i = 0; i < length; i++) {

    Serial.print(pData[i], HEX);
    Serial.print(" "); //separate values with a space
  }
  Serial.println("");

    //lock, unlock update
  if(length == 3 && pData[0] == 0xfd && pData[1] == 0x33) {
    tilt_lock = pData[2] == 0x01;
  }

  //currentGrade update
  if(length == 4 && pData[0] == 0xfd && pData[1] == 0x34){
    char s[10] = {};
    if(pData[3] < 0x80)
    {
      negative = 0; 
      float tmp = (float)(pData[3] << 8 | pData[2]) / 100;
      Serial.println(tmp);
      sprintf(s, "+ %.1f %%", tmp); 
      currentGrade = s;
      Serial.println(currentGrade);
    }
    else
    {
      negative = 1;
      uint16_t tmp16 = 0xffff - (pData[3] << 8 | pData[2]);
      float tmp = (float)tmp16 / 100;
      Serial.println(tmp16);
      Serial.println(tmp);
      sprintf(s, "- %.1f %%",tmp);
      currentGrade = s;
      Serial.println(currentGrade);
    }
  }

}


static void notifyCallbackPower(BLERemoteCharacteristic *pBLERemoteCharacteristic, uint8_t *pData, size_t length, bool isNotify)
{
  uint16_t tmp16 = (pData[3] << 8 | pData[2]);
  currentPower = (float)(tmp16);
}

static void notifyCallbackGrade(
  BLERemoteCharacteristic* pBLERemoteCharacteristic2,
  uint8_t* pData,
  size_t length,
  bool isNotify) {

  Serial.print("noty2 : ");
  /*for(int i=0; i < length; i++) {
    Serial.print(String(i)+":");
    Serial.println(String(pData[i]));
  }*/

  calc_tilt(pData, length); 
}



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

  frontgear = (1+pData[2]);
  reargear = (1+pData[3]);

  fg = (int)(1+pData[2]);
  rg = (int)(1+pData[3]);

}

