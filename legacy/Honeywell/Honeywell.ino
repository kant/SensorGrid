#include "Honeywell.h"
#include <SD.h>
#include <Adafruit_SleepyDog.h>

Adafruit_FeatherOLED display = Adafruit_FeatherOLED();
RTC_PCF8523 rtc;
static bool DEEP_SLEEP = false;
static bool USE_WATCHDOG = true;
static float BAT_THRESH = 3.7;

byte enable_autosend[] = {0x68, 0x01, 0x40, 0x57};
byte stop_autosend[] = { 0x68, 0x01, 0x20, 0x77 };
byte start_pm[] = { 0x68, 0x01, 0x01, 0x96 };
byte stop_pm[] = { 0x68, 0x01, 0x02, 0x95 };
byte read_pm_results[] = { 0x68, 0x01, 0x04, 0x93 };
byte uartbuf[32];

#define UART_TIMEOUT 1000

#define DEFAULT_SD_CHIP_SELECT_PIN 10 //4 for Uno
#define ALTERNATE_RFM95_CS 19 //10 for Uno
#define DEFAULT_RFM95_CS 8 //not needed for Uno
#define MAX_WATCHDOG 16000

/* real time clock */
bool setClock = false;
char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
int nodeId = 1;


void sleep(int ms)
{
    if (DEEP_SLEEP) {
        Watchdog.sleep(ms);
    } else {
        delay(ms);
    }
}

/**
 * More or less deprecated but here in case needed. Currently, the broader read_message is being used
 */
bool read_control_ack(byte* buf)
{
    uint8_t i = 0;
    unsigned long activity_time = millis();
    while (i < 2 && (millis() - activity_time) < UART_TIMEOUT) {
        while (!Serial1.available()) {
            if ( (millis() - activity_time) > UART_TIMEOUT);
            return false;
        }
        byte b = Serial1.read();
        Serial.print(b, HEX);
        activity_time = millis();
        if (b == 0xA5 || b == 0x96 || b == 0x157) {
            if (i != 0 && b != buf[i-1]) {
                i = 0;
            }
            buf[i++] = b;
        } else {
            i = 0;
        }
    }
    if (i == 2) {
        return true;
    } else {
        return false;
    }
}

/*
 * Read the next message from the sensor off the UART
 */
bool read_message(byte* buf)
{
    uint8_t i = 0;
    uint8_t len = 32;
    unsigned long activity_time = millis();
    while (i < len && (millis() - activity_time) < UART_TIMEOUT) {
        while (!Serial1.available()) {
            if ( (millis() - activity_time) > UART_TIMEOUT);
            return false;
        }
        byte b = Serial1.read();
        Serial.print(b, HEX);
        activity_time = millis();
        Serial.print(" ");
        if (i == 0) {
            if (b == 0x40 || b == 0xA5 || b == 0x96 || b == 0x42) {
                buf[i++] = b;
                switch (b) {
                    case 0x40:
                        len = 8; break;
                    case 0xA5:
                        len = 2; break;
                    case 0x96:
                        len = 2; break;
                    case 0x42:
                        len = 32; break;
                }
            }
        } else if (i == 1) {
            if (  (b == 0x05 && buf[0] == 0x40)
                  || (b == 0xA5 && buf[0] == 0xA5)
                  || (b == 0x96 && buf[0] == 0x96)
                  || (b == 0x4d && buf[0] == 0x42) ) {
                buf[i++] = b;
            } else {
                i = 0;
            }
        } else if (i == 2) {
            if ( (b != 0x04)
                 ||(buf[0] == 0x40 && buf[1] == 0x05) ) {
                buf[i++] = b;
            } else {
                i = 0;
            }
        } else {
            buf[i++] = b;
        }
    }
    for (int j=0; j<i; j++) {
        Serial.print(buf[j]); Serial.print(" ");
    }
    if (i == len) {
        return true;
    } else {
        return false;
    }
}

void read_pm_results_data(byte* buf)
{
    Serial.print("SENDING: READ_PARTICLE_MEASURING_RESULTS ..");
    Serial1.write(read_pm_results, 4);
    read_message(uartbuf);
    Serial.println("");
    Serial.print("PM 2.5: "); Serial.println(buf[3]*256 + buf[4], DEC);
    Serial.print("PM 10: "); Serial.println(buf[5]*256+buf[6], DEC);
}


bool send_start_pm()
{
    Serial.print("SENDING: START_PARTICLE_MEASUREMENT ..");
    Serial1.write(start_pm, 4);
    if (read_message(uartbuf) && uartbuf[0] == 0xA5 && uartbuf[1] == 0xA5) {
        Serial.println(".. SENT");
        return true;
    } else {
        Serial.println("\nWARNING: Trouble sending START_PARTICLE_MEASUREMENT");
        return false;
    }
}

