#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ESP32Servo.h>

const char* ssid = "Wokwi-GUEST";
const char* password = "";

String firebaseUrl = "https://phan-loai-rac-default-rtdb.asia-southeast1.firebasedatabase.app";
const char* firebaseSecret = "SVGLYWM6I546bNRZUKoH0VFCYA6ChQVmyNbXHxMB";

Servo servoTang1;
Servo servoTang2;

#define PIN_SERVO_TANG1 13
#define PIN_SERVO_TANG2 14

// Siêu âm 2, 3, 4 (Đo mức rác báo đầy cho 3 thùng)
#define TRIG_HC_HUUCO 27
#define ECHO_HC_HUUCO 15

#define TRIG_HC_NHUA 18
#define ECHO_HC_NHUA 19

#define TRIG_HC_GIAY 32
#define ECHO_HC_GIAY 33

// Hàm đo khoảng cách
int doKhoangCach(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  
  long duration = pulseIn(echoPin, HIGH, 30000); 
  if (duration == 0) return 999; 
  
  int distance = duration * 0.034 / 2;
  return distance;
}

// Hàm PUT dữ liệu lên Firebase
void sendToFirebase(String path, String data) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(firebaseUrl + path);
    http.addHeader("Content-Type", "application/json");
    http.PUT(data);
    http.end();
  }
}

// Hàm GET dữ liệu từ Firebase
String getFromFirebase(String path) {
  String payload = "";
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(firebaseUrl + path);
    int httpCode = http.GET();
    if (httpCode > 0) {
      payload = http.getString();
      payload.replace("\"", ""); // Xóa dấu ngoặc kép bọc ngoài chuỗi
    }
    http.end();
  }
  return payload;
}

void setup() {
  Serial.begin(115200);
  Serial.print("\nKet noi WiFi...");
  
  WiFi.begin(ssid, password, 6); // Thêm số 6 cho Wokwi kết nối nhanh
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi OK!");
  
  // Cấu hình chân Cảm biến báo đầy
  pinMode(TRIG_HC_HUUCO, OUTPUT); pinMode(ECHO_HC_HUUCO, INPUT);
  pinMode(TRIG_HC_NHUA, OUTPUT); pinMode(ECHO_HC_NHUA, INPUT);
  pinMode(TRIG_HC_GIAY, OUTPUT); pinMode(ECHO_HC_GIAY, INPUT);

  // Cấu hình Servo tầng phân loại
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  
  servoTang1.attach(PIN_SERVO_TANG1);
  servoTang2.attach(PIN_SERVO_TANG2);

  // Reset Servo về cân bằng
  servoTang1.write(90);   
  servoTang2.write(90); 

  Serial.println("ESP32 PHAN LOAI - San sang nhan lenh tu AI!");
}

// ================= HÀM XỬ LÝ LOGIC PHÂN LOẠI =================
void phanLoaiHuuCo() {
  Serial.println("-> Thuc thi: Gat rac HUU CO");
  servoTang1.write(45); 
  delay(1500);          
  servoTang1.write(90); 
}

void phanLoaiNhua() {
  Serial.println("-> Thuc thi: Gat rac NHUA");
  servoTang1.write(135); 
  delay(500);           
  servoTang2.write(45); 
  delay(1500);
  servoTang1.write(90); 
  servoTang2.write(90);
}

void phanLoaiGiay() {
  Serial.println("-> Thuc thi: Gat rac GIAY");
  servoTang1.write(135); 
  delay(500);
  servoTang2.write(135); 
  delay(1500);           
  servoTang1.write(90); 
  servoTang2.write(90);
}

// ================= VÒNG LẶP CHÍNH =================
void loop() {
  // 1. LIÊN TỤC LẮNG NGHE LỆNH TỪ AI (Cứ mỗi 1 giây check 1 lần)
  String trashCategory = getFromFirebase("/trash_type/category.json");
  
  // Nếu Firebase có chữ H, N, hoặc G
  if (trashCategory == "H" || trashCategory == "N" || trashCategory == "G") {
    Serial.println("\n[NHAN LENH] AI tra ve ket qua: " + trashCategory);
    
    // Phân loại
    if (trashCategory == "H") phanLoaiHuuCo();
    else if (trashCategory == "N") phanLoaiNhua();
    else if (trashCategory == "G") phanLoaiGiay();

    // Phân loại xong phải lập tức reset Firebase về rỗng để tránh bị gạt liên tục
    Serial.println("[ESP32] Đang dọn dẹp băng thông, reset phiên làm việc...");
      
    // Reset obstacle về 0
    sendToFirebase("/sensors/pir/obstacle.json", "0");

    // Xóa trống category
    sendToFirebase("/trash_type/category.json", "\"\"");

    Serial.println("[ESP32] Sẵn sàng cho lượt quét tiếp theo.\n");
  }

  // 2. KIỂM TRA TÌNH TRẠNG ĐẦY THÙNG
  int kc_HuuCo = doKhoangCach(TRIG_HC_HUUCO, ECHO_HC_HUUCO);
  int kc_Nhua = doKhoangCach(TRIG_HC_NHUA, ECHO_HC_NHUA);
  int kc_Giay = doKhoangCach(TRIG_HC_GIAY, ECHO_HC_GIAY);

  if (kc_HuuCo < 5) sendToFirebase("/sensors/hc-sr04/status_huuco.json", "\"FULL\"");
  else sendToFirebase("/sensors/hc-sr04/status_huuco.json", "\"NOT_FULL\"");

  if (kc_Nhua < 5) sendToFirebase("/sensors/hc-sr04/status_nhua.json", "\"FULL\"");
  else sendToFirebase("/sensors/hc-sr04/status_nhua.json", "\"NOT_FULL\"");

  if (kc_Giay < 5) sendToFirebase("/sensors/hc-sr04/status_giay.json", "\"FULL\"");
  else sendToFirebase("/sensors/hc-sr04/status_giay.json", "\"NOT_FULL\"");

  delay(1000); // Nghỉ 1 giây để không Spam Firebase quá đà
}