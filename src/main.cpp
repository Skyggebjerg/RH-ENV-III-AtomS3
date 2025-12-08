#include <Arduino.h>

/**
 * @file ENV_III_optimized.ino
 * @date 2025-12-04
 *
 * Hardwares: M5AtomS3 + Unit ENV_III
 * Dependent Library:
 * M5UnitENV: https://github.com/m5stack/M5Unit-ENV
 */

//#include "M5AtomS3.h"
#include <M5GFX.h>
#include "M5UnitENV.h"
#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <FS.h>

SHT3X sht3x;
QMP6988 qmp;

M5GFX display;
M5Canvas canvas(&display);

const char* DATA_FILE   = "/sensor_data.bin";
const char* MINMAX_FILE = "/minmax.bin";

// *** WiFi AP ***
const char* ssid     = "AtomS3-RH-Sensor";
const char* password = "12345678";

// RH threshold for alert
const int RH_THRESHOLD = 50;

// *** Sampling interval i minutter (NEM AT ÆNDRE) ***
const uint16_t SAMPLE_INTERVAL_MIN = 5;
const unsigned long LOG_INTERVAL   = SAMPLE_INTERVAL_MIN * 60000UL;  // ms

// Data logging settings
const int MAX_DATA_POINTS  = 1440;            // stadig max 1440 punkter
unsigned long lastLogTime = 0;

// Hvor mange punkter vi vil sende til graferne
const int MAX_POINTS_TO_SEND = 300;

// Data structure for logged readings
struct DataPoint {
    float humidity;
    float temperature;
    float pressure;
    unsigned long timestamp; // Minutes since boot (ikke brugt i grafen lige nu)
};

// Min/Max tracking
struct MinMax {
    float minHumidity   = 999.0;
    float maxHumidity   = -999.0;
    float minTemperature= 999.0;
    float maxTemperature= -999.0;
    float minPressure   = 9999.0;
    float maxPressure   = 0.0;
};

MinMax minMaxValues;

// Web server on port 80
WebServer server(80);

// -------------------------------------------------------------------
// HTML: Forside /
// -------------------------------------------------------------------
void handleRoot() {
    int humidity      = (int)sht3x.humidity;
    float temperature = sht3x.cTemp;
    float pressure    = qmp.pressure / 100.0; // Pa -> mbar
    bool isAlert      = (humidity >= RH_THRESHOLD);
    String bgColor    = isAlert ? "#cc0000" : "#1a1a1a";
    
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
    html += "<div style='margin-top: 30px; font-size: 20px; color: #888;'>Recorded Min/Max</div>";
    html += "<div class='other-values' style='font-size: 18px;'>RH: " + String(minMaxValues.minHumidity, 1) + "% - " + String(minMaxValues.maxHumidity, 1) + "%</div>";
    html += "<div class='other-values' style='font-size: 18px;'>Temp: " + String(minMaxValues.minTemperature, 1) + "°C - " + String(minMaxValues.maxTemperature, 1) + "°C</div>";
    html += "<div class='other-values' style='font-size: 18px;'>Press: " + String(minMaxValues.minPressure, 1) + " - " + String(minMaxValues.maxPressure, 1) + " mbar</div>";
    html += "<div class='timestamp'>Updates every 10 seconds</div>";
    html += "<a href='/history' class='button'>View History</a>";
    html += "</div>";
    html += "</body></html>";
    
    server.send(200, "text/html", html);
}

// -------------------------------------------------------------------
// HTML: /history (grafer + start/slut-tider)
// -------------------------------------------------------------------
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
    html += "<button onclick='clearData()' class='button' style='background-color:#f44336;'>Clear All Data</button>";
    html += "<div id='timeinfo' style='margin-top:10px;color:#ccc;font-size:14px;'></div>";
    html += "<div id='loading'>Loading data...</div>";
    html += "<div class='chart-container'><h2>Humidity (%)</h2><canvas id='chart1'></canvas></div>";
    html += "<div class='chart-container'><h2>Temperature (°C)</h2><canvas id='chart2'></canvas></div>";
    html += "<div class='chart-container'><h2>Pressure (mbar)</h2><canvas id='chart3'></canvas></div>";
    html += "<script>";
    // JS-konstant med samme interval som i C++
    html += "const SAMPLE_INTERVAL_MIN=" + String(SAMPLE_INTERVAL_MIN) + ";";
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
    html += "ctx.clearRect(0,0,w,h);";
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
    html += "const timeInfo=document.getElementById('timeinfo');";
    html += "if(data.length===0){timeInfo.textContent='No data logged yet';return;}";
    // Beregn start/slut-tid ud fra telefonens ur
    html += "const n=data.length;";
    html += "const now=new Date();";
    html += "const endTime=now;";
    html += "const spanMin=n*SAMPLE_INTERVAL_MIN;";  // efter din 10*5-logik
    html += "const startTime=new Date(now.getTime()-spanMin*60000);";
    html += "const fmt=t=>t.toLocaleTimeString([],{hour:'2-digit',minute:'2-digit'});";
    html += "timeInfo.textContent='Start: '+fmt(startTime)+'  |  Slut: '+fmt(endTime);";
    // Tegn grafer
    html += "drawChart('chart1',data.map(d=>d.humidity),'rgb(75,192,192)','Humidity');";
    html += "drawChart('chart2',data.map(d=>d.temperature),'rgb(255,99,132)','Temperature');";
    html += "drawChart('chart3',data.map(d=>d.pressure),'rgb(255,205,86)','Pressure');";
    html += "});";
    html += "function clearData(){";
    html += "if(confirm('Are you sure you want to delete all logged data?')){";
    html += "fetch('/clear',{method:'POST'}).then(()=>location.reload());";
    html += "}";
    html += "}";
    html += "</script>";
    html += "</body></html>";
    
    server.send(200, "text/html", html);
}

