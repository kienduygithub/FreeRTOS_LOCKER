#define BLYNK_TEMPLATE_ID "TMPL61hLFKof_"
#define BLYNK_TEMPLATE_NAME "RFID Locker"
#define BLYNK_AUTH_TOKEN "b5lH099qtUS5bWwh0XNDXCCc9KgrB9xw"
#define BLYNK_PRINT Serial
#include <BlynkSimpleEsp32.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <ESP32Servo.h>
#include <SPI.h>
#include <MFRC522.h>
#include <EEPROM.h>
#include <Key.h>
#include <Keypad.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include <string.h>

char auth[] = BLYNK_AUTH_TOKEN;
char ssid[] = "Hi";
char pass[] = "DuyAkitok2";

LiquidCrystal_I2C lcd(0x27, 16, 2);  

#define SERVO_PIN 32
#define BUZZER 1
Servo myServo;  // Initialize servo
int pos = 0;    // Variable to store servo position

const byte ROWS = 4;
const byte COLS = 3;
char keys[ROWS][COLS] = {
  { '1', '2', '3' },
  { '4', '5', '6' },
  { '7', '8', '9' },
  { '*', '0', '#' }
};
byte rowPins[ROWS] = { 13, 12, 14, 27 };
byte colPins[COLS] = { 26, 25, 33 };
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

char key_code[7]; 
char new_pass[7]; 
char password[7] = "111111"; 

#define SS_PIN 5
#define RST_PIN 15
MFRC522 rfid(SS_PIN, RST_PIN);

#define EEPROM_SIZE 512
uint32_t ID_CARD_PASS[10];
int TotalCardStored = 10;
const uint32_t initCard = 0x630E3F25; // Thẻ ban đầu { 0x63, 0x0E, 0x3F, 0x25 }
byte ScannedCard[4];

#define BUTTON 2

void HandleButtonTask(void *pvParameters);
void DisplayLCDTask(void *pvParameters);
void InputKeypadTask(void *pvParameters);
void StartProgramTask(void *pvParameters);
void ScanRFIDTask(void *pvParameters);
void SaveRFIDToEEPROM(uint32_t card, int cardIndex);
bool LoadRFIDFromEEPROM(uint32_t &card, int cardIndex);
// FreeRTOS
QueueHandle_t commandQueue;
SemaphoreHandle_t xSemaphoreStartProgram;

#define STATE_LOCKER_ON_APP_PIN V0
#define BUTTON_LOCKER_ON_APP_PIN V1
#define PASSWORD_ON_APP_PIN V2
#define TOTAL_CARD_ON_APP_PIN V3
WidgetLED STATE_LOCKER_ON_APP(STATE_LOCKER_ON_APP_PIN);
int button;

void setup(){
  Serial.begin(9600);
  SPI.begin(); 
  rfid.PCD_Init(); 
  lcd.init();                   
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print(" Choose Options");
  pinMode(BUZZER, OUTPUT);
  pinMode(BUTTON, INPUT_PULLUP);
  myServo.attach(SERVO_PIN); // Gắn servo vào chân SERVO_PIN với các giá trị giới hạn 1000 µs và 2000 µs
  if(!EEPROM.begin(EEPROM_SIZE))
  {
    Serial.println("Failed to initialize EEPROM...");
    return;
  }
  Serial.println("EEPROM initialised successfully!");
  uint32_t TestCard;
  if(!LoadRFIDFromEEPROM(TestCard, 0))
  {
    Serial.println("Initializing default RFID card...");
    SaveRFIDToEEPROM(initCard, 0);
    ID_CARD_PASS[0] = initCard;
  }
  // Tải tất cả các mã thẻ từ EEPROM vào mảng ID_CARD_PASS
  for(int i = 0;i < TotalCardStored;i++)
  {
    LoadRFIDFromEEPROM(ID_CARD_PASS[i], i);
  }

  commandQueue = xQueueCreate(1, sizeof(char));
  xSemaphoreStartProgram = xSemaphoreCreateMutex();
  xTaskCreate(HandleButtonTask, "HandleButtonTask", 1280, NULL, 1, NULL);
  xTaskCreate(DisplayLCDTask, "DisplayLCDTask", 1280 * 2, NULL, 1, NULL);
  xTaskCreate(InputKeypadTask, "InputKeypadTask", 1280, NULL, 1, NULL);
  xTaskCreate(StartProgramTask, "StartProgramTask", 1280 * 8, NULL, 1, NULL);
  xTaskCreate(ScanRFIDTask, "ScanRFIDTask", 1280 * 4, NULL, 1, NULL);

  int total = GetCurrentTotalCardStored();
  Blynk.begin(auth, ssid, pass);
  Blynk.virtualWrite(PASSWORD_ON_APP_PIN, password);
  Blynk.virtualWrite(TOTAL_CARD_ON_APP_PIN, total);
}

