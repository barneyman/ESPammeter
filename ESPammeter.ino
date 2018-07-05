/*
    Name:       ESPammeter.ino
    Created:	2/07/2018 11:26:00 AM
    Author:     HOME\bflint
*/

#include <Adafruit_INA219.h>
#include <debuglogger.h>
#include <Tiny4kOLED.h>


#define FS_NO_GLOBALS	// turn off the 'Using'
#include <FS.h>

// instance
Adafruit_INA219 ina219;
SerialDebug debugger;// (debug::dbImportant);
SSD1306Device oled;

#include <myWifi.h>
myWifiClass wifiInstance("wemos_", &debugger);
myWifiClass::wifiDetails blank;


unsigned long lastMillis = 0, startedMillis=0;

#include <circQ.h>

class tempQ : public circQueueT<500, unsigned, unsigned short>
{
public:
	tempQ() :circQueueT<500, unsigned, unsigned short>(true)
	{

	}

	// debug
	void Dump(debugBaseClass *debugger)
	{
		debugger->printf(debug::dbImportant, "Dump of CircQ - %u available %u readC %u writeC\n\r", available(), readCursor, writeCursor);
		for (int peeker = 0; peeker < available(); peeker++)
		{
			debugger->printf(debug::dbImportant, "%d,", peek(peeker));
		}
		debugger->println(debug::dbImportant, "");
	}

};


typedef circQueueT<1000, unsigned, int> currentQueue;
//typedef tempQ currentQueue;

currentQueue dataReadings;

class readingsStreamer : public Stream
{
protected:

	String m_preamble, m_postamble;
public:
	readingsStreamer(currentQueue *hostData):m_hostData(hostData), m_preamble("{\"data\":["), m_postamble("]}\n\r")
	{
		// work out the size
		// {"data":[xxxx,]}
		// 4 chars and optional comma
		m_rawDataSize = hostData->available() * 4;
		// account for the comma
		if (hostData->available())
			m_rawDataSize += (hostData->available() - 1);

		m_jsonSize = m_preamble.length()+m_postamble.length() + (m_rawDataSize);

		m_cursor = 0;

		// debug
		//Serial.printf("\n\r(%d) - ", m_hostData->available());
		//for(int peek = 0; peek < m_hostData->available();peek++)
		//{
		//	Serial.printf("%d ", m_hostData->peek(peek));
		//}
		//Serial.println("");
	}

	String name() { return "rawData"; }
	size_t size() 
	{ 
		return m_jsonSize;
	}

	virtual int available()
	{
		return m_jsonSize- m_cursor;
	}

	virtual int read()
	{
		static int currentReading = 0;
		int retval = -1;
		if (m_cursor < m_preamble.length())
		{
			retval = m_preamble[m_cursor];
		}
		else if (m_cursor < (m_preamble.length()+ m_rawDataSize))
		{
			size_t temp = (m_cursor - m_preamble.length())%5;
			switch (temp)
			{
			case 0:
				currentReading = m_hostData->read()%10000;
				if (currentReading < 1000)
					retval = ' ';
				else
					retval = (currentReading / 1000) + '0';
				break;
			case 1:
				if (currentReading < 100)
					retval = ' ';
				else
					retval = ((currentReading % 1000)/100) + '0';
				break;
			case 2:
				if (currentReading < 10)
					retval = ' ';
				else
					retval = ((currentReading % 100) / 10) + '0';
				break;
			case 3:
				retval = ((currentReading % 10) ) + '0';
				break;
			case 4:
				retval = ',';
				yield();
				break;
			}
		}
		else
		{
			// must be post amble
			retval = m_postamble[m_cursor - (m_preamble.length() + m_rawDataSize)];
		}
		m_cursor++;

		//Serial.printf("%c", retval);

		return retval;
	}

	virtual int peek()
	{
		return 1;
	}

	virtual size_t write(uint8_t ee)
	{
		return 0;
	}

protected:

	currentQueue *m_hostData;
	size_t m_jsonSize, m_cursor, m_rawDataSize;

};



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

	// create an AP
	wifiInstance.ConnectWifi(myWifiClass::modeAP, blank);

	wifiInstance.server.on("/", HTTP_GET, []() {

		fs::File f;
		f = SPIFFS.open("/default.htm", "r");
		wifiInstance.server.streamFile(f, "text/html");
		f.close();

	});


	wifiInstance.server.on("/rawdata.json", HTTP_GET, []() {

		// send the json out raw by hijacking the streamFile method
		readingsStreamer rs(&dataReadings);
		wifiInstance.server.streamFile(rs, "text/json");


	});


	fs::Dir dir = SPIFFS.openDir("/");
	while (dir.next()) {
		String file = dir.fileName();

		// cache it for an hour
		wifiInstance.server.serveStatic(file.c_str(), SPIFFS, file.c_str(), "");

		debugger.printf(debug::dbVerbose, "Serving %s\n\r", file.c_str());

	}

	startedMillis=lastMillis = millis();
}

float minCurrent = 1000, maxCurrent = 0, lastCurrent = 0;

float powerConsumed=0;

unsigned long loopCount = 0;





#define _HOUR_IN_MS	(float)(60*60*1000)

void loop()
{

	float current=ina219.getCurrent_mA();

	//if (current < 0)
	//	current = 0;

	if (current > maxCurrent)
		maxCurrent = current;
	if (current < minCurrent)
		minCurrent = current;

	unsigned long millisSince = millis() - lastMillis;

	float powerConsumedMAH = ((float)(current + lastCurrent) / 2.0)*( (float)millisSince/(_HOUR_IN_MS) );

	powerConsumed += powerConsumedMAH;

	//debugger.printf(debug::dbInfo, "%.1f ma (max %.1f min %.1f mah %.3f)\n\r", current, maxCurrent, minCurrent, powerConsumedMAH);

	if (!(loopCount % 1000))
	{
		unsigned long totalMillis = millis() - startedMillis;
		float hourRatio = ((float)(totalMillis) / _HOUR_IN_MS);
		float projectedMAH = powerConsumed / hourRatio;

		String topLine(current, 1);
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

	}

	dataReadings.write(current);
	//dataReadings.write(loopCount);

	lastCurrent = current;
	lastMillis = millis();
	++loopCount;

	wifiInstance.server.handleClient();
}