void send_stop_pm()
{
    Serial.print("SENDING: STOP_PARTICLE_MEASUREMENT ..");
    Serial1.write(stop_pm, 4);
    /* For some reason, the sensor is returning a data message instead of a STOP_PM ack. The
     *  following logic attempts to read out a subsequent STOP_PM ack with the assumption
     *  that the data just needs to be cleared out of the buffer -- but that does not seem
     *  to be the case. Rather, it seems that we are really getting a data payload instead of the ACK
     *
     *  TODO: verify that the fan is stopping consistently even if a data message is received
     *  instead of an ACK. If so, warning can be removed.
     */
    if (read_message(uartbuf)) {
        if(uartbuf[0] == 0xA5 && uartbuf[1] == 0xA5) {
            Serial.println(".. SENT");
            return;
        } else if (uartbuf[0] == 0x40 && uartbuf[1] == 0x05) {
            if (read_message(uartbuf) && uartbuf[0] == 0xA5 && uartbuf[1] == 0xA5) {
                Serial.println(".. SENT");
                return;
            }
        }
    }
    Serial.println("\nTrouble sending: STOP_PARTICLE_MEASUREMENT");
    Serial.println("WARNING: Sensor fan may not have stopped");
}

void send_stop_autosend()
{
    Serial.print("SENDING: STOP_AUTOSEND ..");
    Serial1.write(stop_autosend, 4);
    if (read_message(uartbuf) && uartbuf[0] == 0xA5 && uartbuf[1] == 0xA5) {
        Serial.println(".. SENT");
    } else {
        Serial.println("\nTrouble sending: STOP_AUTOSEND");
    }
}

void setup()
{
    //while (!Serial);
    setupDisplay();
    delay(5000);
    if (Serial) {
        DEEP_SLEEP = false;
    }
    Serial.begin(115200);
    Serial1.begin(9600);
    if (!DEEP_SLEEP) {
        Serial.println("Continuing without sleep enabled");
    }
    Serial.print("Initializing SD card...");
    digitalWrite(DEFAULT_SD_CHIP_SELECT_PIN, HIGH);
    digitalWrite(DEFAULT_RFM95_CS, HIGH);
    digitalWrite(ALTERNATE_RFM95_CS, HIGH);
    if (!SD.begin(DEFAULT_SD_CHIP_SELECT_PIN)) {
        digitalWrite(ALTERNATE_RFM95_CS, LOW);
        digitalWrite(DEFAULT_SD_CHIP_SELECT_PIN, LOW);
        Serial.println("Card failed, or not present");
    }
    Serial.println("card initialized.");
    if (!rtc.begin()) {
        Serial.println("Error: Failed to initialize RTC");
    }
    if (setClock) {
        Serial.print("Printing initial DateTime: ");
        rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
        Serial.print(F(__DATE__));
        Serial.print('/');
        Serial.println(F(__TIME__));
    }
    displayCurrentRTCDateTime();
    delay(3000);
    //int countdownMS = Watchdog.enable(MAX_WATCHDOG);
    //Serial.print("Enabled the watchdog with max countdown of ");
    //Serial.print(countdownMS, DEC);
    //Serial.println(" milliseconds!");
    //Serial.println();
}

static long sample_period = 60 * 10;
static long heartbeat_period = 30;
static long last_data_sample = 0 - sample_period;
static long last_heartbeat = 0 - heartbeat_period;

void loop()
{
    Watchdog.reset();
    float bat = batteryLevel();
    long diff_h = rtc.now().secondstime() - last_heartbeat;
    if (diff_h > heartbeat_period) {
        digitalWrite(LED_BUILTIN, HIGH);
        sleep(500);
        digitalWrite(LED_BUILTIN, LOW);
        if (bat <= (BAT_THRESH + 0.1)) {
            digitalWrite(LED_BUILTIN, HIGH);
            sleep(500);
            digitalWrite(LED_BUILTIN, LOW);
        }
        last_heartbeat = rtc.now().secondstime();
    }
    long diff = rtc.now().secondstime() - last_data_sample;
    if (diff < sample_period) {
        /* Sleep up to remainder of period or max watchdog sleep time */
        //sleep( (sample_period - diff) *1000 );
        sleep(1000);
        return;
    }
    last_data_sample = rtc.now().secondstime();
    if (bat >= BAT_THRESH) {
        send_start_pm();
        delay(100);
        send_stop_autosend();
        for (int i=0; i<6; i++) {
            Serial.print(".");
            sleep(1000);
        }
        Serial.println("\nReading samples ---");
        for (int i=0; i<5; i++) {
            read_pm_results_data(uartbuf);
            delay(100);
        }
        Serial.println("\n---");
        send_stop_pm();
        delay(100);
    }

    File dataFile = SD.open("datalog.txt", FILE_WRITE);
    if (dataFile) {
        Serial.println("Opening dataFile...");
    } else {
        Serial.println("error opening datalog.txt");
    }
    DateTime now = rtc.now();
    dataFile.print(now.year());
    dataFile.print("-");
    dataFile.print(now.month());
    dataFile.print("-");
    dataFile.print(now.day());
    dataFile.print("T");
    dataFile.print(now.hour());
    dataFile.print(":");
    dataFile.print(now.minute());
    dataFile.print(":");
    dataFile.print(now.second());
    dataFile.print(",");
    dataFile.print(bat);
    dataFile.print(",");

    if (bat >= BAT_THRESH) {
        dataFile.print(uartbuf[3]*256 + uartbuf[4]); // PM 2.5
        dataFile.print(",");
        dataFile.println(uartbuf[5]*256+uartbuf[6]); // PM 10
    } else {
        dataFile.print(",");
    }

    Serial.println("Closing dataFile");
    dataFile.close();

}
