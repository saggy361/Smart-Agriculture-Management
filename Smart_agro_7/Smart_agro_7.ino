#include <TinyGPS++.h>
#include <DHT.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// WiFi credentials
const char* ssid = "esp-access";
const char* password = "87654321";

// MAX485 control pins (you can adjust these to any available GPIO pins on the ESP32)
#define RE 32
#define DE 33

// Sensor pin definitions
#define SOIL_MOISTURE_PIN 35  // Soil moisture sensor analog pin
#define RAIN_SENSOR_PIN 34    // Rain sensor analog pin
#define DHT_PIN 26            // DHT11 sensor pin
#define DHT_TYPE DHT11        // Define the DHT sensor type
#define PIR_SENSOR_PIN 27     // PIR sensor digital pin

// LED pins
#define LED1_PIN 12           // LED for soil moisture condition
#define LED2_PIN 13           // LED for NPK condition

// Command bytes for Modbus RTU communication
const byte nitro[] = {0x01, 0x03, 0x00, 0x1e, 0x00, 0x01, 0xe4, 0x0c};  // Nitrogen
const byte phos[]  = {0x01, 0x03, 0x00, 0x1f, 0x00, 0x01, 0xb5, 0xcc};  // Phosphorous
const byte pota[]  = {0x01, 0x03, 0x00, 0x20, 0x00, 0x01, 0x85, 0xc0};  // Potassium

// GPS module (UART1 on ESP32)
TinyGPSPlus gps;
//HardwareSerial GPS_Serial(1);  // Use UART1 for GPS (TX=22, RX=23)

// pH Sensor (UART2 on ESP32, for example using TX=18, RX=19)
HardwareSerial PH_Serial(1);   // Use UART2 for pH sensor (TX=18, RX=19)

// Use UART2 for communication (TX=17, RX=16 by default)
HardwareSerial mod(2);

// DHT sensor
DHT dht(DHT_PIN, DHT_TYPE);

// API endpoint
const char* api_endpoint = "http://13.60.36.216/api/insert_details";

byte values[11];
unsigned long previousMillis = 0;  // Store the last time sendDataToAPI was called
const long interval = 30000;       // Interval to call sendDataToAPI (1 minute)

void setup() {
  // Initialize serial monitor for debugging
  Serial.begin(115200);

  // Initialize WiFi connection
  connectToWiFi();

  // Initialize UART2 for RS485 communication
  mod.begin(9600, SERIAL_8N1, 16, 17); // RX=16, TX=17 (you can adjust these pins if necessary)

  // Initialize pH sensor communication (UART2, pH sensor connected to RX=19, TX=18)
  PH_Serial.begin(9600, SERIAL_8N1, 19, 18);

  // Set RE and DE pins for RS485
  pinMode(RE, OUTPUT);
  pinMode(DE, OUTPUT);

  // Initialize LED pins
  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);

  // Initialize soil moisture and rain sensor pins
  pinMode(SOIL_MOISTURE_PIN, INPUT);
  pinMode(RAIN_SENSOR_PIN, INPUT);

  // Initialize PIR sensor pin
  pinMode(PIR_SENSOR_PIN, INPUT);

  // Initialize DHT sensor
  dht.begin();

  Serial.println("NPK Sensor, Soil Moisture, Rain Sensor, GPS, DHT11, PIR Sensor, and pH Sensor Communication Initialized...");
}

