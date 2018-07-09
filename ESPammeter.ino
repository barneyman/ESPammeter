/*
    Name:       ESPammeter.ino
    Created:	2/07/2018 11:26:00 AM
    Author:     HOME\bflint
*/

#include <Adafruit_INA219.h>
#include <debuglogger.h>
#include <Tiny4kOLED.h>


#define FS_NO_GLOBALS	// turn off the 'Using'
#ifdef ESP8266
#include <FS.h>
#else
#include <spiffs.h>
#endif

// instance
Adafruit_INA219 ina219;
SerialDebug debugger(debug::dbVerbose);// (debug::dbImportant);
SSD1306Device oled;

#include <myWifi.h>
myWifiClass wifiInstance("wemos_", &debugger);
myWifiClass::wifiDetails wifiConfig;


unsigned long lastMillis = 0, startedMillis=0;

#include <circQ.h>

class ochl
{
public:
	ochl()
	{}

	void open(int value)
	{
		m_open = value;
		m_lo = 1000;
		m_hi = -1000;
		latest(value);
	}

	void close(int value)
	{
		m_close = value;
		latest(value);
	}

	void latest(int value)
	{
		if (value < m_lo)
			m_lo = value;

		if (value > m_hi)
			m_hi = value;
	}

	int m_open, m_close, m_hi, m_lo;
};

#define _SAMPLE_PERIOD_MILLIS	1000
// sample every second, store 5 mins worth
typedef circQueueT<300, ochl, int> currentQueue;

currentQueue dataReadings;


#define _JSON_CONFIG_FILE "/config.json"

bool readConfig()
{

	debugger.println(debug::dbVerbose, "reading config");

	// try to read the config - if it fails, create the default
	fs::File configFile = SPIFFS.open(_JSON_CONFIG_FILE, "r");

	if (!configFile)
	{
		debugger.println(debug::dbImportant, "no config file on SPIFFS");
		return false;
	}

	String configText = configFile.readString();

	configFile.close();

	DynamicJsonBuffer jsonBuffer;

	JsonObject &root = jsonBuffer.parseObject(configText);

	return wifiInstance.ReadDetailsFromJSON(root, wifiConfig);

}


#define _MYVERSION	"1.0"
#define _PAGESIZE	15

volatile bool collecting = true;

void setup()
{
	SPIFFS.begin();
	Wire.begin();
	ina219.begin();
	ina219.setCalibration_32V_1A();

	debugger.begin(9600);
	oled.begin();

	delay(2000);

	oled.on();
	oled.switchRenderFrame();

	debugger.println(debug::dbInfo, "Running");

	if (!readConfig())
	{
		// create an AP
		wifiInstance.QuickStartAP();

	}
	else
	{
		wifiInstance.ConnectWifi(myWifiClass::modeSTA, wifiConfig);
	}

	wifiInstance.server.on("/", HTTP_GET, []() {

		fs::File f;
		f = SPIFFS.open("/default.htm", "r");
		wifiInstance.server.streamFile(f, "text/html");
		f.close();

	});

	wifiInstance.server.on("/stop", HTTP_GET, []() {
		collecting = false;
		wifiInstance.server.send(200);
	});

	wifiInstance.server.on("/start", HTTP_GET, []() {
		collecting = true;
		wifiInstance.server.send(200);
	});


	wifiInstance.server.on("/json/data", HTTP_GET, []() {

		// tell them how many samples we have
		DynamicJsonBuffer jsonBuffer;

		JsonObject &root = jsonBuffer.createObject();


		root["pages"] = (dataReadings.available()/_PAGESIZE)+((dataReadings.size() % _PAGESIZE)?1:0);
		root["pagesize"] = _PAGESIZE;

		String jsonText;
		root.prettyPrintTo(jsonText);

		//debugger.println(debug::dbVerbose, jsonText);

		wifiInstance.server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
		wifiInstance.server.send(200, "application/json", jsonText);


	});


	wifiInstance.server.on("/json/page", HTTP_GET, []() {

		// tell them how many samples we have
		DynamicJsonBuffer jsonBuffer;

		JsonObject &root = jsonBuffer.createObject();
		int pageNumber= wifiInstance.server.arg("page").toInt();
		root["page"] = pageNumber;

		JsonArray &dataArray = root.createNestedArray("data");

		for (int leaf = 0; (leaf < _PAGESIZE) && dataReadings.available(); leaf++)
		{
			JsonObject &pageleaf=dataArray.createNestedObject();

			ochl thisOne = dataReadings.read();

			pageleaf["o"] = thisOne.m_open;
			pageleaf["c"] = thisOne.m_close;
			pageleaf["h"] = thisOne.m_hi;
			pageleaf["l"] = thisOne.m_lo;
			pageleaf["t"] = (pageNumber * _PAGESIZE) + leaf;
			yield();
		}

		String jsonText;
		root.prettyPrintTo(jsonText);

		//debugger.println(debug::dbInfo,jsonText);

		wifiInstance.server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
		wifiInstance.server.send(200, "application/json", jsonText);


	});



	wifiInstance.server.on("/json/config", HTTP_GET, []() {
		// give them back the port / switch map
		debugger.println(debug::dbInfo, "json config called");

		DynamicJsonBuffer jsonBuffer;

		JsonObject &root = jsonBuffer.createObject();

		root["name"] = wifiInstance.m_hostName.c_str();
		root["version"] = _MYVERSION;

		String jsonText;
		root.prettyPrintTo(jsonText);

		debugger.println(debug::dbVerbose, jsonText);

		wifiInstance.server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
		wifiInstance.server.send(200, "application/json", jsonText);
	});

#ifdef ESP8266
	fs::Dir dir = SPIFFS.openDir("/");
	while (dir.next()) 
#else
	fs::File root = SPIFFS.open("/");
	fs::File dir;
	while(dir= root.openNextFile())
#endif
	{
		String file =
#ifdef ESP8266
			dir.fileName();
#else
			dir.name();
#endif
		// cache it for an hour
		wifiInstance.server.serveStatic(file.c_str(), SPIFFS, file.c_str(), "");

		debugger.printf(debug::dbVerbose, "Serving %s\n\r", file.c_str());

	}


	startedMillis=lastMillis = millis();
}

