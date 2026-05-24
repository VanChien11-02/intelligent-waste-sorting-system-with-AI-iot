#include <Arduino.h>
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ESP32Servo.h>

// Thông tin WiFi của Wokwi
const char* ssid = "Wokwi-GUEST";
const char* password = "";

// Cấu hình Firebase
String firebaseUrl = "https://phan-loai-rac-default-rtdb.asia-southeast1.firebasedatabase.app/sensors/pir/obstacle.json";
const char* firebaseSecret = "SVGLYWM6I546bNRZUKoH0VFCYA6ChQVmyNbXHxMB";

int sensorPin = 27; // Chân GPIO kết nối OUT của PIR

Servo servoNap;
#define PIN_SERVO_NAP 12

// Siêu âm 1 (Đo người đến gần để mở nắp)
#define TRIG_NAP 25
#define ECHO_NAP 26

int khoangCachNap = 0;

int doKhoangCach(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  
  long duration = pulseIn(echoPin, HIGH, 30000); // Timeout 30ms để tránh kẹt code
  if (duration == 0) return 999; // Lỗi không đọc được
  
  int distance = duration * 0.034 / 2;
 return distance;
}

void setup() {
  Serial.begin(115200);
  pinMode(sensorPin, INPUT);

  pinMode(TRIG_NAP, OUTPUT); pinMode(ECHO_NAP, INPUT);

  servoNap.attach(PIN_SERVO_NAP);

  // cân bằng
  servoNap.write(90);

  // Kết nối WiFi
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" Connected!");
}

void loop() {

  khoangCachNap = doKhoangCach(TRIG_NAP, ECHO_NAP);

  if (khoangCachNap > 0 && khoangCachNap < 30) {
    Serial.println("Moi bo rac vao!");
    servoNap.write(0);
    delay(5000);
    servoNap.write(90);
    delay(2000);
  }

  // Đọc cảm biến PIR
  int pirState = digitalRead(sensorPin);

  if (pirState == 1 && WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[ESP32] Có rác! Báo cho máy tính...");
    
    HTTPClient http;
    // Bắn lệnh obstacle = 1 lên Firebase
    http.begin(firebaseUrl);
    http.addHeader("Content-Type", "application/json");
    http.PUT("1");
    http.end();

    // 2. Vòng lặp đứng chờ Tool C# xử lý ảnh và trả về kết quả phân loại
    String trashCategory = "";
    Serial.print("[ESP32] Đang đợi AI trên máy tính phân loại rác");
    
    int cnt = 0;
    bool check = true;
    while (trashCategory == "" || trashCategory == "null") {
      delay(1000); // Cứ 1 giây kiểm tra Firebase 1 lần
      Serial.print(".");
      Serial.println();
      cnt++;
      if(cnt > 10){
        http.begin(firebaseUrl);
        http.PUT("0");
        http.end();
        Serial.println("Không nhận được phản hồi");
        check = false;
        break;
      }
      
      http.begin("https://phan-loai-rac-default-rtdb.asia-southeast1.firebasedatabase.app/trash_type/category.json");
      int httpCode = http.GET();
      if (httpCode > 0) {
        trashCategory = http.getString();
        trashCategory.replace("\"", ""); // Xóa bỏ dấu ngoặc kép thừa nếu có
      }
      http.end();
    }

    if(check == true){
      // 3. Đã nhận được kết quả phân loại từ AI đám mây
      Serial.println("\n[ESP32] -> AI ĐÃ PHÂN LOẠI XONG!");
      Serial.print("[ESP32] Loại rác nhận được là: ");
      Serial.println(trashCategory);

      delay(5000); // Giả lập chờ người ta vứt rác vào trong 5 giây
    }
  }

  delay(1000); // Tránh quét quá dày khi không có người
}