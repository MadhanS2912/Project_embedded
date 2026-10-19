#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// === WiFi CREDENTIALS ===
const char* ssid = "madhan_phone";        // Change this
const char* password_wifi = "Madhan2912";  // Change this

// === API CONFIGURATION ===
const char* API_BASE_URL = "https://agnicom-server.onrender.com";
String MY_USER_ID = "ZUFAzDN3WiVv";  // Your user ID
String MY_PIN = "1234";               // Your PIN

// === LCD SETUP ===
LiquidCrystal_I2C lcd(0x27, 16, 2);

// === KEYPAD SETUP ===
const int rowPins[4] = {16, 4, 0, 2};
const int colPins[4] = {19, 18, 5, 17};

char keys[4][4] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

// === NOKIA MULTI-TAP MAPPING ===
const char* keyMap[10] = {
  " 0",      // 0
  "1.,?!",   // 1
  "2ABC",    // 2
  "3DEF",    // 3
  "4GHI",    // 4
  "5JKL",    // 5
  "6MNO",    // 6
  "7PQRS",   // 7
  "8TUV",    // 8
  "9WXYZ"    // 9
};

// === STATE MANAGEMENT ===
enum State {
  STATE_PASSWORD,
  STATE_LOGIN,
  STATE_USER_LIST,
  STATE_VIEW_SEND_OPTION,
  STATE_VIEW_MESSAGE,
  STATE_COMPOSE_MESSAGE
};

State currentState = STATE_PASSWORD;

// === USER DATA ===
struct User {
  String user_id;
  String username;
};

User users[10];
int userCount = 0;
int selectedUserIndex = 0;
String selectedRecipientId = "";

// === PASSWORD ENTRY ===
String password = "1234";
String entered = "";

// === KEYPAD DEBOUNCING ===
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 200;
char lastKey = '\0';

// === MESSAGE COMPOSITION ===
String composedMessage = "";
unsigned long lastPressTime = 0;
char lastMultiTapKey = '\0';
int pressCount = 0;
const unsigned long multiTapTimeout = 1000;

// === MESSAGE DATA ===
String lastMessageDecrypted = "";

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);

  // LCD setup
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("AgniCom Chat");
  delay(1500);

  // Keypad pin setup
  for (int r = 0; r < 4; r++) {
    pinMode(rowPins[r], OUTPUT);
    digitalWrite(rowPins[r], HIGH);
  }
  for (int c = 0; c < 4; c++) {
    pinMode(colPins[c], INPUT_PULLUP);
  }

  // Connect to WiFi
  connectWiFi();

  lcd.clear();
  lcd.print("Enter Password:");
  currentState = STATE_PASSWORD;
}

void connectWiFi() {
  lcd.clear();
  lcd.print("Connecting WiFi");
  Serial.println("Connecting to WiFi...");

  WiFi.begin(ssid, password_wifi);
  int attempts = 0;

  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    lcd.setCursor(0, 1);
    lcd.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    lcd.clear();
    lcd.print("WiFi Connected!");
    Serial.println("\nWiFi Connected!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    delay(1500);
  } else {
    lcd.clear();
    lcd.print("WiFi Failed!");
    Serial.println("WiFi Connection Failed!");
    while (1);
  }
}

char scanKeypad() {
  for (int r = 0; r < 4; r++) {
    digitalWrite(rowPins[r], LOW);
    for (int c = 0; c < 4; c++) {
      if (digitalRead(colPins[c]) == LOW) {
        digitalWrite(rowPins[r], HIGH);
        return keys[r][c];
      }
    }
    digitalWrite(rowPins[r], HIGH);
  }
  return '\0';
}

bool loginUser(String pin) {
  if (WiFi.status() != WL_CONNECTED) {
    lcd.clear();
    lcd.print("No WiFi!");
    delay(1500);
    return false;
  }

  HTTPClient http;
  String url = String(API_BASE_URL) + "/login";

  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<200> doc;
  doc["user_id"] = MY_USER_ID;
  doc["pin"] = pin;

  String requestBody;
  serializeJson(doc, requestBody);

  int httpResponseCode = http.POST(requestBody);

  if (httpResponseCode > 0) {
    String response = http.getString();
    StaticJsonDocument<300> responseDoc;
    deserializeJson(responseDoc, response);

    if (responseDoc["message"] == "Login successful") {
      http.end();
      return true;
    }
  }

  http.end();
  return false;
}

void getAllUsers() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  String url = String(API_BASE_URL) + "/users";
  http.begin(url);
  int code = http.GET();

  if (code > 0) {
    String response = http.getString();
    DynamicJsonDocument doc(2048);
    deserializeJson(doc, response);

    JsonArray arr = doc["users"].as<JsonArray>();
    userCount = 0;

    for (JsonObject user : arr) {
      if (userCount >= 10) break;
      if (user["user_id"].as<String>() != MY_USER_ID) {
        users[userCount].user_id = user["user_id"].as<String>();
        users[userCount].username = user["username"].as<String>();
        userCount++;
      }
    }
  }
  http.end();
}

