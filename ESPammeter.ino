/*
    Name:       ESPammeter.ino
    Created:	2/07/2018 11:26:00 AM
    Author:     HOME\bflint
*/

#include <Adafruit_INA219.h>
#include <debuglogger.h>
#include <Tiny4kOLED.h>

// instance
Adafruit_INA219 ina219;
SerialDebug debugger;// (debug::dbImportant);
SSD1306Device oled;

unsigned long lastMillis = 0, startedMillis=0;


void setup()
{
	Wire.begin();
	ina219.begin();
	ina219.setCalibration_32V_1A();

	debugger.begin(9600);
	oled.begin();

	delay(2000);

	oled.on();
	oled.switchRenderFrame();

	debugger.println(debug::dbInfo, "Running");

	startedMillis=lastMillis = millis();
}

float minCurrent = 1000, maxCurrent = 0, lastCurrent = 0;

float powerConsumed=0;

unsigned long loopCount = 0;

#define _HOUR_IN_MS	(float)(60*60*1000)

void loop()
{

	float current=ina219.getCurrent_mA();

	if (current < 0)
		current = 0;

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

		debugger.printf(debug::dbInfo, "%.3f %.3f %.3f \n\r", hourRatio, powerConsumed, projectedMAH);


		oled.print(nextLine);
		oled.switchFrame();

	}

	lastCurrent = current;
	lastMillis = millis();
	++loopCount;

}