float minCurrent = 1000, maxCurrent = 0, lastCurrent = 0;

float powerConsumed=0;

unsigned long loopCount = 0;
unsigned long lastSampleMs=0;

ochl workingValue;
bool reopenWorkingValue = true;

#define _HOUR_IN_MS	(float)(60*60*1000)


void loop()
{

	float current=ina219.getCurrent_mA();

	if (current > maxCurrent)
		maxCurrent = current;
	if (current < minCurrent)
		minCurrent = current;

	unsigned long millisSince = millis() - lastMillis;

	float powerConsumedMAH = ((float)(current + lastCurrent) / 2.0)*( (float)millisSince/(_HOUR_IN_MS) );

	powerConsumed += powerConsumedMAH;

	//debugger.printf(debug::dbInfo, "%.1f ma (max %.1f min %.1f mah %.3f)\n\r", current, maxCurrent, minCurrent, powerConsumedMAH);

	if (millis()-lastSampleMs > _SAMPLE_PERIOD_MILLIS)
	{
		lastSampleMs = millis();

		unsigned long totalMillis = millis() - startedMillis;
		float hourRatio = ((float)(totalMillis) / _HOUR_IN_MS);
		float projectedMAH = powerConsumed / hourRatio;

		oled.setInverse(!collecting);

		String topLine(current, 0);
		topLine += "ma ";

		topLine += String(projectedMAH, 0);
		topLine += "maH";

		String nextLine(maxCurrent, 0);
		nextLine += " max ";
		nextLine += String(minCurrent, 0);
		nextLine += " min";

		oled.setFont(FONT8X16);

		oled.clear();
		oled.setCursor((128-(topLine.length()*8)), 0);
		oled.print(topLine);

		oled.setFont(FONT6X8);

		oled.setCursor(0, 2);
		oled.print(nextLine);

		oled.setCursor(0, 3);
		nextLine = String(powerConsumed, 1);
		nextLine += " maH in ";

		unsigned long secs = totalMillis / 1000;

		nextLine += String(secs/60);
		nextLine += ":";
		if(secs%60 < 10)
			nextLine += "0";
		nextLine += String(secs % 60);

		//debugger.printf(debug::dbInfo, "%.3f %.3f %.3f \n\r", hourRatio, powerConsumed, projectedMAH);
		//dataReadings.Dump(&debugger);

		oled.print(nextLine);
		oled.switchFrame();

		if(loopCount)
		{
			workingValue.close(current);
			if(collecting)
				dataReadings.write(workingValue);
		}
		reopenWorkingValue = true;
	}

	if (reopenWorkingValue)
	{
		workingValue.open(current);
		reopenWorkingValue = false;
	}
	else
		workingValue.latest(current);

	//dataReadings.write(loopCount);

	lastCurrent = current;
	lastMillis = millis();
	++loopCount;

	wifiInstance.server.handleClient();
}
