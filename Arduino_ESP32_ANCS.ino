// Original: https://github.com/S-March/esp32_ANCS
// fixed for Arduino15/packages/esp32/hardware/esp32/1.0.3

#include <Arduino.h>
#include "BLEDevice.h"
#include "BLEServer.h"
#include "BLEClient.h"
#include "BLEUtils.h"
#include "BLE2902.h"
#include <esp_log.h>
#include <esp_bt_main.h>
#include <string>
#include "Task.h"
#include <sys/time.h>
#include <time.h>
#include "sdkconfig.h"
#include <M5StickC.h>

static char LOG_TAG[] = "SampleServer";

static BLEUUID ancsServiceUUID("7905F431-B5CE-4E99-A40F-4B1E122D00D0");
static BLEUUID notificationSourceCharacteristicUUID("9FBF120D-6301-42D9-8C58-25E699A21DBD");
static BLEUUID controlPointCharacteristicUUID("69D1D8F3-45E1-49A8-9821-9BBDFDAAD9D9");
static BLEUUID dataSourceCharacteristicUUID("22EAC6E9-24D6-4BB5-BE44-B36ACE7C7BFB");

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
char* category_table[CATEGORY_TABLE_SIZE] = {
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
        delay(portMAX_DELAY);
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
        pMyClient->setStackSize(18000);
        pMyClient->start(new BLEAddress(param->connect.remote_bda));
        connected = true;
    };

    void onDisconnect(BLEServer* pServer) {
        Serial.println("************************");
        Serial.println("**Device  disconnected**");
        Serial.println("************************");

        // reboot
        M5.Axp.DeepSleep(SLEEP_SEC(1));
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


#define LED_BUILTIN 10

void setup()
{
  M5.begin(false);
  M5.Axp.ScreenBreath(7);
  M5.Lcd.begin();

  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setRotation(1);
  M5.Lcd.setTextFont(1);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  pinMode(M5_BUTTON_HOME, INPUT);

  Serial.begin(115200);
  SampleSecureServer();
}

void loop()
{
  bool led = false;
  int toral_count = 0;
  for(int i=0; i<CATEGORY_TABLE_SIZE; i++){
    toral_count += notification_counts[i];
  }
  if(!connected && 3*1000 < millis()){
    led = (millis() / 200 % 2 == 0);
  }else
  if(0 < notification_counts[CATEGORY_INCOMING_CALL]){
    led = (millis() / 100 % 2 == 0);
  }else
  if(0 < toral_count){
    led = (millis() / 100 % 100 == 0);
  }else{
    led = false;
  }
  digitalWrite(LED_BUILTIN, led ? LOW : HIGH);
  display();
  delay(20);
}


void display()
{
  static int last_counter = 0;
  int counter = millis() / 100;
  if(counter == last_counter){ return; }
  last_counter = counter;

  if(!connected && 3*1000 < millis()){
    uint16_t c0 = YELLOW;
    uint16_t c1 = RED;
    switch(counter / 3 % 2){
    case 0:
      c0 = YELLOW;
      c1 = RED;
      break;
    case 1:
      c0 = RED;
      c1 = YELLOW;
      break;
    }
    M5.Axp.ScreenBreath(15);
    M5.Lcd.fillScreen(c1);
    M5.Lcd.setTextSize(4);
    M5.Lcd.setCursor(3, 3, 1);
    M5.Lcd.setTextColor(c0, c1);
    M5.Lcd.printf("Misplaced!?");
  }else
  if(0 < notification_counts[CATEGORY_INCOMING_CALL]){
    switch(counter / 10 % 2){
    case 0:
      M5.Axp.ScreenBreath(15);
      M5.Lcd.fillScreen(BLACK);
      M5.Lcd.setTextSize(4);
      M5.Lcd.setCursor(3, 3, 1);
      M5.Lcd.setTextColor(GREEN, BLACK);
      M5.Lcd.printf("Calling...");
      break;
    case 1:
      M5.Axp.ScreenBreath(0);
      M5.Lcd.fillScreen(BLACK);
      break;
    }
  }else{
    RTC_TimeTypeDef RTC_TimeStruct;
    M5.Rtc.GetTime(&RTC_TimeStruct);
    if(RTC_TimeStruct.Seconds <= 5){
      if(RTC_TimeStruct.Minutes == 0){
        M5.Axp.ScreenBreath(15);
      }else
      if(RTC_TimeStruct.Minutes % 15 == 0){
        M5.Axp.ScreenBreath(12);
      }else{
        M5.Axp.ScreenBreath(8);
      }
      RTC_DateTypeDef RTC_DateStruct;
      M5.Rtc.GetData(&RTC_DateStruct);
      M5.Lcd.setCursor(3, 3, 7);
      M5.Lcd.setTextSize(1);
      M5.Lcd.setTextColor(ORANGE, BLACK);
      M5.Lcd.printf("%02d:%02d", RTC_TimeStruct.Hours, RTC_TimeStruct.Minutes);
      Serial.printf("%04d-%02d-%02d(%d) %02d:%02d:%02d\n", RTC_DateStruct.Year, RTC_DateStruct.Month, RTC_DateStruct.Date, RTC_DateStruct.WeekDay, RTC_TimeStruct.Hours, RTC_TimeStruct.Minutes, RTC_TimeStruct.Seconds);
    }else{
      M5.Axp.ScreenBreath(0);
      M5.Lcd.fillScreen(BLACK);
    }
  }
}