// -------------------------------------------------------------------
// CSV download
// -------------------------------------------------------------------
void handleCSV() {
    server.sendHeader("Content-Disposition", "attachment; filename=sensor_data.csv");
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/csv", "");

    server.sendContent("Minutes Ago,Humidity (%),Temperature (°C),Pressure (mbar)\n");
    
    File file = SPIFFS.open(DATA_FILE, "r");
    if (file) {
        int totalPoints = file.size() / sizeof(DataPoint);
        int index = 0;
        DataPoint dp;

        while (file.available() >= (int)sizeof(DataPoint)) {
            file.read((uint8_t*)&dp, sizeof(DataPoint));

            // nu i minutter, med step = SAMPLE_INTERVAL_MIN
            unsigned long minutesAgo = (unsigned long)(totalPoints - 1 - index) * SAMPLE_INTERVAL_MIN;

            String line;
            line.reserve(64);
            line  = String(minutesAgo);
            line += ",";
            line += String(dp.humidity, 1);
            line += ",";
            line += String(dp.temperature, 1);
            line += ",";
            line += String(dp.pressure, 1);
            line += "\n";

            server.sendContent(line);
            index++;
        }
        file.close();
    }
    
    server.sendContent("");
}

// -------------------------------------------------------------------
// JSON data til grafer
// -------------------------------------------------------------------
void handleData() {
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "application/json", "");
    server.sendContent("[");

    File file = SPIFFS.open(DATA_FILE, "r");
    if (file) {
        int totalPoints = file.size() / sizeof(DataPoint);

        int startIndex = 0;
        if (totalPoints > MAX_POINTS_TO_SEND) {
            startIndex = totalPoints - MAX_POINTS_TO_SEND;
        }

        file.seek(startIndex * sizeof(DataPoint));

        bool first = true;
        int globalIndex = startIndex;
        DataPoint dp;

        while (file.available() >= (int)sizeof(DataPoint)) {
            file.read((uint8_t*)&dp, sizeof(DataPoint));

            if (!first) server.sendContent(",");
            first = false;

            unsigned long minutesAgo = (unsigned long)(totalPoints - 1 - globalIndex) * SAMPLE_INTERVAL_MIN;

            String json;
            json.reserve(96);
            json  = "{\"offset\":";
            json += String(minutesAgo);
            json += ",\"humidity\":";
            json += String(dp.humidity, 1);
            json += ",\"temperature\":";
            json += String(dp.temperature, 1);
            json += ",\"pressure\":";
            json += String(dp.pressure, 1);
            json += "}";

            server.sendContent(json);
            globalIndex++;
        }

        file.close();
    }

    server.sendContent("]");
    server.sendContent("");
}

// -------------------------------------------------------------------
// Clear data
// -------------------------------------------------------------------
void handleClear() {
    bool dataCleared   = SPIFFS.remove(DATA_FILE);
    bool minMaxCleared = SPIFFS.remove(MINMAX_FILE);
    
    minMaxValues.minHumidity    = 999.0;
    minMaxValues.maxHumidity    = -999.0;
    minMaxValues.minTemperature = 999.0;
    minMaxValues.maxTemperature = -999.0;
    minMaxValues.minPressure    = 9999.0;
    minMaxValues.maxPressure    = 0.0;
    
    if (dataCleared || minMaxCleared) {
        Serial.println("All data and min/max cleared");
        server.send(200, "text/plain", "Data cleared");
    } else {
        Serial.println("Failed to clear data");
        server.send(500, "text/plain", "Failed to clear data");
    }
}

// -------------------------------------------------------------------
// Load / save min/max
// -------------------------------------------------------------------
void loadMinMax() {
    File file = SPIFFS.open(MINMAX_FILE, "r");
    if (file && file.size() == sizeof(MinMax)) {
        file.read((uint8_t*)&minMaxValues, sizeof(MinMax));
        file.close();
        Serial.println("Min/Max values loaded");
    } else {
        Serial.println("No min/max data found, using defaults");
    }
}

