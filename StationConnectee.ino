// Barometer
#include <SFE_BMP180.h>
#include <Wire.h>
// Display
#include "U8glib.h"
// Wifi Connector
#include <SoftwareSerial.h>
// Date / Time
#include <Time.h>

#define SSID "YOUR_WIFI_SSID"
#define PASS "YOUR_WIFI_PASSWORD"
#define THINGSPEAK_IP "184.106.153.149"    // thingspeak.com
#define MAX_FAILS 3
#define DEBUG true

SoftwareSerial espSerial(2, 3);          // RX, TX

// Thermo-Hygro
#include <DHT.h>
DHT dht(10, DHT11);

// Barometer
SFE_BMP180 pressure;
double temp, pression, hum, p0, a;
// Light
const int light=A1;
int lightValue=0;

unsigned long sample_interval = 30000;
unsigned long last_time;
int fails = 0;


// OLED Display
U8GLIB_SSD1306_128X64 u8g(U8G_I2C_OPT_NONE);

void setup() {
  pinMode(2, INPUT);    // softserial RX
  pinMode(3, OUTPUT);   // softserial TX
  pinMode(4, OUTPUT);    // esp reset
  // Thermo-Hygro
  pinMode(10, INPUT);     // DHT - ...

  setTime(0);

  delay(3000);           // to enable uploading??

  Serial.begin(115200);
  espSerial.begin(115200);
  espSerial.setTimeout(2000);

  // Thermo-Hygro
  dht.begin();

  // Barometer
  if (!pressure.begin()) {
    Serial.println("Could not find a valid BMP085 sensor, check wiring!");
  }

  last_time = millis();  // get the current time;

  if (!resetESP()) return;
  if (!connectWiFi()) return;

}

boolean resetESP() {
  // - test if module ready
  Serial.print(F("reset espSerial..."));

  // physically reset EPS module
  digitalWrite(4, LOW);
  delay(100);
  digitalWrite(4, HIGH);
  delay(500);

  if (!send("AT+RST", "ready", F("%% module no response"))) return false;

  Serial.print(F("module ready..."));
  return true;
}

boolean connectWiFi() {
  int tries = 5;
  while (tries-- > 0 && !tryConnectWiFi());
  if (tries <= 0) {
    Serial.println(F("%% tried X times to connect, please reset"));
    return false;
  }
  delay(500); // TOOD: send and wait for correct response?
  // set the single connection mode
  espSerial.println("AT+CIPMUX=0");
  delay(500); // TOOD: send and wait for correct response?
  //espSerial.println("AT+CIPSERVER=1,80");
  //delay(500); // TOOD: send and wait for correct response?
  // TODO: listen?
  return true;
}

boolean tryConnectWiFi() {
  espSerial.println("AT+CWMODE=1");
  delay(2000); // TOOD: send and wait for correct response?

  String cmd = "AT+CWJAP=\"";
  cmd += SSID;
  cmd += "\",\"";
  cmd += PASS;
  cmd += "\"";

  if (!send(cmd, "OK", F("%% cannot connect to wifi..."))) return false;

  Serial.println(F("WiFi OK..."));
  return true;
}

boolean send(String cmd, char* waitFor, String errMsg) {
  espSerial.println(cmd);
  if (!espSerial.find(waitFor)) {
    Serial.print(errMsg);
    return false;
  }
  return true;
}

boolean connect(char* ip) {
  String cmd;
  cmd = "AT+CIPSTART=\"TCP\",\"";
  cmd += ip;
  cmd += "\",80";
  espSerial.println(cmd);
  // TODO: this is strange, we wait for ERROR
  // so normal is to timeout (2s) then continue
  if (espSerial.find("Error")) return false;
  return true;
}

boolean sendGET(String path) {
  String cmd = "GET ";
  cmd += path;

  // Part 1: send info about data to send
  String xx = "AT+CIPSEND=";
  xx += cmd.length();
  if (!send(xx, ">", F("%% connect timeout"))) return false;
  Serial.print(">");

  // Part 2: send actual data
  if (!send(cmd, "SEND OK", F("%% no response"))) return false;

  return true;
}

String sendData(String command, const int timeout, boolean debug)
{
    String response = "";
    espSerial.print(command); // send the read character to the espSerial
    long int time = millis();
    while( (time+timeout) > millis())
    {
      while(espSerial.available())
      {
        // The esp has data so display its output to the serial window
        char c = espSerial.read(); // read the next character.
        response+=c;
      }
    }
    if(debug)
    {Serial.print(response);}
    return response;
}

