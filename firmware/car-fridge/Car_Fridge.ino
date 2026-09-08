#include <WiFi.h>
#include <WebServer.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// Wi-Fi settings
const char* ssid = "SmartFridge";
const char* password = "Smart1234";

IPAddress local_IP(192, 168, 4, 1);
IPAddress gateway(192, 168, 4, 1);
IPAddress subnet(255, 255, 255, 0);

// Temperature sensor settings
#define ONE_WIRE_BUS1 5  // First DS18B20 sensor
//#define ONE_WIRE_BUS2 18 // Second DS18B20 sensor

OneWire oneWire1(ONE_WIRE_BUS1);
//OneWire oneWire2(ONE_WIRE_BUS2);
DallasTemperature sensor1(&oneWire1);
//DallasTemperature sensor2(&oneWire2);

// Cooling control pins
#define PELTIER_PIN 4
#define FAN_INNER1_PIN 16 
#define FAN_INNER2_PIN 17

float targetTemperature = 5;
float currentTemperature = 5; // Default temperature
WebServer server(80);

// Manual control variables
bool manualControl = false;
bool fanINManual = false;
bool fanOUTManual = false;
bool peltierManual = false;

// Timer for temperature measurement
unsigned long previousMillis = 0;
const long interval = 200; // Interval for temperature measurement in milliseconds

unsigned long fanOUTDelayStart = 0; // Čas, kdy byl Peltier vypnut
const long fanOUTDelayDuration = 20000; // Doba, po kterou má běžet venkovní ventilátor (20 sekund)
bool fanOUTDelayed = false; // Indikuje, zda je ventilátor v režimu zpožděného vypnutí

void setup() {
    Serial.begin(115200);

    // Initialize temperature sensors
    sensor1.begin();
    //sensor2.begin();

    // Set pin modes
    pinMode(PELTIER_PIN, OUTPUT);
    pinMode(FAN_INNER1_PIN, OUTPUT);
    pinMode(FAN_INNER2_PIN, OUTPUT);

    // Turn off devices at startup
    digitalWrite(PELTIER_PIN, LOW);
    digitalWrite(FAN_INNER1_PIN, LOW);
    digitalWrite(FAN_INNER2_PIN, LOW);

    // Set up Wi-Fi hotspot
    WiFi.softAPConfig(local_IP, gateway, subnet);
    WiFi.softAP(ssid, password);

    // Web server routes
    server.on("/", HTTP_GET, []() {
        server.send(200, "text/html", generateHTML());
    });

    server.on("/set_temperature", HTTP_GET, []() {
        if (server.hasArg("temperature")) {
            targetTemperature = server.arg("temperature").toFloat();
        }
        server.send(200, "text/html", generateHTML());
    });

    server.on("/get_status", HTTP_GET, []() {
        String response = "{";
        response += "\"fanIN\": " + String(digitalRead(FAN_INNER1_PIN)) + ",";
        response += "\"fanOUT\": " + String(digitalRead(FAN_INNER2_PIN)) + ",";
        response += "\"peltier\": " + String(digitalRead(PELTIER_PIN)) + ",";
        response += "\"mode\": \"" + String(manualControl ? "Manual" : "Auto") + "\",";
        response += "\"fridgeStatus\": \"" + String(digitalRead(PELTIER_PIN) ? "Cooling" : "Standby") + "\",";
        response += "\"currentTemp\": " + String(currentTemperature, 2);
        response += "}";
        server.send(200, "application/json", response);
    });

    server.on("/toggle", HTTP_GET, []() {
        String device = server.arg("device");
        if (device == "fanIN") {
            fanINManual = !fanINManual;
            digitalWrite(FAN_INNER1_PIN, fanINManual ? HIGH : LOW);
        } else if (device == "fanOUT") {
            fanOUTManual = !fanOUTManual;
            digitalWrite(FAN_INNER2_PIN, fanOUTManual ? HIGH : LOW);
        } else if (device == "peltier") {
            peltierManual = !peltierManual;
            digitalWrite(PELTIER_PIN, peltierManual ? HIGH : LOW);
        } else if (device == "mode") {
            manualControl = !manualControl; // Toggle between Manual and Auto mode
        }
        server.send(200, "text/plain", "OK");
    });

    server.begin(); // Start the web server
}