void saveMinMax() {
    File file = SPIFFS.open(MINMAX_FILE, "w");
    if (file) {
        file.write((uint8_t*)&minMaxValues, sizeof(MinMax));
        file.close();
    }
}

// -------------------------------------------------------------------
// Update min/max
// -------------------------------------------------------------------
void updateMinMax(float humidity, float temperature, float pressure) {
    bool updated = false;
    
    if (humidity < minMaxValues.minHumidity) {
        minMaxValues.minHumidity = humidity;
        updated = true;
    }
    if (humidity > minMaxValues.maxHumidity) {
        minMaxValues.maxHumidity = humidity;
        updated = true;
    }
    if (temperature < minMaxValues.minTemperature) {
        minMaxValues.minTemperature = temperature;
        updated = true;
    }
    if (temperature > minMaxValues.maxTemperature) {
        minMaxValues.maxTemperature = temperature;
        updated = true;
    }
    if (pressure < minMaxValues.minPressure) {
        minMaxValues.minPressure = pressure;
        updated = true;
    }
    if (pressure > minMaxValues.maxPressure) {
        minMaxValues.maxPressure = pressure;
        updated = true;
    }
    
    if (updated) {
        saveMinMax();
    }
}

// -------------------------------------------------------------------
// Save data point til SPIFFS
// -------------------------------------------------------------------
void saveDataPoint(float humidity, float temperature, float pressure) {
    DataPoint dp;
    dp.humidity    = humidity;
    dp.temperature = temperature;
    dp.pressure    = pressure;
    dp.timestamp   = millis() / 60000UL; // Minutes since boot
    
    int count = 0;
    File file = SPIFFS.open(DATA_FILE, "r");
    if (file) {
        count = file.size() / sizeof(DataPoint);
        file.close();
    }
    
    if (count >= MAX_DATA_POINTS) {
        File readFile = SPIFFS.open(DATA_FILE, "r");
        File tempFile = SPIFFS.open("/temp.bin", "w");
        
        if (readFile && tempFile) {
            readFile.seek(sizeof(DataPoint));
            
            uint8_t buffer[sizeof(DataPoint)];
            while (readFile.available() >= (int)sizeof(DataPoint)) {
                readFile.read(buffer, sizeof(DataPoint));
                tempFile.write(buffer, sizeof(DataPoint));
            }
            
            readFile.close();
            tempFile.close();
            
            SPIFFS.remove(DATA_FILE);
            SPIFFS.rename("/temp.bin", DATA_FILE);
        }
    }
    
    file = SPIFFS.open(DATA_FILE, "a");
    if (file) {
        file.write((uint8_t*)&dp, sizeof(DataPoint));
        file.close();
        Serial.println("Data point saved: RH=" + String(humidity) + "% T=" + String(temperature) + "°C P=" + String(pressure) + "mbar");
        updateMinMax(humidity, temperature, pressure);
    } else {
        Serial.println("Failed to save data point");
    }
}

// -------------------------------------------------------------------
// setup()
// -------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    
    display.begin();
    display.setRotation(0);
    display.setBrightness(128);
    
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
    
    server.on("/",       handleRoot);
    server.on("/history",handleHistory);
    server.on("/data",   handleData);
    server.on("/csv",    handleCSV);
    server.on("/clear",  HTTP_POST, handleClear);
    server.begin();
    Serial.println("HTTP server started");
    
    if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS Mount Failed");
    } else {
        Serial.println("SPIFFS Mounted Successfully");
        Serial.print("Total space: ");
        Serial.print(SPIFFS.totalBytes());
        Serial.println(" bytes");
        Serial.print("Used space: ");
        Serial.print(SPIFFS.usedBytes());
        Serial.println(" bytes");
        
        loadMinMax();
    }
    
    delay(3000);
}

// -------------------------------------------------------------------
// loop()
// -------------------------------------------------------------------
void loop() {
    server.handleClient();
    
    if (sht3x.update()) {
        // OK
    }

    if (qmp.update()) {
        // OK
    }
    
    if (millis() - lastLogTime >= LOG_INTERVAL) {
        lastLogTime = millis();
        saveDataPoint(sht3x.humidity, sht3x.cTemp, qmp.pressure / 100.0);
    }
    
    canvas.fillScreen(BLACK);
    
    int humidity = (int)sht3x.humidity;
    char humidityStr[8];
    snprintf(humidityStr, sizeof(humidityStr), "%d", humidity);
    
    canvas.setTextSize(2);
    canvas.setFont(&fonts::Font7);
    canvas.setTextDatum(middle_center);
    
    if (humidity >= RH_THRESHOLD) {
        canvas.setTextColor(RED);
    } else {
        canvas.setTextColor(WHITE);
    }
    
    canvas.drawString(humidityStr, display.width() / 2, display.height() / 2);
    canvas.pushSprite(0, 0);
    
    delay(1000);
}