void displayUserList() {
  lcd.clear();
  if (userCount == 0) {
    lcd.print("No users found");
    return;
  }
  lcd.setCursor(0, 0);
  lcd.print("Select User:");
  lcd.setCursor(0, 1);
  String name = users[selectedUserIndex].username;
  if (name.length() > 16) name = name.substring(0, 16);
  lcd.print(name);
}

void getLastMessage(String senderId, String recipientId) {
  HTTPClient http;
  String url = String(API_BASE_URL) + "/messages/" + senderId + "/" + recipientId;
  http.begin(url);
  int code = http.GET();

  if (code > 0) {
    String response = http.getString();
    DynamicJsonDocument doc(4096);
    deserializeJson(doc, response);

    JsonArray msgs = doc.as<JsonArray>();
    if (msgs.size() > 0) {
      Serial.println(msgs);
      String msgId = msgs[0]["message_id"].as<String>();
      decryptMessage(msgId);
    } else {
      lcd.clear();
      lcd.print("No messages");
    }
  } else {
    lcd.clear();
    lcd.print("Failed to get msg");
  }
  http.end();
}

void decryptMessage(String messageId) {
  HTTPClient http;
  String url = String(API_BASE_URL) + "/messages/" + messageId + "/decrypt?user_id=" + MY_USER_ID;
  http.begin(url);
  int code = http.POST("");

  if (code > 0) {
    String response = http.getString();
    StaticJsonDocument<512> doc;
    deserializeJson(doc, response);
    lastMessageDecrypted = doc["decrypted_message"].as<String>();

    lcd.clear();
    lcd.print("Last Msg:");
    lcd.setCursor(0, 1);
    lcd.print(lastMessageDecrypted.substring(0, 16));
  } else {
    lcd.clear();
    lcd.print("Decrypt failed");
  }
  http.end();
  delay(2000);
}

void sendMessage(String recipientId, String msg) {
  HTTPClient http;
  String url = String(API_BASE_URL) + "/messages?sender_user_id=" + MY_USER_ID;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<300> doc;
  doc["recipient_user_id"] = recipientId;
  doc["message"] = msg;

  String body;
  serializeJson(doc, body);
  int code = http.POST(body);

  if (code > 0) {
    lcd.clear();
    lcd.print("Message Sent!");
  } else {
    lcd.clear();
    lcd.print("Send Failed!");
  }

  http.end();
  delay(1500);
}

char getMultiTapChar(char key, int count) {
  int index = key - '0';
  if (index >= 0 && index <= 9) {
    const char* chars = keyMap[index];
    int len = strlen(chars);
    return chars[count % len];
  }
  return key;
}

void updateMessageDisplay() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Msg: ");
  
  // Display the composed message (last 11 chars on first line)
  int startPos = max(0, (int)composedMessage.length() - 11);
  lcd.print(composedMessage.substring(startPos));
  
  lcd.setCursor(0, 1);
  lcd.print("*Del #Send D=OK");
}

void loop() {
  char key = scanKeypad();

  // Handle multi-tap timeout in compose mode
  if (currentState == STATE_COMPOSE_MESSAGE) {
    if (lastMultiTapKey != '\0' && (millis() - lastPressTime) > multiTapTimeout) {
      // Confirm the last character
      char finalChar = getMultiTapChar(lastMultiTapKey, pressCount);
      composedMessage += finalChar;
      lastMultiTapKey = '\0';
      pressCount = 0;
      updateMessageDisplay();
    }
  }

  if (key != '\0' && key != lastKey) {
    if (millis() - lastDebounceTime > debounceDelay) {
      handleKey(key);
      lastDebounceTime = millis();
      lastKey = key;
    }
  } else if (key == '\0') {
    lastKey = '\0';
  }
}

void handleKey(char key) {
  switch (currentState) {
    case STATE_PASSWORD:
      handlePassword(key);
      break;

    case STATE_LOGIN:
      handleLogin(key);
      break;

    case STATE_USER_LIST:
      handleUserList(key);
      break;

    case STATE_VIEW_SEND_OPTION:
      handleViewSendOption(key);
      break;

    case STATE_COMPOSE_MESSAGE:
      handleComposeMessage(key);
      break;

    default:
      break;
  }
}

void handlePassword(char key) {
  if (key == '*') {
    entered = "";
    lcd.clear();
    lcd.print("Enter Password:");
  } else if (key == '#') {
    if (entered == password) {
      lcd.clear();
      lcd.print("Access Granted");
      delay(1000);
      currentState = STATE_LOGIN;
      entered = "";
      lcd.clear();
      lcd.print("Enter PIN:");
      lcd.setCursor(0, 1);
      lcd.print("Press #");
    } else {
      lcd.clear();
      lcd.print("Access Denied");
      delay(1500);
      lcd.clear();
      lcd.print("Enter Password:");
      entered = "";
    }
  } else if (entered.length() < 4) {
    entered += key;
    lcd.setCursor(0, 1);
    for (int i = 0; i < entered.length(); i++) lcd.print('*');
  }
}

