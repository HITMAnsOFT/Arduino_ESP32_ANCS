// Original: https://github.com/S-March/esp32_ANCS
// fixed for Arduino15/packages/esp32/hardware/esp32/1.0.3

#include "BLEDevice.h"
#include "Task.h"
#include <U8g2lib.h>
#include <soc/rtc.h>
#include <M5Stack.h>

// surplus macro from M5Stack LCD library
#undef setFont

static char LOG_TAG[] = "SampleServer";

static BLEUUID ancsServiceUUID("7905F431-B5CE-4E99-A40F-4B1E122D00D0");
static BLEUUID notificationSourceCharacteristicUUID("9FBF120D-6301-42D9-8C58-25E699A21DBD");
static BLEUUID controlPointCharacteristicUUID("69D1D8F3-45E1-49A8-9821-9BBDFDAAD9D9");
static BLEUUID dataSourceCharacteristicUUID("22EAC6E9-24D6-4BB5-BE44-B36ACE7C7BFB");

time_t timeOffset {};
class MySecurity : public BLESecurityCallbacks {

    uint32_t onPassKeyRequest(){
        ESP_LOGI(LOG_TAG, "PassKeyRequest");
        return 123456;
    }

    void onPassKeyNotify(uint32_t pass_key){
        ESP_LOGI(LOG_TAG, "On passkey Notify number:%d", pass_key);
    }

    bool onSecurityRequest(){
        ESP_LOGI(LOG_TAG, "On Security Request");
        return true;
    }
    
    bool onConfirmPIN(unsigned int){
        ESP_LOGI(LOG_TAG, "On Confrimed Pin Request");
        return true;
    }

    void onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl){
        ESP_LOGI(LOG_TAG, "Starting BLE work!");
        if(cmpl.success){
            uint16_t length;
            esp_ble_gap_get_whitelist_size(&length);
            ESP_LOGD(LOG_TAG, "size: %d", length);
        }
    }
};

bool connected = false;

#define CATEGORY_TABLE_SIZE 12
int notification_counts[CATEGORY_TABLE_SIZE] = {0,0,0,0,0,0,0,0,0,0,0,0};
const char* category_table[CATEGORY_TABLE_SIZE] = {
  "Other",         //  0
  "Incoming call", //  1
  "Missed call",   //  2
  "Voicemail",     //  3
  "Social",        //  4
  "Schedule",      //  5
  "Email",         //  6
  "News",          //  7
  "Health",        //  8
  "Business",      //  9
  "Location",      // 10
  "Entertainment", // 11
};
#define CATEGORY_INCOMING_CALL 1

static void _dataSourceNotifyCallback(BLERemoteCharacteristic* pCharacteristic, uint8_t* pData, size_t length, bool isNotify);
static void _notificationSourceNotifyCallback(BLERemoteCharacteristic* pCharacteristic, uint8_t* pData, size_t length, bool isNotify);

/**
 * Become a BLE client to a remote BLE server.  We are passed in the address of the BLE server
 * as the input parameter when the task is created.
 */
class MyClient: public Task {
    void run(void* data) {
        BLEAddress* pAddress = (BLEAddress*)data;
        BLEClient*  pClient  = BLEDevice::createClient();
        BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT);
        BLEDevice::setSecurityCallbacks(new MySecurity());