int passwordIndex = 0;
bool ChangePassMode = false;
bool EnterNewPassMode = false;
bool EnterPassMode = false;
bool UpdateLCD = true;
bool HandleCardMode = false;
bool InsertCardMode = false;
bool RemoveCardMode = false;
bool ChangeCardMode = false;
bool UnLockEnabled = false;
bool CanAccess = false;

void loop(){
  Blynk.run();
  delay(300);
  if(UnLockEnabled)
  {
    STATE_LOCKER_ON_APP.on();
    Blynk.virtualWrite(BUTTON_LOCKER_ON_APP_PIN, UnLockEnabled ? 1 : 0);
  }
  else
  {
    STATE_LOCKER_ON_APP.off();
    Blynk.virtualWrite(BUTTON_LOCKER_ON_APP_PIN, UnLockEnabled ? 1 : 0);
  }
}

// ================================= BLYNK ================================= //
BLYNK_WRITE(BUTTON_LOCKER_ON_APP_PIN)
{
  button = param.asInt();
  if(button == 1){
    UnLockEnabled = true;
    CanAccess = true;
  }else{
    UnLockEnabled = false;
    CanAccess= false;
  }
}
// =================================  END  ================================= //
void DisplayLCDTask(void *pvParameters)
{
  while(1)
  {
    if(xSemaphoreTake(xSemaphoreStartProgram, portMAX_DELAY) == pdTRUE)
    {
      if(UpdateLCD)
      {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(" Choose Options");
        UpdateLCD = false;
      }
      xSemaphoreGive(xSemaphoreStartProgram); // Giải phóng Semaphore sau khi sử dụng xong
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

void InputKeypadTask(void *pvParameters)
{
  while(1)
  {
    char key = keypad.getKey();
    if(key != NO_KEY)
    {
      if(xSemaphoreTake(xSemaphoreStartProgram, portMAX_DELAY) == pdTRUE)
      {
        Serial.println(key);
        xQueueSend(commandQueue, &key, portMAX_DELAY);
        xSemaphoreGive(xSemaphoreStartProgram);
      }
    }
    vTaskDelay(pdMS_TO_TICKS( 100 ));
  }
}

void StartProgramTask(void *pvParameters)
{
  while(1)
  {
    char key;
    if(xQueueReceive(commandQueue, &key, portMAX_DELAY) == pdTRUE)
    {
      if(ChangePassMode || EnterPassMode || HandleCardMode)
      {
        if(ChangePassMode)  handleChangePassWord(key);
        else if(EnterPassMode) handleEnterPassword(key);
        else if(HandleCardMode) handleCard(key);
      }
      else if(key == '5')
      {
        UpdateLCD = false;
        Serial.println("Trạng thái xóa thẻ RFID");
        ChangeCardMode = true;
        HandleCardMode = true;
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(" Enter Password");
        passwordIndex = 0;
      }
      else if(key == '4')
      {
        UpdateLCD = false;
        Serial.println("Trạng thái xóa thẻ RFID");
        RemoveCardMode = true;
        HandleCardMode = true;
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(" Enter Password");
        passwordIndex = 0;
      }
      else if(key == '3')
      {
        UpdateLCD = false;
        Serial.println("Trạng thái thêm thẻ RFID");
        InsertCardMode = true;
        HandleCardMode = true;
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(" Enter Password");
        passwordIndex = 0;
      }
      else if(key == '2')
      {
        UpdateLCD = false;
        Serial.println("Trạng thái đổi mật khẩu");
        ChangePassMode = true;
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(" Enter Old Pass");
        passwordIndex = 0;
      }
      else if(key == '1')
      {
        UpdateLCD = false;
        Serial.println("Trạng thái nhập mật khẩu");
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(" Enter Password");
        EnterPassMode = true;
        passwordIndex = 0;
      }
      else if(key == '*')
      {
        ClearAllCardsInEEPROM();
        SaveRFIDToEEPROM(initCard, 0);
        ID_CARD_PASS[0] = initCard;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void handleChangePassWord(char key)
{
  if(key == '#')
  {
    key_code[passwordIndex] = '\0';
    passwordIndex = 0;
    if(strcmp(password, key_code) == 0)
    {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(" Enter New Pass");
      EnterPassMode = true;
      EnterNewPassMode = true;
    }
    else
    {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(" Wrong Password");
      vTaskDelay(pdMS_TO_TICKS(1500));
      UpdateLCD = true;
    }
    memset(key_code, 0, sizeof(key_code));
    ChangePassMode = false;
  }
  else if(passwordIndex < 6)
  {
    key_code[passwordIndex++] = key;
    lcd.setCursor(4 + passwordIndex, 1);
    lcd.print("*");
  }
  
}

void handleEnterPassword(char key)
{
  if(key == '#')
  {
    key_code[passwordIndex] = '\0';
    passwordIndex = 0;
    if(EnterNewPassMode)
    {
      strcpy(password, key_code);
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(" Change Succeed");
      Blynk.virtualWrite(PASSWORD_ON_APP_PIN, password);
      vTaskDelay(pdMS_TO_TICKS(1500));
    }
    else
    {
      if(strcmp(password, key_code) == 0)
      {
        Unlock();
      }
      else
      {
        NotUnlock();
      }
    }
    memset(key_code, 0, sizeof(key_code));
    EnterPassMode = false;
    EnterNewPassMode = false;
    UpdateLCD = true;
  }
  else if(passwordIndex < 6)
  {
    key_code[passwordIndex++] = key;
    lcd.setCursor(4 + passwordIndex, 1);
    lcd.print("*");
  }
}

// ================================= MỞ CỬA ================================= //
void HandleButtonTask(void *pvParameters)
{
  while(1)
  {
    if(CanAccess)
    { 
      if(xSemaphoreTake(xSemaphoreStartProgram, portMAX_DELAY) == pdTRUE)
      {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("  Access Grant  ");
        for (int z = 180; z > 40; z -= 20) {
          myServo.write(z);
          delay(300);
        }
        vTaskDelay(pdTICKS_TO_MS(4000));
        myServo.write(180);
        ChangePassMode = false;
        EnterNewPassMode = false;
        EnterPassMode = false;
        InsertCardMode = false;
        RemoveCardMode = false;
        ChangeCardMode = false;
        UpdateLCD = true;
        UnLockEnabled = false;
        CanAccess = false;
        xSemaphoreGive(xSemaphoreStartProgram);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(100)); // Delay giữa các lần kiểm tra nút
  }
}

void Unlock() 
{
  UnLockEnabled = true;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("  Access Grant  ");
  for (int z = 180; z > 40; z -= 20) {
    myServo.write(z);
    delay(300);
  }
  vTaskDelay(pdTICKS_TO_MS(4000));
  myServo.write(180);
  UnLockEnabled = false;
}

void NotUnlock() 
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("  Access Denied");
  vTaskDelay(pdMS_TO_TICKS(1500));
}

// ================================= RFID ================================= //
bool isValidCard = false;
void ScanRFIDTask(void *pvParameters)
{
  while(1)
  {
    if(!HandleCardMode)
    {
      if(xSemaphoreTake(xSemaphoreStartProgram, portMAX_DELAY) == pdTRUE)
      {
        if(rfid.PICC_IsNewCardPresent())
        {
          if(rfid.PICC_ReadCardSerial())
          {
            for(byte i = 0;i < 4;i++)
            {
              ScannedCard[i] = rfid.uid.uidByte[i];
            }
            Serial.print("Scanned Card: ");
            for(byte i = 0;i < 4;i++)
            {
              Serial.print(ScannedCard[i], HEX);
              Serial.print(" ");
            }
            Serial.println();
            uint32_t ScannedCardUint_32 = byteArrayToUint32(ScannedCard);
            isValidCard = false;
            for(int i = 0;i < TotalCardStored;i++)
            {
              if(ScannedCardUint_32 == ID_CARD_PASS[i])
              {
                isValidCard = true;
                break;
              }
            }
            if(isValidCard){
              Unlock();
            }else{
              NotUnlock();
            }
            isValidCard = false;
            EnterPassMode = false;
            ChangePassMode = false;
            EnterNewPassMode = false;
            InsertCardMode = false;
            RemoveCardMode = false;
            ChangeCardMode = false;
            UpdateLCD = true;
            UnLockEnabled = false;
          }
        }
        xSemaphoreGive(xSemaphoreStartProgram);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void handleCard(char key)
{
  if(key == '#')
  {
    key_code[passwordIndex] = '\0';
    passwordIndex = 0;
    if(strcmp(password, key_code) == 0)
    {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("  Scanning....");
      if(!ChangeCardMode) ScanAndHandleCard();
      else handleChangeCard();
    }
    else
    {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(" Wrong Password");
      vTaskDelay(pdMS_TO_TICKS(1500));
      UpdateLCD = true;
      HandleCardMode = false;
      ChangeCardMode = false;
      RemoveCardMode = false;
      InsertCardMode = false;
    }
    memset(key_code, 0, sizeof(key_code));
  }
  else if(passwordIndex < 6)
  {
    key_code[passwordIndex++] = key;
    lcd.setCursor(4 + passwordIndex, 1);
    lcd.print("*");
  }
}

void handleChangeCard()
{
  while(!rfid.PICC_IsNewCardPresent())
  {
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  if(rfid.PICC_ReadCardSerial())
  {
    if(xSemaphoreTake(xSemaphoreStartProgram, portMAX_DELAY) == pdTRUE)
    {
      for(byte i = 0;i < 4;i++)
      {
        ScannedCard[i] = rfid.uid.uidByte[i];
      }
      uint32_t ScannedCardUint_32 = byteArrayToUint32(ScannedCard);
      int index = -1;
      isValidCard = false;
      for(int i = 0;i < TotalCardStored;i++)
      {
        if(ScannedCardUint_32 == ID_CARD_PASS[i])
        {
          isValidCard = true;
          index = i;
          break;
        }
      }
      if(isValidCard)
      {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(" Scan New Card");
        vTaskDelay(pdMS_TO_TICKS(1000));
        while(!rfid.PICC_IsNewCardPresent())
        {
          vTaskDelay(pdMS_TO_TICKS(100));
        }
        if(rfid.PICC_ReadCardSerial())
        {
          for(byte i = 0;i < 4;i++)
          {
            ScannedCard[i] = rfid.uid.uidByte[i];
          }   
          uint32_t ScannedNewCardUint_32 = byteArrayToUint32(ScannedCard);
          SaveRFIDToEEPROM(ScannedNewCardUint_32, index);
          ID_CARD_PASS[index] = ScannedNewCardUint_32;
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print(" Changed Succeed");
        }
      }
      else
      {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(" Changed Failed");
        lcd.setCursor(0, 1);
        lcd.print("  Not In List");
      }
      vTaskDelay(pdMS_TO_TICKS(1500));
      HandleCardMode = false;
      InsertCardMode = false;
      RemoveCardMode = false;
      ChangeCardMode = false;
      EnterPassMode = false;
      UpdateLCD = true;
      xSemaphoreGive(xSemaphoreStartProgram);
    }
  }
}

void ScanAndHandleCard()
{
  while(!rfid.PICC_IsNewCardPresent())
  {
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  if(rfid.PICC_ReadCardSerial())
  {
    if(xSemaphoreTake(xSemaphoreStartProgram, portMAX_DELAY) == pdTRUE)
    {
      for(byte i = 0;i < 4;i++)
      {
        ScannedCard[i] = rfid.uid.uidByte[i];
      }
      uint32_t ScannedCardUint_32 = byteArrayToUint32(ScannedCard);
      int index = -1;
      bool isFull = false;
      isValidCard = false;
      for(int i = 0;i < TotalCardStored;i++)
      {
        if(ScannedCardUint_32 == ID_CARD_PASS[i])
        {
          isValidCard = true;
          index = i;
          break;
        }
        if(ID_CARD_PASS[i] == 0xFFFFFFFF) {isFull = false;}
      }
      if(!isValidCard)
      {
        if(InsertCardMode)
        {
          if (!isFull) 
          {
            for (int i = 0; i < TotalCardStored; i++) 
            {
              if (ID_CARD_PASS[i] == 0xFFFFFFFF) 
              {
                ID_CARD_PASS[i] = ScannedCardUint_32;
                SaveRFIDToEEPROM(ScannedCardUint_32, i);
                lcd.clear();
                lcd.setCursor(0, 0);
                lcd.print("Inserted Succeed");
                break;
              }
            }
          } 
          else 
          {
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print(" Updated Failed ");
            lcd.setCursor(0, 1);
            lcd.print("  Full Of List  ");
          }
        }
        else if(RemoveCardMode)
        {
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("  Removed Fail");
        }
      }
      else
      {
        if(RemoveCardMode)
        {
          SaveRFIDToEEPROM(0xFFFFFFFF, index);
          ID_CARD_PASS[index] = 0xFFFFFFFF;
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print(" Remove Succeed");
        }
        else
        {
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("  Existed Card");
        }
      }
      vTaskDelay(pdMS_TO_TICKS(1500));
      HandleCardMode = false;
      InsertCardMode = false;
      RemoveCardMode = false;
      EnterPassMode = false;
      UpdateLCD = true;
      int total = GetCurrentTotalCardStored();
      Blynk.virtualWrite(TOTAL_CARD_ON_APP_PIN, total);
      xSemaphoreGive(xSemaphoreStartProgram);
    }
  }
}

void SaveRFIDToEEPROM(uint32_t card, int cardIndex) 
{
  int startAddress = cardIndex * sizeof(uint32_t);
  for (int i = 0; i < sizeof(uint32_t); i++) {
    EEPROM.write(startAddress + i, (card >> (i * 8)) & 0xFF);
  }
  EEPROM.commit();
}

bool LoadRFIDFromEEPROM(uint32_t &card, int cardIndex) 
{
  int startAddress = cardIndex * sizeof(uint32_t);
  card = 0;
  for (int i = 0; i < sizeof(uint32_t); i++) {
    card |= ((uint32_t)EEPROM.read(startAddress + i)) << (i * 8);
  }
  return (card != 0xFFFFFFFF); // Trả về false nếu là giá trị trống
}

uint32_t byteArrayToUint32(byte* array) 
{
  uint32_t value = 0;
  value |= ((uint32_t)array[0] << 24);
  value |= ((uint32_t)array[1] << 16);
  value |= ((uint32_t)array[2] << 8);
  value |= ((uint32_t)array[3]);
  return value;
}

void uint32ToByteArray(uint32_t value, byte* array) 
{
  array[0] = (byte)(value >> 24);
  array[1] = (byte)(value >> 16);
  array[2] = (byte)(value >> 8);
  array[3] = (byte)(value);
}

void ClearAllCardsInEEPROM() 
{
  for (int i = 0; i < TotalCardStored; i++) {
    SaveRFIDToEEPROM(0xFFFFFFFF, i); // Lưu giá trị 0xFFFFFFFF vào mỗi vị trí để đánh dấu là trống
    ID_CARD_PASS[i] = 0xFFFFFFFF;
  }
  Serial.println("All RFID tags have been cleared from EEPROM.");
}

int GetCurrentTotalCardStored()
{
  int total = 0;
  for(int i = 0;i < TotalCardStored;i++)
  {
    if(ID_CARD_PASS[i] != 0xFFFFFFFF) total++;
  }
  return total;
}