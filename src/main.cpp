#include <Arduino.h>

/**
 * @file ENV_III.ino
 * @date 2025-12-04
 *
 *
 * @Hardwares: M5AtomS3 + Unit ENV_III
 * @Dependent Library:
 * M5UnitENV: https://github.com/m5stack/M5Unit-ENV
 */

//#include "M5AtomS3.h"
#include <M5GFX.h>
#include "M5UnitENV.h"
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

SHT3X sht3x;
QMP6988 qmp;

M5GFX display;
M5Canvas canvas(&display);

Preferences preferences;

// WiFi AP credentials
const char* ssid = "AtomS3-RH-Sensor";
const char* password = "12345678";

// RH threshold for alert (change this value to adjust threshold)
const int RH_THRESHOLD = 50;

// Data logging settings
const int MAX_DATA_POINTS = 1440; // 24 hours * 60 minutes
unsigned long lastLogTime = 0;
const unsigned long LOG_INTERVAL = 60000; // 1 minute in milliseconds
int dataIndex = 0;

// Data structure for logged readings
struct DataPoint {
    float humidity;
    float temperature;
    float pressure;
    unsigned long timestamp; // Minutes since boot
};

// Web server on port 80
WebServer server(80);

// HTML page with auto-refresh
void handleRoot() {
    int humidity = (int)sht3x.humidity;
    float temperature = sht3x.cTemp;
    float pressure = qmp.pressure / 100.0; // Convert Pa to mbar
    bool isAlert = (humidity >= RH_THRESHOLD);
    String bgColor = isAlert ? "#cc0000" : "#1a1a1a";
    
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<meta http-equiv='refresh' content='10'>";
    html += "<title>RH Sensor</title>";
    html += "<style>";
    html += "body { font-family: Arial, sans-serif; text-align: center; background-color: " + bgColor + "; color: white; margin: 0; padding: 0; }";
    html += ".container { display: flex; flex-direction: column; justify-content: center; align-items: center; min-height: 100vh; }";
    html += ".rh-value { font-size: 150px; font-weight: bold; margin: 20px; }";
    html += ".label { font-size: 40px; color: #888; }";
    html += ".other-values { font-size: 30px; color: #ccc; margin: 10px; }";
    html += ".timestamp { font-size: 20px; color: #666; margin-top: 30px; }";
    html += ".button { background-color: #4CAF50; border: none; color: white; padding: 15px 32px; text-align: center; text-decoration: none; display: inline-block; font-size: 16px; margin: 20px; cursor: pointer; border-radius: 4px; }";
    html += "</style>";
    html += "</head><body>";
    html += "<div class='container'>";
    html += "<div class='label'>Relative Humidity</div>";
    html += "<div class='rh-value'>" + String(humidity) + "%</div>";
    html += "<div class='other-values'>Temperature: " + String(temperature, 1) + " °C</div>";
    html += "<div class='other-values'>Pressure: " + String(pressure, 1) + " mbar</div>";
    html += "<div class='timestamp'>Updates every 10 seconds</div>";
    html += "<a href='/history' class='button'>View History</a>";
    html += "</div>";
    html += "</body></html>";
    
    server.send(200, "text/html", html);
}

// Handle history data request
void handleHistory() {
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<title>Sensor History</title>";
    html += "<style>";
    html += "body { font-family: Arial, sans-serif; background-color: #1a1a1a; color: white; margin: 20px; }";
    html += ".button { background-color: #4CAF50; border: none; color: white; padding: 10px 20px; text-decoration: none; display: inline-block; font-size: 14px; margin: 10px 5px; cursor: pointer; border-radius: 4px; }";
    html += "#loading { color: #888; margin: 20px 0; }";
    html += ".chart-container { margin: 30px 0; }";
    html += "canvas { width: 100%; height: 250px; background-color: #2a2a2a; border-radius: 8px; display: block; }";
    html += "h2 { color: #888; font-size: 18px; margin: 10px 0; }";
    html += "</style>";
    html += "</head><body>";
    html += "<h1>Sensor History</h1>";
    html += "<a href='/' class='button'>Back to Current</a>";
    html += "<a href='/csv' class='button'>Download CSV</a>";
    html += "<div id='loading'>Loading data...</div>";
    html += "<div class='chart-container'><h2>Humidity (%)</h2><canvas id='chart1'></canvas></div>";
    html += "<div class='chart-container'><h2>Temperature (°C)</h2><canvas id='chart2'></canvas></div>";
    html += "<div class='chart-container'><h2>Pressure (mbar)</h2><canvas id='chart3'></canvas></div>";
    html += "<script>";
    html += "function drawChart(canvasId,data,color,label){";
    html += "const canvas=document.getElementById(canvasId);";
    html += "const ctx=canvas.getContext('2d');";
    html += "const w=canvas.width=canvas.offsetWidth;";
    html += "const h=canvas.height=250;";
    html += "const padding=40;";
    html += "const chartW=w-2*padding;";
    html += "const chartH=h-2*padding;";
    html += "if(data.length===0)return;";
    html += "const min=Math.min(...data);";
    html += "const max=Math.max(...data);";
    html += "const range=max-min||1;";
    html += "ctx.strokeStyle='#444';ctx.lineWidth=1;";
    html += "for(let i=0;i<5;i++){ctx.beginPath();const y=padding+i*chartH/4;ctx.moveTo(padding,y);ctx.lineTo(w-padding,y);ctx.stroke();}";
    html += "ctx.fillStyle='#888';ctx.font='12px Arial';";
    html += "for(let i=0;i<=4;i++){const val=(max-i*range/4).toFixed(1);ctx.fillText(val,5,padding+i*chartH/4+4);}";
    html += "ctx.strokeStyle=color;ctx.lineWidth=2;ctx.beginPath();";
    html += "data.forEach((v,i)=>{";
    html += "const x=padding+i*chartW/(data.length-1||1);";
    html += "const y=padding+chartH-(v-min)*chartH/range;";
    html += "i===0?ctx.moveTo(x,y):ctx.lineTo(x,y);";
    html += "});";
    html += "ctx.stroke();";
    html += "}";
    html += "fetch('/data').then(r=>r.json()).then(data=>{";
    html += "document.getElementById('loading').style.display='none';";
    html += "if(data.length===0){document.body.innerHTML+='<p>No data logged yet</p>';return;}";
    html += "drawChart('chart1',data.map(d=>d.humidity),'rgb(75,192,192)','Humidity');";
    html += "drawChart('chart2',data.map(d=>d.temperature),'rgb(255,99,132)','Temperature');";
    html += "drawChart('chart3',data.map(d=>d.pressure),'rgb(255,205,86)','Pressure');";
    html += "});";
    html += "</script>";
    html += "</body></html>";
    
    server.send(200, "text/html", html);
}