        BLESecurity *pSecurity = new BLESecurity();
        pSecurity->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND);
        pSecurity->setCapability(ESP_IO_CAP_IO);
        pSecurity->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
        // Connect to the remove BLE Server.
        pClient->connect(*pAddress);

        /** BEGIN ANCS SERVICE **/
        // Obtain a reference to the service we are after in the remote BLE server.
        BLERemoteService* pAncsService = pClient->getService(ancsServiceUUID);
        if (pAncsService == nullptr) {
            ESP_LOGD(LOG_TAG, "Failed to find our service UUID: %s", ancsServiceUUID.toString().c_str());
            return;
        }
        // Obtain a reference to the characteristic in the service of the remote BLE server.
        BLERemoteCharacteristic* pNotificationSourceCharacteristic = pAncsService->getCharacteristic(notificationSourceCharacteristicUUID);
        if (pNotificationSourceCharacteristic == nullptr) {
            ESP_LOGD(LOG_TAG, "Failed to find our characteristic UUID: %s", notificationSourceCharacteristicUUID.toString().c_str());
            return;
        }        
        // Obtain a reference to the characteristic in the service of the remote BLE server.
        BLERemoteCharacteristic* pControlPointCharacteristic = pAncsService->getCharacteristic(controlPointCharacteristicUUID);
        if (pControlPointCharacteristic == nullptr) {
            ESP_LOGD(LOG_TAG, "Failed to find our characteristic UUID: %s", controlPointCharacteristicUUID.toString().c_str());
            return;
        }        
        // Obtain a reference to the characteristic in the service of the remote BLE server.
        BLERemoteCharacteristic* pDataSourceCharacteristic = pAncsService->getCharacteristic(dataSourceCharacteristicUUID);
        if (pDataSourceCharacteristic == nullptr) {
            ESP_LOGD(LOG_TAG, "Failed to find our characteristic UUID: %s", dataSourceCharacteristicUUID.toString().c_str());
            return;
        }        
        const uint8_t v[]={0x1,0x0};
        pDataSourceCharacteristic->registerForNotify(_dataSourceNotifyCallback);
        pDataSourceCharacteristic->getDescriptor(BLEUUID((uint16_t)0x2902))->writeValue((uint8_t*)v,2,true);
        pNotificationSourceCharacteristic->registerForNotify(_notificationSourceNotifyCallback);
        pNotificationSourceCharacteristic->getDescriptor(BLEUUID((uint16_t)0x2902))->writeValue((uint8_t*)v,2,true);
        /** END ANCS SERVICE **/

        while(1){
          if(updateRTC(pClient)){
            delay(1000 * 60 * 5);
          }else{
            delay(1000);
          }
        }
    } // run

    public:
    void dataSourceNotifyCallback(BLERemoteCharacteristic* pCharacteristic, uint8_t* pData, size_t length, bool isNotify){
      Serial.print("Notify callback for characteristic ");
      Serial.print(pCharacteristic->getUUID().toString().c_str());
      Serial.print(" of data length ");
      Serial.println(length);
    }

    public:
    void notificationSourceNotifyCallback(BLERemoteCharacteristic* pCharacteristic, uint8_t* pData, size_t length, bool isNotify){
      if(pData[0]==0){
        uint8_t category_id = pData[2];
        notification_counts[category_id]++;
        Serial.printf("New notification! Category: %s(%d) count=%d\n", category_table[category_id], category_id, notification_counts[category_id]);
      }else
      if(pData[0]==1){
        Serial.printf("Notification modified!\n");
      }else
      if(pData[0]==2){
        uint8_t category_id = pData[2];
        notification_counts[category_id]--;
        Serial.printf("Notification removed! Category: %s(%d) count=%d\n", category_table[category_id], category_id, notification_counts[category_id]);
      }
      Serial.printf("  pData=");
      for(int i=0; i<length; i++){
        Serial.printf("%02X ", pData[i]);
      }
      Serial.printf("length=%d isNotify=%d\n", length, isNotify);
    }

    bool updateRTC(BLEClient* pClient)
    {
      // Get current time from iPhone's "Current Time Service"
      std::string s = pClient->getValue(BLEUUID((uint16_t)0x1805), BLEUUID((uint16_t)0x2A2B));
      if(s.length() == 10){
        Serial.printf("CurrentTimeService: length=%d data=[", s.length());
        for(int i=0; i<s.length(); i++){
          Serial.printf("%02X ", s.c_str()[i]);
        }
        Serial.printf("] ");
        uint16_t year   = *((uint16_t*)(s.c_str() + 0));
        uint8_t  month  = *((uint8_t* )(s.c_str() + 2));
        uint8_t  day    = *((uint8_t* )(s.c_str() + 3));
        uint8_t  hour   = *((uint8_t* )(s.c_str() + 4));
        uint8_t  minute = *((uint8_t* )(s.c_str() + 5));
        uint8_t  second = *((uint8_t* )(s.c_str() + 6));
        uint8_t  wday   = *((uint8_t* )(s.c_str() + 7));
        Serial.printf("%04d-%02d-%02d(%d) %02d:%02d:%02d\n", year, month, day, wday, hour, minute, second);

        struct tm tmNow {};
        tmNow.tm_year = year - 1900;
        tmNow.tm_mon = month - 1;
        tmNow.tm_mday = day;
        tmNow.tm_hour = hour;
        tmNow.tm_min  = minute;
        tmNow.tm_sec = second;
        tmNow.tm_wday = wday;

        time_t now = mktime(&tmNow);
        time_t rtc = time(0);
        timeOffset = now - rtc;
        return true;
      }
      return false;
    }
}; // MyClient