void handleLogin(char key) {
  if (key == '*') {
    entered = "";
    lcd.clear();
    lcd.print("Enter PIN:");
  } else if (key == '#') {
    if (entered.length() > 0) {
      lcd.clear();
      lcd.print("Logging in...");
      if (loginUser(entered)) {
        lcd.clear();
        lcd.print("Login OK");
        delay(1000);
        getAllUsers();
        currentState = STATE_USER_LIST;
        displayUserList();
      } else {
        lcd.clear();
        lcd.print("Login Failed");
        delay(1000);
        lcd.clear();
        lcd.print("Enter PIN:");
      }
      entered = "";
    }
  } else if (entered.length() < 6) {
    entered += key;
    lcd.setCursor(0, 1);
    for (int i = 0; i < entered.length(); i++) lcd.print('*');
  }
}

void handleUserList(char key) {
  if (key == 'A') {  // UP
    if (userCount > 0) {
      selectedUserIndex--;
      if (selectedUserIndex < 0) selectedUserIndex = userCount - 1;
      displayUserList();
    }
  } else if (key == 'B') {  // DOWN
    if (userCount > 0) {
      selectedUserIndex++;
      if (selectedUserIndex >= userCount) selectedUserIndex = 0;
      displayUserList();
    }
  } else if (key == 'C') {  // SELECT
    selectedRecipientId = users[selectedUserIndex].user_id;
    lcd.clear();
    lcd.print("Selected:");
    lcd.setCursor(0, 1);
    lcd.print(users[selectedUserIndex].username.substring(0, 16));
    delay(1000);
    lcd.clear();
    lcd.print("View[0] Send[1]");
    currentState = STATE_VIEW_SEND_OPTION;
  }
}

void handleViewSendOption(char key) {
  if (key == '0') {
    currentState = STATE_VIEW_MESSAGE;
    lcd.clear();
    lcd.print("Loading msg...");
    getLastMessage(MY_USER_ID, selectedRecipientId);
    currentState = STATE_USER_LIST;
    displayUserList();
  } else if (key == '1') {
    // Enter message composition mode
    currentState = STATE_COMPOSE_MESSAGE;
    composedMessage = "";
    lastMultiTapKey = '\0';
    pressCount = 0;
    updateMessageDisplay();
  } else if (key == 'C') {  // BACK
    currentState = STATE_USER_LIST;
    displayUserList();
  }
}

void handleComposeMessage(char key) {
  unsigned long now = millis();
  
  if (key >= '0' && key <= '9') {
    // Number key pressed - multi-tap mode
    if (key == lastMultiTapKey && (now - lastPressTime) < multiTapTimeout) {
      // Same key pressed again - cycle through characters
      pressCount++;
    } else {
      // Different key or timeout - confirm previous character
      if (lastMultiTapKey != '\0') {
        char finalChar = getMultiTapChar(lastMultiTapKey, pressCount);
        composedMessage += finalChar;
      }
      pressCount = 0;
      lastMultiTapKey = key;
    }
    lastPressTime = now;
    
    // Show current character being typed
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Msg: ");
    int startPos = max(0, (int)composedMessage.length() - 10);
    lcd.print(composedMessage.substring(startPos));
    lcd.print(getMultiTapChar(key, pressCount));
    
    lcd.setCursor(0, 1);
    lcd.print("*Del #Send D=OK");
    
  } else if (key == '*') {
    // Delete last character
    if (lastMultiTapKey != '\0') {
      // Cancel current multi-tap
      lastMultiTapKey = '\0';
      pressCount = 0;
    } else if (composedMessage.length() > 0) {
      // Delete last confirmed character
      composedMessage.remove(composedMessage.length() - 1);
    }
    updateMessageDisplay();
    
  } else if (key == '#') {
    // Confirm current character and send message
    if (lastMultiTapKey != '\0') {
      char finalChar = getMultiTapChar(lastMultiTapKey, pressCount);
      composedMessage += finalChar;
      lastMultiTapKey = '\0';
      pressCount = 0;
    }
    
    if (composedMessage.length() > 0) {
      lcd.clear();
      lcd.print("Sending...");
      sendMessage(selectedRecipientId, composedMessage);
      composedMessage = "";
      currentState = STATE_USER_LIST;
      displayUserList();
    } else {
      lcd.clear();
      lcd.print("Empty message!");
      delay(1000);
      updateMessageDisplay();
    }
    
  } else if (key == 'D') {
    // Confirm current character (OK button)
    if (lastMultiTapKey != '\0') {
      char finalChar = getMultiTapChar(lastMultiTapKey, pressCount);
      composedMessage += finalChar;
      lastMultiTapKey = '\0';
      pressCount = 0;
      updateMessageDisplay();
    }
    
  } else if (key == 'C') {
    // Cancel and go back
    composedMessage = "";
    lastMultiTapKey = '\0';
    pressCount = 0;
    currentState = STATE_USER_LIST;
    displayUserList();
  }
}