void loop() {


  u8g.firstPage();  //OLED Display
  do {
    OLED_display(pression, temp, hum, lightValue);
    u8g.setColorIndex(1);
  } while ( u8g.nextPage() );

  // Thermo-Hygro
  hum = dht.readHumidity();
  //float t = dht.readTemperature();

  // Light
  lightValue = analogRead(light);

  // Barometer
  char status;

  status = pressure.startTemperature();
  if (status != 0)
  {
    // Wait for the measurement to complete:
    delay(status);
    // Retrieve the completed temperature measurement:
    // Note that the measurement is stored in the variable T.
    // Function returns 1 if successful, 0 if failure.
    status = pressure.getTemperature(temp);
    if (status != 0)
    {
      // Print out the measurement:
      /*
      Serial.print("temperature: ");
      Serial.print(T,2);
      Serial.print(" deg C, ");
      Serial.print((9.0/5.0)*T+32.0,2);
      Serial.println(" deg F");
      */

      // Start a pressure measurement:
      // The parameter is the oversampling setting, from 0 to 3 (highest res, longest wait).
      // If request is successful, the number of ms to wait is returned.
      // If request is unsuccessful, 0 is returned.
      status = pressure.startPressure(3);
      if (status != 0)
      {
        // Wait for the measurement to complete:
        delay(status);
        // Retrieve the completed pressure measurement:
        // Note that the measurement is stored in the variable P.
        // Note also that the function requires the previous temperature measurement (T).
        // (If temperature is stable, you can do one temperature measurement for a number of pressure measurements.)
        // Function returns 1 if successful, 0 if failure.
        status = pressure.getPressure(pression, temp);
        if (status != 0)
        {
          // Print out the measurement:
          /*
          Serial.print("absolute pressure: ");
          Serial.print(pression,2);
          Serial.print(" mb, ");
          Serial.print(P*0.0295333727,2);
          Serial.println(" inHg");
          */
        }
      }
    }
  }
 
  if (millis() - last_time < sample_interval) return;

  last_time = millis();

  if (!sendDataThingSpeak(pression, temp, hum, lightValue)) {
    Serial.println(F("%% failed sending data"));
    // we failed X times, at MAX_FAILS reconnect till it works
    if (fails++ > MAX_FAILS) {
      if (!resetESP()) return;
      if (!connectWiFi()) return;
    }
  } else {
    fails = 0;
  }

}

void OLED_display(float pression, float temp, float hum, float lightValue)
{
  if (isnan(pression) || isnan(hum) || isnan(temp) || isnan(lightValue)) {
    u8g.setFont(u8g_font_7x14);
    u8g.drawStr(10, 10, "Failed to read");
    u8g.drawStr(10, 25, "from sensors");
  } else {
    u8g.setFont(u8g_font_7x14);
    // Light
    u8g.setPrintPos(78, 24);
    u8g.print("L:");
    u8g.print(lightValue, 1);
    delay(1);
    u8g.setPrintPos(78, 10);
    u8g.print("H:");
    u8g.print(hum, 1);
    u8g.print("%");
    delay(1);
    // Barometer-Thermo
    u8g.setPrintPos(0, 24);
    u8g.print("P:");
    u8g.print(pression, 1);
    u8g.print("hPa");
    delay(1);
    u8g.setPrintPos(0, 10);
    u8g.print("T:");
    u8g.print(temp, 1);
    u8g.print("C");
    delay(1);
  }
  /*
  u8g.setPrintPos(10, 25);
  u8g.print("M:");
  u8g.print(MoistureValue, 1);
  delay(1);
  */
  u8g.setPrintPos(8, 36);
  u8g.print("smartmosphere.com");
  char strOut[3];
  u8g.setPrintPos(40, 50);
  formatTimeDigits(strOut, hour());
  u8g.print(strOut);
  u8g.print(":");
  formatTimeDigits(strOut, minute());
  u8g.print(strOut);
  u8g.print(":");
  formatTimeDigits(strOut, second());
  u8g.print(strOut);

  u8g.setPrintPos(32, 64);
  formatTimeDigits(strOut, day());
  u8g.print(strOut);
  u8g.print("/");
  formatTimeDigits(strOut, month());
  u8g.print(strOut);
  u8g.print("/");
  u8g.print(year());
  delay(1);

}

void formatTimeDigits(char strOut[3], int num)
{
  strOut[0] = '0' + (num / 10);
  strOut[1] = '0' + (num % 10);
  strOut[2] = '\0';
}

boolean sendDataThingSpeak(float temp, float pression, float hum, float lightValue) {
  if (!connect(THINGSPEAK_IP)) return false;

  String path = "/update?key=YOUR_THINGSPEAK_API_KEY&field1=";
  path += temp;
  path += "&field2=";
  path += pression;
  path += "&field3=";
  path += hum;
  path += "&field4=";
  path += lightValue;
  path += "\r\n";
  if (!sendGET(path)) return false;

  Serial.print(F(" thingspeak.com OK"));
  return true;
}