MyClient* pMyClient = NULL; // only one instance....
static void _dataSourceNotifyCallback(BLERemoteCharacteristic* pCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
    pMyClient->dataSourceNotifyCallback(pCharacteristic, pData, length, isNotify);
}
static void _notificationSourceNotifyCallback(BLERemoteCharacteristic* pCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
    pMyClient->notificationSourceNotifyCallback(pCharacteristic, pData, length, isNotify);
}


class MainBLEServer: public Task, public BLEServerCallbacks {
    void run(void *data) {
        ESP_LOGD(LOG_TAG, "Starting BLE work!");
        esp_log_buffer_char(LOG_TAG, LOG_TAG, sizeof(LOG_TAG));
        esp_log_buffer_hex(LOG_TAG, LOG_TAG, sizeof(LOG_TAG));

        // Initialize device
        BLEDevice::init("Watch");
        BLEServer* pServer = BLEDevice::createServer();
        pServer->setCallbacks(this);
        BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT);
        BLEDevice::setSecurityCallbacks(new MySecurity());

        // Advertising parameters:
        // Soliciting ANCS
        BLEAdvertising *pAdvertising = pServer->getAdvertising();
        BLEAdvertisementData oAdvertisementData = BLEAdvertisementData();
        oAdvertisementData.setFlags(0x01);
        _setServiceSolicitation(&oAdvertisementData, BLEUUID("7905F431-B5CE-4E99-A40F-4B1E122D00D0"));
        pAdvertising->setAdvertisementData(oAdvertisementData);        

        // Set security
        BLESecurity *pSecurity = new BLESecurity();
        pSecurity->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND);
        pSecurity->setCapability(ESP_IO_CAP_OUT);
        pSecurity->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

        //Start advertising
        pAdvertising->start();
        
        ESP_LOGD(LOG_TAG, "Advertising started!");
        while (true) {
          delay(portMAX_DELAY);
        }
    }

    void onConnect(BLEServer* pServer, esp_ble_gatts_cb_param_t *param) {
        Serial.println("********************");
        Serial.println("**Device connected**");
        Serial.println(BLEAddress(param->connect.remote_bda).toString().c_str());
        Serial.println("********************");
        if(pMyClient != NULL){
          Serial.println("...invalid state...");
          return;
        }
        pMyClient = new MyClient();
        pMyClient->setStackSize(20000);
        pMyClient->start(new BLEAddress(param->connect.remote_bda));
        connected = true;
    };

    void onDisconnect(BLEServer* pServer) {
        Serial.println("************************");
        Serial.println("**Device  disconnected**");
        Serial.println("************************");

        // reboot
        esp_sleep_enable_timer_wakeup(1000000);
        Serial.flush();
        esp_deep_sleep_start();
    }

    /**
     * @brief Set the service solicitation (UUID)
     * @param [in] uuid The UUID to set with the service solicitation data.  Size of UUID will be used.
     */
    void _setServiceSolicitation(BLEAdvertisementData *a, BLEUUID uuid)
    {
      char cdata[2];
      switch(uuid.bitSize()) {
        case 16: {
          // [Len] [0x14] [UUID16] data
          cdata[0] = 3;
          cdata[1] = ESP_BLE_AD_TYPE_SOL_SRV_UUID;  // 0x14
          a->addData(std::string(cdata, 2) + std::string((char *)&uuid.getNative()->uuid.uuid16,2));
          break;
        }
    
        case 128: {
          // [Len] [0x15] [UUID128] data
          cdata[0] = 17;
          cdata[1] = ESP_BLE_AD_TYPE_128SOL_SRV_UUID;  // 0x15
          a->addData(std::string(cdata, 2) + std::string((char *)uuid.getNative()->uuid.uuid128,16));
          break;
        }
    
        default:
          return;
      }
    } // setServiceSolicitationData

    
};

