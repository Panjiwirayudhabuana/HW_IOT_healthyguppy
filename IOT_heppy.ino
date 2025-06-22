#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ESP32Servo.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// WIFI SETUP
#define WIFI_SSID "gopix"
#define WIFI_PASSWORD "12345678"

// FIREBASE SETUP
const char* firebaseApiKey = "AIzaSyActIy26pwjmR2jrUIe5mIbRozs2NCW3T4";
const char* firebaseEmail = "heppy@gmail.com";
const char* firebasePassword = "qwerty7890";

String firebaseIdToken = "";
String firebaseRefreshToken = "";

// PIT SETUP
#define ultraTrigger 14
#define ultraEcho 12
#define pinServoPakan 23 
#define pinServoSensor 22       
#define relayBuzzer 25
#define relayBuzzerPasive 19
#define relayPump 26
#define relaySolenoid 27
#define pinSuhu 21
#define ledAktif 32    
#define ledWiFi 33     

//OBJECT
OneWire oneWire(pinSuhu);
DallasTemperature sensors(&oneWire);
Servo servoPakan;
Servo servoSensor;

// VARIABLE SETUP
float suhuAir = 0.0;
bool deteksiHama = false;
bool naik = true;
int pos = 0;
long duration;
float distance;
bool pompaAktif = false;
bool buzzerPasifAktif = false;
bool dataChanged = false;
SemaphoreHandle_t dataMutex;

// CONNECT WIFI
bool connectWiFi() {
  Serial.println("Menghubungkan ke WiFi...");
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  int timeout = 0;
  while (WiFi.status() != WL_CONNECTED && timeout < 40) { // 20 detik
    delay(500);
    Serial.print(".");
    timeout++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi terhubung!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    digitalWrite(ledWiFi, HIGH);
    return true;
  } else {
    Serial.println("\nGagal terhubung WiFi!");
    digitalWrite(ledWiFi, LOW);
    return false;
  }
}

//LOGIN FIREBASE
bool loginFirebase() {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;

  String url = "https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=" + String(firebaseApiKey);

  Serial.println("Login ke Firebase...");
  https.begin(client, url);
  https.addHeader("Content-Type", "application/json");

  String payload = "{\"email\":\"" + String(firebaseEmail) + "\",\"password\":\"" + String(firebasePassword) + "\",\"returnSecureToken\":true}";

  int httpCode = https.POST(payload);
  String response = https.getString();

  Serial.print("Kode HTTP Login: ");
  Serial.println(httpCode);

  if (httpCode == 200) {
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, response);
    
    if (error) {
      Serial.println("JSON parse error in login");
      https.end();
      return false;
    }

    firebaseIdToken = doc["idToken"].as<String>();
    firebaseRefreshToken = doc["refreshToken"].as<String>();

    Serial.println("Login Firebase berhasil!");
    https.end();
    return true;
  } else {
    Serial.println("Login Firebase gagal.");
    Serial.println("Response: " + response);
    https.end();
    return false;
  }
}

//REFRESH EXPIRED TOKEN
bool refreshFirebaseToken(int maxRetries = 3) {
  if (firebaseRefreshToken == "") {
    Serial.println("Refresh token kosong.");
    return false;
  }

  for (int attempt = 1; attempt <= maxRetries; attempt++) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient https;

    String url = "https://securetoken.googleapis.com/v1/token?key=" + String(firebaseApiKey);
    https.begin(client, url);
    https.addHeader("Content-Type", "application/x-www-form-urlencoded");
    https.setTimeout(15000);

    String postData = "grant_type=refresh_token&refresh_token=" + firebaseRefreshToken;
    int httpCode = https.POST(postData);
    String response = https.getString();
    https.end();

    Serial.printf("Refresh token (attempt %d) - HTTP: %d\n", attempt, httpCode);

    if (httpCode == 200) {
      DynamicJsonDocument doc(2048);
      DeserializationError error = deserializeJson(doc, response);
      
      if (!error) {
        firebaseIdToken = doc["id_token"].as<String>();
        firebaseRefreshToken = doc["refresh_token"].as<String>();
        Serial.println("Token berhasil di-refresh.");
        return true;
      } else {
        Serial.println("JSON parse error in refresh token");
      }
    } else {
      Serial.println("Gagal refresh token.");
      Serial.println("Response: " + response);
      
      if (attempt < maxRetries) {
        vTaskDelay(pdMS_TO_TICKS(5000 * attempt));
      }
    }
  }
  
  // Jika refresh gagal, coba login ulang
  Serial.println("Refresh token gagal, mencoba login ulang...");
  return loginFirebase();
}