void loop() {
    server.handleClient(); // Handle client requests

    unsigned long currentMillis = millis(); // Get the current time

    // Measure temperature every 200 ms
    if (currentMillis - previousMillis >= interval) {
        previousMillis = currentMillis; // Save the last measurement time

        // Read temperature from sensors
        sensor1.requestTemperatures();
        //sensor2.requestTemperatures();
        float temp1 = sensor1.getTempCByIndex(0);
        //float temp2 = sensor2.getTempCByIndex(0);
        currentTemperature = ((temp1)); //+ temp2) / 2);

        // Automatic temperature control (only if not in manual mode)
        if (!manualControl) {
            if (currentTemperature > targetTemperature) {
                digitalWrite(PELTIER_PIN, HIGH);
                digitalWrite(FAN_INNER1_PIN, HIGH); // Fan IN (pin 16)
                digitalWrite(FAN_INNER2_PIN, HIGH); // Fan OUT (pin 17)
                fanOUTDelayed = false; // Reset zpožděného vypnutí
            } else {
                digitalWrite(PELTIER_PIN, LOW);
                digitalWrite(FAN_INNER1_PIN, LOW); // Fan IN (pin 16) se vypne okamžitě

                // Pokud Peltier byl právě vypnut, spusť časovač pro Fan OUT
                if (!fanOUTDelayed) {
                    fanOUTDelayStart = currentMillis; // Ulož čas vypnutí Peltieru
                    fanOUTDelayed = true; // Aktivuj zpožděné vypnutí
                }

                // Pokud je ventilátor v režimu zpožděného vypnutí, vypni ho po uplynutí času
                if (fanOUTDelayed && (currentMillis - fanOUTDelayStart >= fanOUTDelayDuration)) {
                    digitalWrite(FAN_INNER2_PIN, LOW); // Fan OUT (pin 17)
                    fanOUTDelayed = false; // Reset zpožděného vypnutí
                }
            }
        } else {
            // Respect manual control state
            digitalWrite(FAN_INNER1_PIN, fanINManual ? HIGH : LOW);  // Fan IN (pin 16)
            digitalWrite(FAN_INNER2_PIN, fanOUTManual ? HIGH : LOW); // Fan OUT (pin 17)
            digitalWrite(PELTIER_PIN, peltierManual ? HIGH : LOW);   // Peltier
            fanOUTDelayed = false; // Reset zpožděného vypnutí v manuálním režimu
        }
    }
}