void SampleSecureServer(void)
{
    MainBLEServer* pMainBleServer = new MainBLEServer();
    pMainBleServer->setStackSize(20000);
    pMainBleServer->start();
}


#define LedPin 19
#define IrPin 17
#define BuzzerPin 26
#define BtnPin 35
U8G2_SH1107_64X128_F_4W_HW_SPI u8g2(U8G2_R1, 14, /* dc=*/ 27, /* reset=*/ 33);
const int CURRENT_100MA = (0x01 << 0);

void u8g2_setup()
{
  u8g2.begin();
  u8g2.setFontRefHeightExtendedText();
  u8g2.setFontDirection(0);
  u8g2.setDrawColor(1);
  u8g2.setFontPosTop();
}

void setup()
{
  //rtc_clk_cpu_freq_set(RTC_CPU_FREQ_80M);
  M5.Power.begin();
  M5.Power.setPowerBoostKeepOn(true);
  M5.Power.setVinMaxCurrent(CURRENT_100MA);
  u8g2_setup();
  pinMode(LedPin, OUTPUT);
  digitalWrite(LedPin, LOW);
  pinMode(BtnPin, INPUT_PULLUP);
  Serial.begin(115200);
  SampleSecureServer();
}

int toral_count = 0;

void loop()
{
    bool led = false;
    toral_count = 0;
    for(int i=0; i < CATEGORY_TABLE_SIZE; i++){
        toral_count += notification_counts[i];
    }
    if(!connected && 3*1000 < millis()){
        led = (millis() / 200 % 2 == 0);
    }else if(0 < notification_counts[CATEGORY_INCOMING_CALL]){
        led = (millis() / 100 % 2 == 0);
    }else if(0 < toral_count){
        led = (millis() / 100 % 100 == 0);
    }else{
        led = false;
    }
    digitalWrite(LedPin, led ? HIGH : LOW);
    display();
    delay(50);
}

void u8g2_prepare() {
  u8g2.setPowerSave(0);
  u8g2.setFont(u8g2_font_8x13B_tf);
  u8g2.setContrast(150);
  u8g2.clearBuffer();
}

