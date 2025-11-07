#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// WiFi ảo Wokwi
const char* ssid = "Wokwi-GUEST";
const char* password = "";

// HiveMQ broker
const char* mqtt_server = "6f280729577c4c89b2a0cfa0d588f93f.s1.eu.hivemq.cloud"; 
const int mqtt_port = 8883; 
const char* mqtt_user = "quocgc"; 
const char* mqtt_pass = "Quocvt123";

// Pins PWM cho LED RGB
const int RED_PIN = 25;
const int GREEN_PIN = 26;
const int BLUE_PIN = 27;
const int CH_RED = 0;
const int CH_GREEN = 1;
const int CH_BLUE = 2;
const int PWM_FREQ = 5000;
const int PWM_RES = 8;

// Cấu hình MQTT Client bảo mật, dùng WiFiClientSecure để hỗ trợ kết nối TLS đến broker HiveMQ.
WiFiClientSecure espClient; 
PubSubClient client(espClient);

String getValue(String data, String key);

// Cấu hình thứ tự kênh mà dashboard gửi
// Ví dụ: "RGB" (mặc định)
const String COLOR_ORDER = "RGB";

// Thứ tự kênh màu và chế độ LED
const bool INVERT_RED = false;
const bool INVERT_GREEN = false;
const bool INVERT_BLUE = false;

// Parser robust: lấy 3 số nguyên bất kể phân cách là , ; space, JSON array, v.v.
void parseRGB(String msg, int &r, int &g, int &b) {
  int vals[3] = {0, 0, 0};
  int idx = 0;
  String num = "";
  for (unsigned int i = 0; i < msg.length() && idx < 3; i++) {
    char c = msg.charAt(i);
    if ((c >= '0' && c <= '9')) {
      num += c;
    } else {
      if (num.length() > 0) {
        vals[idx++] = num.toInt();
        num = "";
      }
    }
  }
  if (num.length() > 0 && idx < 3) {
    vals[idx++] = num.toInt();
  }
  // Nếu chưa đủ 3 giá trị, giữ nguyên 0 cho phần còn lại
  // Ánh xạ theo COLOR_ORDER
  int mapped[3] = {0,0,0};
  for (int i = 0; i < 3; i++) mapped[i] = vals[i];
  if (COLOR_ORDER == "RGB") {
    r = mapped[0]; g = mapped[1]; b = mapped[2];
  } else if (COLOR_ORDER == "RBG") {
    r = mapped[0]; g = mapped[2]; b = mapped[1];
  } else if (COLOR_ORDER == "GRB") {
    r = mapped[1]; g = mapped[0]; b = mapped[2];
  } else if (COLOR_ORDER == "GBR") {
    r = mapped[2]; g = mapped[0]; b = mapped[1];
  } else if (COLOR_ORDER == "BRG") {
    r = mapped[1]; g = mapped[2]; b = mapped[0];
  } else if (COLOR_ORDER == "BGR") {
    r = mapped[2]; g = mapped[1]; b = mapped[0];
  } else {
    // fallback
    r = mapped[0]; g = mapped[1]; b = mapped[2];
  }
}

// Callback khi có tin nhắn MQTT đến
void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) message += (char)payload[i];
  Serial.println("Received: " + message);  // Debug

  int r = 0, g = 0, b = 0;

  // Dùng parser chung để bắt các định dạng như: "255,0,255", "255;0;255", "[255,0,255]", JSON object, hay chuỗi chứa số
  if (message.startsWith("#") && message.length() == 7) {
    long hex = strtol(message.substring(1).c_str(), NULL, 16);
    r = (hex >> 16) & 255;
    g = (hex >> 8) & 255;
    b = hex & 255;
  } else {
    parseRGB(message, r, g, b);
  }

  // Debug chi tiết để so sánh payload từ MQTTX và MQTT Panel
  Serial.println("Parsed RGB: " + String(r) + "," + String(g) + "," + String(b) + " (COLOR_ORDER=" + COLOR_ORDER + ")");

  if (r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255) {
    int dutyR = INVERT_RED ? (255 - r) : r;
    int dutyG = INVERT_GREEN ? (255 - g) : g;
    int dutyB = INVERT_BLUE ? (255 - b) : b;
    ledcWrite(CH_RED, dutyR);
    ledcWrite(CH_GREEN, dutyG);
    ledcWrite(CH_BLUE, dutyB);
    Serial.println("RGB set (input): " + String(r) + "," + String(g) + "," + String(b));
    Serial.println("PWM written (duty): " + String(dutyR) + "," + String(dutyG) + "," + String(dutyB));
  } else {
    Serial.println("Invalid RGB values, keeping previous state");
  }
}

// Helper function cho JSON simple
String getValue(String data, String key) {
  int start = data.indexOf(key + "\":") + key.length() + 3;
  int end = data.indexOf(',', start);
  if (end == -1) end = data.indexOf('}', start);
  return data.substring(start, end);
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect("WokwiESP32Client", mqtt_user, mqtt_pass)) {
      Serial.println("connected");
      client.subscribe("home/rgb/control"); 
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);

  // Cấu hình PWM
  ledcSetup(CH_RED, PWM_FREQ, PWM_RES);
  ledcSetup(CH_GREEN, PWM_FREQ, PWM_RES);
  ledcSetup(CH_BLUE, PWM_FREQ, PWM_RES);
  ledcAttachPin(RED_PIN, CH_RED);
  ledcAttachPin(GREEN_PIN, CH_GREEN);
  ledcAttachPin(BLUE_PIN, CH_BLUE);

  // Kết nối WiFi
  WiFi.begin(ssid, password, 6);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");

  // TLS cho HiveMQ (bỏ qua cert cho simulation)
  espClient.setInsecure();  // Không verify cert (chỉ cho test)
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
}

void loop() {
  if (!client.connected()) reconnect();
  client.loop();
}