//KIRIM DATA KE FIREBASE
// Tambahkan retry mechanism untuk HTTP requests
bool updateDataFirebase(float suhuBaru, String hamaBaru, int maxRetries = 3) {
  for (int attempt = 1; attempt <= maxRetries; attempt++) {
    if (firebaseIdToken == "") {
      Serial.println("Token belum tersedia. Login dulu.");
      return false;
    }

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient https;

    String projectId = "healthyguppy-95a43";
    String documentPath = "Monitoring/QiTZ7CtUxanTTlefUyOF";
    String url = "https://firestore.googleapis.com/v1/projects/" + projectId + "/databases/(default)/documents/" + documentPath;

    https.begin(client, url);
    https.addHeader("Authorization", "Bearer " + firebaseIdToken);
    https.addHeader("Content-Type", "application/json");
    https.setTimeout(10000); // 10 detik timeout

    String payload = "{"
                      "\"fields\": {"
                        "\"suhu\": {\"doubleValue\": " + String(suhuBaru, 2) + "},"
                        "\"hama\": {\"stringValue\": \"" + hamaBaru + "\"}"
                      "}"
                    "}";

    int httpCode = https.PATCH(payload);
    String response = https.getString();
    https.end();

    Serial.printf("Update Firebase (attempt %d) - HTTP: %d\n", attempt, httpCode);

    if (httpCode == 200) {
      return true;
    } else if (httpCode == 401 || httpCode == 403) {
      Serial.println("Token expired, refreshing...");
      refreshFirebaseToken();
      vTaskDelay(pdMS_TO_TICKS(2000));
    } else {
      Serial.println("Error response: " + response);
      if (attempt < maxRetries) {
        vTaskDelay(pdMS_TO_TICKS(2000 * attempt)); // Exponential backoff
      }
    }
  }
  return false;
}

// BUNYIKAN BUZZER PASIF (Dengan suara Ultrasonic)
void playUltrasonicSound() {
  for (int freq = 20000; freq <= 40000; freq += 2000) {
    tone(relayBuzzerPasive, freq, 30);
    delay(30);
  }
  noTone(relayBuzzerPasive);
}

// TASK SUHU (Cek suhu dan kendalikan waterpump & selenoid)
void taskSuhu(void *param) {
  while (true) {
    Serial.println("Membaca suhu...");
    sensors.requestTemperatures();
    float suhuBaru = sensors.getTempCByIndex(0);
    
    if (suhuBaru != DEVICE_DISCONNECTED_C && suhuBaru > -50 && suhuBaru < 100) {
      // Ambil mutex untuk update data
      if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(1000))) {
        suhuAir = suhuBaru;
        dataChanged = true;
        xSemaphoreGive(dataMutex);
      }
      
      Serial.print("Suhu: ");
      Serial.println(suhuAir);

      bool suhuTidakNormal = (suhuAir < 24 || suhuAir > 28);

      if (suhuTidakNormal && !pompaAktif) {
        Serial.println("Suhu tidak normal, aktifkan pompa & solenoid");
        digitalWrite(relayPump, HIGH);
        digitalWrite(relaySolenoid, HIGH);
        pompaAktif = true;
      } else if (!suhuTidakNormal && pompaAktif) {
        Serial.println("Suhu normal, nonaktifkan pompa & solenoid");
        digitalWrite(relayPump, LOW);
        digitalWrite(relaySolenoid, LOW);
        pompaAktif = false;
      }
    } else {
      Serial.println("Sensor suhu error!");
    }

    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}