void loop() {
  byte val1, val2, val3;
  int soilMoisturePercentage;
  String rainDescription;
  float latitude, longitude;
  latitude= 22.5553608;
  longitude= 88.488099;
  float temperature, humidity;
  bool motionDetected;
  String phValue = "";  // Default pH value in case parsing fails

  // Fetch Nitrogen, Phosphorous, and Potassium values
  val1 = nitrogen();
  val2 = phosphorous();
  val3 = potassium();

  // Fetch soil moisture and rain sensor values
  soilMoisturePercentage = readSoilMoisture();
  rainDescription = readRainSensor();  // Get rain description as string

  // Fetch temperature and humidity from DHT11 sensor
  temperature = dht.readTemperature();
  humidity = dht.readHumidity();

  // Read PIR sensor state (HIGH = motion detected, LOW = no motion)
  motionDetected = digitalRead(PIR_SENSOR_PIN);

  // Fetch and parse the pH sensor data over UART
  phValue = fetchPHData();

  // Control LED1 based on soil moisture value
  bool water_pump_status = false;  // Default to off
  if (soilMoisturePercentage < 20) {
    digitalWrite(LED1_PIN, HIGH);  // Turn on LED1
    water_pump_status = true;      // Set water pump status to on
  } else if (soilMoisturePercentage > 70) {
    digitalWrite(LED1_PIN, LOW);   // Turn off LED1
    water_pump_status = false;     // Set water pump status to off
  }

  // Control LED2 based on NPK values (Nitrogen, Phosphorus, and Potassium)
  bool fertilizer_pump_status = false;  // Default to off
  if (val1 < 40 && val2 < 40 && val3 < 80) {
    digitalWrite(LED2_PIN, HIGH);  // Turn on LED2
    fertilizer_pump_status = true;  // Set fertilizer pump status to on
  } else if (val1 > 80 && val2 > 80 && val3 > 80) {
    digitalWrite(LED2_PIN, LOW);   // Turn off LED2
    fertilizer_pump_status = false;  // Set fertilizer pump status to off
  }

  //Print values from all sensors before forming the JSON object
  Serial.println("Sensor Values:");
  Serial.print("Nitrogen: ");
  Serial.println(val1);
  Serial.print("Phosphorous: ");
  Serial.println(val2);
  Serial.print("Potassium: ");
  Serial.println(val3);
  Serial.print("Soil Moisture: ");
  Serial.println(soilMoisturePercentage);
  Serial.print("Rainfall: ");
  Serial.println(rainDescription);
  Serial.print("Temperature: ");
  Serial.println(temperature);
  Serial.print("Humidity: ");
  Serial.println(humidity);
  Serial.print("Latitude: ");
  Serial.println(latitude, 6);
  Serial.print("Longitude: ");
  Serial.println(longitude, 6);
  Serial.print("Motion Detected: ");
  Serial.println(motionDetected ? "Yes" : "No");
  Serial.print("pH Value: ");
  Serial.println(phValue);

  // Get the current time in milliseconds
  unsigned long currentMillis = millis();

  // Check if 1 minute (60000 ms) has passed
  if (currentMillis - previousMillis >= interval) {
    // Save the last time sendDataToAPI was called
    previousMillis = currentMillis;

    // Prepare the JSON object
    StaticJsonDocument<500> jsonDoc;
    jsonDoc["nitrogen"] = String(val1);
    jsonDoc["phosphorus"] = String(val2);
    jsonDoc["potassium"] = String(val3);
    jsonDoc["soil_moisture"] = String(soilMoisturePercentage);
    jsonDoc["temperature"] = String(temperature);
    jsonDoc["humidity"] = String(humidity);
    jsonDoc["rainfall"] = rainDescription;  // Use the string describing the rainfall
    jsonDoc["ph"] = phValue;  // Use the parsed pH value
    jsonDoc["latitude"] = String(latitude, 6);
    jsonDoc["longitude"] = String(longitude, 6);
    jsonDoc["motion_status"] = motionDetected;
    jsonDoc["water_pump_status"] = water_pump_status;  // Set water_pump_status based on LED1
    jsonDoc["fertilizer_pump_status"] = fertilizer_pump_status;  // Set fertilizer_pump_status based on LED2
    jsonDoc["fertilizer_level"] = 50;  // Example value (replace with actual sensor data)
    jsonDoc["battery_status"] = 99;  // Example value (replace with actual sensor data)
    jsonDoc["override_fertilizer_pump"] = false;  // Example value
    jsonDoc["override_water_pump"] = true;  // Example value

    // Send the JSON data to the API
    sendDataToAPI(jsonDoc);
  }

  // Delay of 1 second for the rest of the loop
  delay(1000);
}