void display()
{
  static int last_counter = 0;
  static int counter_offset = -1;
  int counter = millis() / 100;
  if(counter <= last_counter){ return; }
  last_counter = counter;
  static struct tm* rtc_time {};
  static bool screenOn = true;
  static int minute  = 0;
  static int hour = 0;
  int second_x10 = (counter + counter_offset) % 600;
  if (598 < second_x10 || second_x10 < 2) {
    minute = (counter + counter_offset) / 600 % 60;
    hour = (counter + counter_offset) / 36000 % 24;
    screenOn = true;
  }

  if (counter_offset == -1 || (second_x10 < 2 && minute % 10 == 0)) {
    if (!timeOffset) {
      return;
    }
    time_t now = time(0) + timeOffset;
    rtc_time = localtime(&now);
    Serial.printf("RTC=%s hour=%d minute=%d second_x10=%d counter_offset=%d\n", asctime(rtc_time), hour, minute, second_x10, counter_offset);
    counter_offset = 10 * ((rtc_time->tm_hour - counter / 36000 % 24) * 3600 + (rtc_time->tm_min - counter / 600 % 60) * 60 + (rtc_time->tm_sec - counter / 10 % 60));
    Serial.printf("new counter_offset=%d\n", counter_offset);
    minute = (counter + counter_offset) / 600 % 60;
    hour = (counter + counter_offset) /36000 % 24;
  } else  if (!connected && 300 < counter) {
    switch(counter / 3 % 2) {
    case 0:
      screenOn = true;
      u8g2_prepare();
      u8g2.drawUTF8(3, 25, "Misplaced!?");
      break;
    case 1:
      u8g2.clearBuffer();
      break;
    }
    u8g2.sendBuffer();
  } else if (0 < notification_counts[CATEGORY_INCOMING_CALL]) {
    switch(counter / 10 % 2){
    case 0:
      screenOn = true;
      u8g2_prepare();
      u8g2.drawUTF8(3, 25, "Incoming call!");
      break;
    case 1:
      u8g2.clearBuffer();
      break;
    }
    u8g2.sendBuffer();
  } else if (digitalRead(BtnPin) == LOW) {
    screenOn = true;
    u8g2_prepare();
    u8g2.setFont(u8g2_font_fub20_tf);
    char timeStr[8];
    snprintf(timeStr, sizeof(timeStr), "%02d %02d", hour, minute);
    u8g2.drawStr(3, 3, timeStr);
    if (second_x10 % 10 < 5) {
          snprintf(timeStr, sizeof(timeStr), "%02d", hour);
          int w = u8g2.getStrWidth(timeStr);
          u8g2.drawGlyph(3 + w, 3, ':');
    }
    u8g2.setFont(u8g2_font_6x10_tf);
    char dateStr[20] {};
    if (rtc_time) {
      strftime(dateStr, sizeof(dateStr), "%a, %d %b %Y", rtc_time);
      u8g2.drawStr(3, 30, dateStr);
    }
    if (toral_count) {
      char notifStr[20];
      snprintf(notifStr, sizeof(notifStr), "Notifications: %d", toral_count);
      u8g2.drawStr(3, 50, notifStr);
    }
    int level = M5.Power.getBatteryLevel();

    if (level >= 0) {
      char battChar = '0' + (level / 20);
      u8g2.setFont(u8g2_font_battery19_tn);
      u8g2.drawGlyph(90, 3, battChar);
      const char okChar = 0x73;
      const char boltChar = 0x60;
      const char noChar = 0;
      char chargeChar = M5.Power.isChargeFull() ? okChar : (M5.Power.isCharging() ? boltChar : noChar);
      u8g2.setFont(u8g2_font_open_iconic_all_2x_t);
      u8g2.drawGlyph(96, 3, chargeChar);
    }
    u8g2.sendBuffer();
  } else {
    if (screenOn) {
#if 0 // No always-on display
      u8g2.setPowerSave(1);
#else // Always-on display
      u8g2_prepare();
      u8g2.setContrast(0);
      //u8g2.setFont(u8g2_font_fur20_tf);
      u8g2.setFont(u8g2_font_6x10_tf);
      char timeStr[8];
      snprintf(timeStr, sizeof(timeStr), "%02d %02d", hour, minute);
      u8g2.drawStr(3, 3, timeStr);
      snprintf(timeStr, sizeof(timeStr), "%02d", hour);
      int w = u8g2.getStrWidth(timeStr);
      u8g2.drawGlyph(3 + w, 3, ':');
      if (toral_count) {
        u8g2.drawGlyph(3, 50, '.');
      }
      u8g2.sendBuffer();
#endif
      screenOn = false;
    }
  }

}