//TASK ULTARSONIC (Cek objek dan kendalikan buzzer pasif & relay buzzer aktif)
void taskUltrasonic(void *param) {
  while (true) {
    float distance = medianUltrasonik();  // ⬅️ Ganti dari pulseIn biasa ke median filter

    Serial.print("Jarak: ");
    Serial.print(distance);
    Serial.println(" cm");

    bool valid = (distance > 2 && distance < 400);
    bool terdeteksi = valid && distance < 15;  // ⬅️ Atur ambang sesuai keperluan

    if (terdeteksi != deteksiHama) {
      if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(1000))) {
        deteksiHama = terdeteksi;
        dataChanged = true;
        xSemaphoreGive(dataMutex);
      }

      if (terdeteksi) {
        Serial.println("Hama terdeteksi! Aktifkan buzzer aktif dan pasif.");
        digitalWrite(relayBuzzer, HIGH);
        buzzerPasifAktif = true;
      } else {
        Serial.println("Hama tidak terdeteksi. Matikan semua buzzer.");
        digitalWrite(relayBuzzer, LOW);
        buzzerPasifAktif = false;
        noTone(relayBuzzerPasive);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

// Fungsi pembacaan satu kali
float bacaUltrasonik() {
  digitalWrite(ultraTrigger, LOW);
  delayMicroseconds(2);
  digitalWrite(ultraTrigger, HIGH);
  delayMicroseconds(10);
  digitalWrite(ultraTrigger, LOW);

  long durasi = pulseIn(ultraEcho, HIGH, 30000); // Timeout 30ms

  if (durasi == 0) return 999;  // Timeout dianggap tidak ada objek
  return durasi * 0.0344 / 2;
}

// Fungsi mengambil median dari 5 pembacaan
float medianUltrasonik() {
  float data[5];
  for (int i = 0; i < 5; i++) {
    data[i] = bacaUltrasonik();
    vTaskDelay(pdMS_TO_TICKS(20));  // Delay antar pembacaan
  }

  // Sort array
  for (int i = 0; i < 5 - 1; i++) {
    for (int j = i + 1; j < 5; j++) {
      if (data[j] < data[i]) {
        float tmp = data[i];
        data[i] = data[j];
        data[j] = tmp;
      }
    }
  }

  return data[2]; // Median = elemen tengah
}

//TASK BUUZZER PASIF (bunyikan buzzer dengan suara ultrasonic)
void taskBuzzerPasif(void *param) {
  while (true) {
    if (buzzerPasifAktif) {
      playUltrasonicSound();
      vTaskDelay(pdMS_TO_TICKS(100));
    } else {
      vTaskDelay(pdMS_TO_TICKS(500));
    }
  }
}

//TASK SERVO GERAK
void taskServoGerak(void *param) {
  while (true) {
    if (!deteksiHama) {
      if (naik) {
        pos++;
        if (pos >= 180) naik = false;
      } else {
        pos--;
        if (pos <= 0) naik = true;
      }
      servoSensor.write(pos);
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

//GET DATA PAKAN 200 = TRUE
bool getDocumentField(const String& path, const String& fieldName, String &result) {
  if (firebaseIdToken == "") {
    Serial.println("Token belum tersedia untuk get document.");
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;

  String projectId = "healthyguppy-95a43";
  String url = "https://firestore.googleapis.com/v1/projects/" + projectId + "/databases/(default)/documents/" + path;

  https.begin(client, url);
  https.addHeader("Authorization", "Bearer " + firebaseIdToken);
  https.addHeader("Content-Type", "application/json");

  int httpCode = https.GET();
  String response = https.getString();

  Serial.print("Get document - HTTP Code: ");
  Serial.println(httpCode);
  
  if (httpCode == 200) {
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, response);
    
    if (!error && doc["fields"][fieldName]["stringValue"]) {
      result = doc["fields"][fieldName]["stringValue"].as<String>();
      https.end();
      return true;
    } else if (error) {
      Serial.println("JSON parse error in getDocumentField");
    }
  } else if (httpCode == 403) {
    Serial.println("Error 403: Token expired atau tidak valid");
    Serial.println("Response: " + response);
  } else {
    Serial.println("Error response: " + response);
  }

  https.end();
  return false;
}

//PUT DATA PAKAN MENJADI TUTUP 
bool updateDocumentField(const String& path, const String& fieldName, const String& value) {
  if (firebaseIdToken == "") {
    Serial.println("Token belum tersedia untuk update document.");
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;

  String projectId = "healthyguppy-95a43";
  String url = "https://firestore.googleapis.com/v1/projects/" + projectId + "/databases/(default)/documents/" + path;

  https.begin(client, url);
  https.addHeader("Authorization", "Bearer " + firebaseIdToken);
  https.addHeader("Content-Type", "application/json");

  String payload = "{"
                    "\"fields\": {"
                      "\"" + fieldName + "\": {\"stringValue\": \"" + value + "\"}"
                    "}"
                  "}";

  int httpCode = https.PATCH(payload);
  String response = https.getString();

  Serial.print("Update document - HTTP Code: ");
  Serial.println(httpCode);
  
  if (httpCode != 200) {
    Serial.println("Update error response: " + response);
    if (httpCode == 403) {
      Serial.println("Error 403: Token expired atau tidak valid untuk update");
    }
  }

  https.end();
  return httpCode == 200;
}

//TASK PAKAN OTOMATIS
void taskCekPakan(void *parameter) {
  vTaskDelay(pdMS_TO_TICKS(5000)); // Tunggu sistem stabil
  
  while (true) {
    if (WiFi.status() == WL_CONNECTED && firebaseIdToken != "") {
      String pakanStatus;
      String path = "Action/qrPLUhAZbg2oUnAyoYtm";
      
      if (getDocumentField(path, "pakan", pakanStatus)) {
        Serial.print("Status pakan: ");
        Serial.println(pakanStatus);

        if (pakanStatus == "buka") {
          Serial.println("Membuka pakan...");
          servoPakan.write(0);
          vTaskDelay(pdMS_TO_TICKS(1000));
          servoPakan.write(60);
          Serial.println("Servo pakan kembali ke posisi 60");

          if (updateDocumentField(path, "pakan", "tutup")) {
            Serial.println("Status pakan berhasil diupdate ke 'tutup'");
          } else {
            Serial.println("Gagal update status pakan");
          }
        }
      } else {
        Serial.println("Gagal membaca status pakan dari Firebase");
        // Coba refresh token jika error 403
        Serial.println("Mencoba refresh token...");
        refreshFirebaseToken();
        vTaskDelay(pdMS_TO_TICKS(2000)); // Tunggu sebentar setelah refresh
      }
    } else {
      Serial.println("WiFi atau Firebase token tidak tersedia");
      // Coba login ulang jika token kosong
      if (WiFi.status() == WL_CONNECTED && firebaseIdToken == "") {
        Serial.println("Mencoba login ulang Firebase...");
        loginFirebase();
      }
    }

    vTaskDelay(pdMS_TO_TICKS(8000));  // cek setiap 8 detik
  }
}

//SYNC FIREBASE (hanya update ketika berubah)
void taskFirebaseSync(void *param) {
  vTaskDelay(pdMS_TO_TICKS(3000)); // Tunggu sistem stabil
  
  while (true) {
    bool needUpdate = false;
    float currentSuhu = 0;
    bool currentHama = false;

    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(1000))) {
      if (dataChanged) {
        needUpdate = true;
        currentSuhu = suhuAir;
        currentHama = deteksiHama;
        dataChanged = false;
      }
      xSemaphoreGive(dataMutex);
    }

    if (needUpdate && WiFi.status() == WL_CONNECTED && firebaseIdToken != "") {
      String hamaStatus = currentHama ? "Hama Terdeteksi" : "Tidak ada hama";
      Serial.println("Sinkronisasi data ke Firebase...");

      if (!updateDataFirebase(currentSuhu, hamaStatus)) {
        Serial.println("Gagal update Firebase, coba refresh token...");
        refreshFirebaseToken();
      }
    }

    vTaskDelay(pdMS_TO_TICKS(3000));
  }
}

//REFRESH TOKEN
void taskRefreshToken(void *param) {
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(3300000)); // 55 menit
    Serial.println("Refresh token otomatis...");
    refreshFirebaseToken();
  }
}

//RECONNECT WIFI & RELOGIN FIREBASE
void taskWiFiMonitor(void *param) {
  int reconnectAttempts = 0;
  const int maxReconnectAttempts = 5;
  
  while (true) {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.printf("WiFi terputus (attempt %d/%d)...\n", reconnectAttempts + 1, maxReconnectAttempts);
      digitalWrite(ledWiFi, LOW);
      
      if (connectWiFi()) {
        Serial.println("WiFi reconnected successfully!");
        reconnectAttempts = 0; // Reset counter
        
        // Login ulang Firebase setelah WiFi reconnect
        vTaskDelay(pdMS_TO_TICKS(3000));
        if (loginFirebase()) {
          Serial.println("Firebase re-login successful!");
        }
      } else {
        reconnectAttempts++;
        if (reconnectAttempts >= maxReconnectAttempts) {
          Serial.println("Max reconnect attempts reached. Restarting ESP32...");
          ESP.restart();
        }
        vTaskDelay(pdMS_TO_TICKS(30000)); // Wait longer between attempts
      }
    } else {
      reconnectAttempts = 0; // Reset if connected
    }
    
    vTaskDelay(pdMS_TO_TICKS(10000)); // Check every 10 seconds
  }
}

// SETUP
void setup() {
  Serial.begin(115200);
  delay(2000);

  // Setup pin
  pinMode(ultraTrigger, OUTPUT);
  pinMode(ultraEcho, INPUT);
  pinMode(relayBuzzer, OUTPUT);
  pinMode(relayBuzzerPasive, OUTPUT);
  pinMode(relayPump, OUTPUT);
  pinMode(relaySolenoid, OUTPUT);
  pinMode(ledAktif, OUTPUT);
  pinMode(ledWiFi, OUTPUT);
  
  // Set initial states
  digitalWrite(relayBuzzer, LOW);
  digitalWrite(relayBuzzerPasive, LOW);
  digitalWrite(relayPump, LOW);
  digitalWrite(relaySolenoid, LOW);  
  digitalWrite(ledAktif, HIGH);
  digitalWrite(ledWiFi, LOW);

  // Initialize sensors
  sensors.begin();
  Serial.print("Jumlah sensor suhu: ");
  Serial.println(sensors.getDeviceCount());

  // Initialize servos
  servoPakan.attach(pinServoPakan);
  servoSensor.attach(pinServoSensor);
  
  servoPakan.write(60);
  servoSensor.write(0);
  delay(1000);

  // Create mutex
  dataMutex = xSemaphoreCreateMutex();
  if (dataMutex == NULL) {
    Serial.println("Failed to create mutex!");
    ESP.restart();
  }

  // Koneksi WiFi dan Firebase
  if (connectWiFi()) {
    delay(2000);
    if (loginFirebase()) {
      Serial.println("Sistem siap!");
    }
  }

  // Create tasks with proper stack sizes and priorities
  xTaskCreatePinnedToCore(taskSuhu, "taskSuhu", 6144, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(taskUltrasonic, "taskUltrasonic", 4096, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(taskBuzzerPasif, "taskBuzzerPasif", 2048, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(taskServoGerak, "taskServoGerak", 2048, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(taskCekPakan, "CekPakanTask", 10240, NULL, 3, NULL, 1);   
  xTaskCreatePinnedToCore(taskFirebaseSync, "taskFirebaseSync", 10240, NULL, 2, NULL, 1);   
  xTaskCreatePinnedToCore(taskRefreshToken, "taskRefreshToken", 8192, NULL, 1, NULL, 1);   
  xTaskCreatePinnedToCore(taskWiFiMonitor, "taskWiFiMonitor", 8192, NULL, 1, NULL, 0);     

  Serial.println("Semua task berjalan!");
  Serial.print("Free heap: ");
  Serial.println(ESP.getFreeHeap());
}

void loop() {  
  // Print memory status periodically
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 30000) { // every 30 seconds
    lastPrint = millis();
    Serial.print("Free heap: ");
    Serial.println(ESP.getFreeHeap());
  }
  
  vTaskDelay(pdMS_TO_TICKS(1000));
}