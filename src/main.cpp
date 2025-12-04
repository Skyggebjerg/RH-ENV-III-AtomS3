#include <Arduino.h>

/**
 * @file ENV_III.ino
 * @author SeanKwok (shaoxiang@m5stack.com)
 * @brief
 * @version 0.2
 * @date 2024-07-18
 *
 *
 * @Hardwares: M5AtomS3 + Unit ENV_III
 * @Platform Version: Arduino M5Stack Board Manager v2.1.0
 * @Dependent Library:
 * M5UnitENV: https://github.com/m5stack/M5Unit-ENV
 */

//#include "M5AtomS3.h"
#include <M5GFX.h>
#include "M5UnitENV.h"
#include <WiFi.h>
#include <WebServer.h>

SHT3X sht3x;
QMP6988 qmp;

M5GFX display;
M5Canvas canvas(&display);

// WiFi AP credentials
const char* ssid = "AtomS3-RH-Sensor";
const char* password = "12345678";

// Web server on port 80
WebServer server(80);

// HTML page with auto-refresh
void handleRoot() {
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<meta http-equiv='refresh' content='1'>";
    html += "<title>RH Sensor</title>";
    html += "<style>";
    html += "body { font-family: Arial, sans-serif; text-align: center; background-color: #1a1a1a; color: white; margin: 0; padding: 0; }";
    html += ".container { display: flex; flex-direction: column; justify-content: center; align-items: center; min-height: 100vh; }";
    html += ".rh-value { font-size: 150px; font-weight: bold; margin: 20px; }";
    html += ".label { font-size: 40px; color: #888; }";
    html += ".timestamp { font-size: 20px; color: #666; margin-top: 30px; }";
    html += "</style>";
    html += "</head><body>";
    html += "<div class='container'>";
    html += "<div class='label'>Relative Humidity</div>";
    html += "<div class='rh-value'>" + String((int)sht3x.humidity) + "%</div>";
    html += "<div class='timestamp'>Updates every second</div>";
    html += "</div>";
    html += "</body></html>";
    
    server.send(200, "text/html", html);
}

void setup() {
    Serial.begin(115200);
    
    // Initialize display
    display.begin();
    display.setRotation(0);
    display.setBrightness(128);
    
    // Initialize canvas
    canvas.createSprite(display.width(), display.height());
    canvas.setTextColor(WHITE);
    canvas.setTextSize(2);
    canvas.fillScreen(BLACK);
    canvas.setCursor(5, 5);
    canvas.println("Starting...");
    canvas.pushSprite(0, 0);
    
    if (!qmp.begin(&Wire, QMP6988_SLAVE_ADDRESS_L, 2, 1, 400000U)) {
        while (1) {
            Serial.println("Couldn't find QMP6988");  
            delay(500);
        }
    }

    if (!sht3x.begin(&Wire, SHT3X_I2C_ADDR, 2, 1, 400000U)) {
        while (1) {
            Serial.println("Couldn't find SHT3X");
            delay(500);
        }
    }

    // Configure WiFi Access Point
    Serial.println("Setting up WiFi Access Point...");
    canvas.fillScreen(BLACK);
    canvas.setTextSize(1);
    canvas.setCursor(5, 5);
    canvas.println("WiFi AP Mode");
    canvas.pushSprite(0, 0);
    
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ssid, password);
    
    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP);
    
    // Display WiFi info on screen
    canvas.fillScreen(BLACK);
    canvas.setTextSize(1);
    canvas.setCursor(5, 5);
    canvas.println("WiFi AP Ready");
    canvas.setCursor(5, 20);
    canvas.print("SSID: ");
    canvas.println(ssid);
    canvas.setCursor(5, 35);
    canvas.print("IP: ");
    canvas.println(IP);
    canvas.setCursor(5, 50);
    canvas.println("Connect & browse");
    canvas.pushSprite(0, 0);
    
    // Setup web server
    server.on("/", handleRoot);
    server.begin();
    Serial.println("HTTP server started");
    
    delay(3000); // Show WiFi info for 3 seconds
}

void loop() {
    // Handle web server clients
    server.handleClient();
    
    if (sht3x.update()) {
        Serial.println("-----SHT3X-----");
        // Serial.print("Temperature: ");
        // Serial.print(sht3x.cTemp);
        // Serial.println(" degrees C");
        // Serial.print("Humidity: ");
        // Serial.print(sht3x.humidity);
        // Serial.println("% rH");
        // Serial.println("-------------\r\n");
    }

    if (qmp.update()) {
        // Serial.println("-----QMP6988-----");
        // Serial.print(F("Temperature: "));
        // Serial.print(qmp.cTemp);
        // Serial.println(" *C");
        // Serial.print(F("Pressure: "));
        // Serial.print(qmp.pressure);
        // Serial.println(" Pa");
        // Serial.print(F("Approx altitude: "));
        // Serial.print(qmp.altitude);
        // Serial.println(" m");
        // Serial.println("-------------\r\n");
    }
    
    // Display all data on screen using canvas
    canvas.fillScreen(BLACK);
    
    // Display only humidity - large centered number with smooth font
    int humidity = (int)sht3x.humidity;
    char humidityStr[4];
    sprintf(humidityStr, "%d", humidity);
    
    // Use built-in smooth font
    canvas.setTextSize(2);
    canvas.setFont(&fonts::Font7);
    canvas.setTextDatum(middle_center);
    
    // Change color based on humidity value
    if (humidity >= 50) {
        canvas.setTextColor(RED);
    } else {
        canvas.setTextColor(WHITE);
    }
    
    // Center the text
    canvas.drawString(humidityStr, display.width() / 2, display.height() / 2);
    
    // // Previous display code - all sensor data
    // canvas.setTextSize(2);
    // canvas.setCursor(5, 5);
    // canvas.printf("T : %.1fC", sht3x.cTemp);
    // canvas.setCursor(5, 35);
    // canvas.printf("RH: %.1f%%", sht3x.humidity);
    // canvas.setCursor(5, 65);
    // canvas.printf("T : %.1fC", qmp.cTemp);
    // canvas.setCursor(5, 95);
    // canvas.printf("P : %.0fPa", qmp.pressure / 100);
    
    canvas.pushSprite(0, 0);
    
    delay(1000);
}