String generateHTML() {
    String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<style>";
    html += "body { font-family: 'Arial', sans-serif; background-color: #f4f4f4; color: #333; margin: 0; padding: 0; display: flex; justify-content: center; align-items: center; height: 100vh; }";
    html += ".container { background: white; padding: 20px; border-radius: 10px; box-shadow: 0 4px 8px rgba(0, 0, 0, 0.1); width: 90%; max-width: 400px; text-align: center; }";
    html += "h1 { font-size: 24px; margin-bottom: 20px; color: #007bff; }";
    html += ".status { font-size: 18px; margin-bottom: 20px; }";
    html += ".temperature { font-size: 20px; margin-bottom: 20px; }";
    html += ".control { margin-bottom: 20px; }";
    html += ".button { font-size: 16px; padding: 10px 20px; margin: 5px; border: none; border-radius: 5px; cursor: pointer; transition: background-color 0.3s ease; }";
    html += ".button.on { background-color: #28a745; color: white; }";
    html += ".button.off { background-color: #dc3545; color: white; }";
    html += ".button.disabled { background-color: #6c757d; color: white; cursor: not-allowed; }";
    html += ".mode-button { background-color: #007bff; color: white; }";
    html += ".mode-button.manual { background-color: #dc3545; }";
    html += ".mode-button.auto { background-color: #28a745; }";
    html += "</style></head><body>";
    html += "<div class='container'>";
    html += "<h1>Smart Fridge</h1>";
    html += "<div class='status'>Fridge Status: <span id='fridgeStatus'>" + String(digitalRead(PELTIER_PIN) ? "Cooling" : "Standby") + "</span></div>";
    html += "<div class='temperature'>Current Temperature: <span id='currentTemp'>" + String(currentTemperature, 2) + "</span> &deg;C</div>";
    html += "<div class='temperature'>Target Temperature: " + String(targetTemperature, 2) + " &deg;C</div>";
    html += "<form action='/set_temperature' method='GET' class='control'>";
    html += "<input type='number' step='0.1' name='temperature' value='" + String(targetTemperature, 1) + "' style='font-size: 16px; width: 100%; padding: 10px; box-sizing: border-box; border-radius: 5px; border: 1px solid #ccc;'>";
    html += "<button type='submit' class='button mode-button' style='margin-top: 10px;'>Set Temperature</button></form>";
    html += "<div class='control'>";
    html += "<button id='fanINButton' class='button " + String(manualControl ? (digitalRead(FAN_INNER1_PIN) ? "on" : "off") : "disabled") + "' " + String(manualControl ? "" : "disabled") + " onclick='toggleDevice(\"fanIN\")'>Fan IN</button>";
    html += "<button id='fanOUTButton' class='button " + String(manualControl ? (digitalRead(FAN_INNER2_PIN) ? "on" : "off") : "disabled") + "' " + String(manualControl ? "" : "disabled") + " onclick='toggleDevice(\"fanOUT\")'>Fan OUT</button>";
    html += "<button id='peltierButton' class='button " + String(manualControl ? (digitalRead(PELTIER_PIN) ? "on" : "off") : "disabled") + "' " + String(manualControl ? "" : "disabled") + " onclick='toggleDevice(\"peltier\")'>Peltier</button>";
    html += "<button id='modeButton' class='button mode-button " + String(manualControl ? "manual" : "auto") + "' onclick='toggleDevice(\"mode\")'>Mode: <span id='modeText'>" + String(manualControl ? "Manual" : "Auto") + "</span></button>";
    html += "</div></div>";

    // JavaScript pro AJAX, ovládání a aktualizaci stavu
    html += "<script>";
    html += "function updateStatus() {";
    html += "  var xhr = new XMLHttpRequest();";
    html += "  xhr.onreadystatechange = function() {";
    html += "    if (this.readyState == 4 && this.status == 200) {";
    html += "      var response = JSON.parse(this.responseText);";
    html += "      document.getElementById('fanINButton').className = 'button ' + (response.mode === 'Manual' ? (response.fanIN ? 'on' : 'off') : 'disabled');";
    html += "      document.getElementById('fanINButton').disabled = response.mode !== 'Manual';";
    html += "      document.getElementById('fanOUTButton').className = 'button ' + (response.mode === 'Manual' ? (response.fanOUT ? 'on' : 'off') : 'disabled');";
    html += "      document.getElementById('fanOUTButton').disabled = response.mode !== 'Manual';";
    html += "      document.getElementById('peltierButton').className = 'button ' + (response.mode === 'Manual' ? (response.peltier ? 'on' : 'off') : 'disabled');";
    html += "      document.getElementById('peltierButton').disabled = response.mode !== 'Manual';";
    html += "      document.getElementById('modeText').innerText = response.mode;";
    html += "      document.getElementById('modeButton').className = 'button mode-button ' + (response.mode === 'Manual' ? 'manual' : 'auto');";
    html += "      document.getElementById('fridgeStatus').innerText = response.fridgeStatus;";
    html += "      document.getElementById('currentTemp').innerText = response.currentTemp;";
    html += "    }";
    html += "  };";
    html += "  xhr.open('GET', '/get_status', true);";
    html += "  xhr.send();";
    html += "}";
    html += "function toggleDevice(device) {";
    html += "  var xhr = new XMLHttpRequest();";
    html += "  xhr.onreadystatechange = function() {";
    html += "    if (this.readyState == 4 && this.status == 200) {";
    html += "      updateStatus();"; // Aktualizovat stav po změně
    html += "    }";
    html += "  };";
    html += "  xhr.open('GET', '/toggle?device=' + device, true);";
    html += "  xhr.send();";
    html += "}";
    html += "setInterval(updateStatus, 500);"; // Aktualizace stavu každých 500 ms
    html += "</script>";

    html += "</body></html>";
    return html;
}