byte nitrogen() {
  return readSensor(nitro);
}

byte phosphorous() {
  return readSensor(phos);
}

byte potassium() {
  return readSensor(pota);
}

byte readSensor(const byte* command) {
  // Send command to the sensor via RS485
  digitalWrite(DE, HIGH);
  digitalWrite(RE, HIGH);
  delay(10);

  mod.write(command, 8);
  delay(10); // Wait for the command to be sent

  // Switch to receive mode
  digitalWrite(DE, LOW);
  digitalWrite(RE, LOW);

  delay(100); // Wait for sensor response

  // Read the response (7 bytes expected: 1 device address, 1 function code, 1 byte count, 2 data bytes, 2 CRC bytes)
  if (mod.available()) {
    for (int i = 0; i < 7; i++) {
      values[i] = mod.read();
    }

    // Return the data byte (values[3] and values[4] make up the 16-bit data)
    return (values[3] << 8) | values[4];  // Assuming the data is in bytes 3 and 4
  } else {
    Serial.println("No data received");
    return 0;
  }
}

int readSoilMoisture() {
  // Read the analog value from the soil moisture sensor
  int soilMoistureValue = analogRead(SOIL_MOISTURE_PIN);

  // Map the analog value (typically 0-4095) to a percentage (0-100%)
  int soilMoisturePercentage = map(soilMoistureValue, 4095, 0, 0, 100);

  return soilMoisturePercentage;
}

// Read rain sensor and return the rainfall description as a string
String readRainSensor() {
  // Read the analog value from the rain sensor
  int rainValue = analogRead(RAIN_SENSOR_PIN);

  // Map the analog value (typically 0-4095) to a percentage (0-100%)
  int rainPercentage = map(rainValue, 4095, 0, 0, 100);

  // Return rainfall description based on the percentage
  if (rainPercentage < 5) {
    return "No Rainfall";
  } else if (rainPercentage >= 5 && rainPercentage < 30) {
    return "Light Rainfall";
  } else if (rainPercentage >= 30 && rainPercentage < 60) {
    return "Moderate Rainfall";
  } else {
    return "Heavy Rainfall";
  }
}

// Fetch and parse the pH data from UART (PH:8.27, W:35, L:18, T:28 format)
String fetchPHData() {
  String pHData = "";
  String phValue = "";  

  // Check if data is available from the pH sensor
  while (PH_Serial.available() > 0) {
    char c = PH_Serial.read();
    pHData += c;
  }

  // Parse the pH value if the format is correct
  if (true) {
    int startIndex = pHData.indexOf("PH:") + 3;
    int endIndex = pHData.indexOf(",", startIndex);
    if (startIndex != -1 && endIndex != -1) {
      phValue = pHData.substring(startIndex, endIndex);
      //Serial.print("Parsed pH Value: ");
      //Serial.println(phValue);
    }
  } else {
    Serial.println("pH data not received or incorrect format");
  }

  return phValue;
}

void connectToWiFi() {
  Serial.print("Connecting to WiFi...");
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("Connected to WiFi");
}

void sendDataToAPI(const JsonDocument& jsonDoc) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;

    http.begin(api_endpoint);  // Specify the URL
    http.addHeader("Content-Type", "application/json");  // Specify content-type header

    // Serialize the JSON document to a string
    String requestBody;
    serializeJson(jsonDoc, requestBody);

    // Send the POST request
    int httpResponseCode = http.POST(requestBody);

    // Print the response code
    if (httpResponseCode > 0) {
      String response = http.getString();  // Get the response to the request
      Serial.print("HTTP Response code: ");
      Serial.println(httpResponseCode);
      Serial.println("Response from server: ");
      Serial.println(response);
    } else {
      Serial.print("Error on sending POST: ");
      Serial.println(httpResponseCode);
    }

    // End the HTTP connection
    http.end();
  } else {
    Serial.println("WiFi not connected");
  }
}