// Handle CSV download - returns raw CSV data
void handleCSV() {
    String csv = "Minutes Ago,Humidity (%),Temperature (°C),Pressure (mbar)\n";
    
    // Read data from NVS
    preferences.begin("sensor_log", true);
    int count = preferences.getInt("count", 0);
    int startIdx = preferences.getInt("startIdx", 0);
    
    if (count > 0) {
        for (int i = 0; i < count; i++) {
            int idx = (startIdx + i) % MAX_DATA_POINTS;
            String key = "d" + String(idx);
            
            size_t len = preferences.getBytesLength(key.c_str());
            if (len == sizeof(DataPoint)) {
                DataPoint dp;
                preferences.getBytes(key.c_str(), &dp, sizeof(DataPoint));
                
                unsigned long minutesAgo = (count - 1 - i);
                
                csv += String(minutesAgo) + ",";
                csv += String(dp.humidity, 1) + ",";
                csv += String(dp.temperature, 1) + ",";
                csv += String(dp.pressure, 1) + "\n";
            }
        }
    }
    
    preferences.end();
    
    server.sendHeader("Content-Disposition", "attachment; filename=sensor_data.csv");
    server.send(200, "text/csv", csv);
}

// Handle JSON data for dynamic loading
void handleData() {
    String json = "[";
    
    preferences.begin("sensor_log", true);
    int count = preferences.getInt("count", 0);
    int startIdx = preferences.getInt("startIdx", 0);
    
    if (count > 0) {
        for (int i = 0; i < count; i++) {
            int idx = (startIdx + i) % MAX_DATA_POINTS;
            String key = "d" + String(idx);
            
            size_t len = preferences.getBytesLength(key.c_str());
            if (len == sizeof(DataPoint)) {
                DataPoint dp;
                preferences.getBytes(key.c_str(), &dp, sizeof(DataPoint));
                
                unsigned long minutesAgo = (count - 1 - i);
                
                if (i > 0) json += ",";
                json += "{\"offset\":" + String(minutesAgo) + ",";
                json += "\"humidity\":" + String(dp.humidity, 1) + ",";
                json += "\"temperature\":" + String(dp.temperature, 1) + ",";
                json += "\"pressure\":" + String(dp.pressure, 1) + "}";
            }
        }
    }
    
    preferences.end();
    json += "]";
    
    server.send(200, "application/json", json);
}

// Save data point to NVS
void saveDataPoint(float humidity, float temperature, float pressure) {
    preferences.begin("sensor_log", false); // Read-write
    
    int count = preferences.getInt("count", 0);
    int startIdx = preferences.getInt("startIdx", 0);
    
    DataPoint dp;
    dp.humidity = humidity;
    dp.temperature = temperature;
    dp.pressure = pressure;
    dp.timestamp = millis() / 60000; // Minutes since boot
    
    // Calculate current index
    int currentIdx = (startIdx + count) % MAX_DATA_POINTS;
    
    // Save data
    String key = "d" + String(currentIdx);
    preferences.putBytes(key.c_str(), &dp, sizeof(DataPoint));
    
    // Update count and startIdx
    if (count < MAX_DATA_POINTS) {
        count++;
        preferences.putInt("count", count);
    } else {
        // Ring buffer is full, move start index
        startIdx = (startIdx + 1) % MAX_DATA_POINTS;
        preferences.putInt("startIdx", startIdx);
    }
    
    preferences.end();
    
    Serial.println("Data point saved: RH=" + String(humidity) + "% T=" + String(temperature) + "°C P=" + String(pressure) + "mbar");
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
    server.on("/history", handleHistory);
    server.on("/data", handleData);
    server.on("/csv", handleCSV);
    server.begin();
    Serial.println("HTTP server started");
    
    // Initialize NVS
    preferences.begin("sensor_log", false);
    // Optionally clear old data on first boot (comment out after first use)
    // preferences.clear();
    preferences.end();
    
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
    
    // Log data every minute
    if (millis() - lastLogTime >= LOG_INTERVAL) {
        lastLogTime = millis();
        saveDataPoint(sht3x.humidity, sht3x.cTemp, qmp.pressure / 100.0);
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
    
    // Change color based on humidity value using global threshold
    if (humidity >= RH_THRESHOLD) {
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