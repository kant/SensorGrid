#include "io.h"

#include <SPI.h>
//#include <SD.h>
#include <SdFat.h>
static SdFat SD;

#define SD_CHIP_SELECT_PIN 10

static RH_RF95 rf95(RFM95_CS, RFM95_INT);
static int maxIDs[MAX_NETWORK_SIZE] = {0};
static uint32_t MSG_ID = 0;
static uint32_t lastAck = 0;
static uint8_t msgLen = sizeof(Message);

static uint8_t buf[sizeof(Message)] = {0};
static struct Message *msg = (struct Message*)buf;
static struct Message message = *msg;
static char* charBuf = (char*)buf;

static void clearBuffer() {
    memset(buf, 0, msgLen);
}

void setupRadio() {
    Serial.println(F("LoRa Test!"));
    digitalWrite(RFM95_RST, LOW);
    delay(10);
    digitalWrite(RFM95_RST, HIGH);
    delay(10);
    while (!rf95.init()) {
        fail(LORA_INIT_FAIL);
    }
    Serial.println(F("LoRa OK!"));
    if (!rf95.setFrequency(rf95Freq)) {
        fail(LORA_FREQ_FAIL);
    }
    Serial.print(F("FREQ: ")); Serial.println(rf95Freq);
    rf95.setTxPower(txPower, false);
    delay(100);
}

static void sendCurrentMessage() {
    rf95.send((const uint8_t*)buf, msgLen);
    rf95.waitPacketSent();
    flashLED(3, HIGH);
}

void printMessageData() {
    Serial.print(F("VER: ")); Serial.print((float)msg->ver_100/100);
    Serial.print(F("; NET: ")); Serial.print(msg->net);
    Serial.print(F("; SND: ")); Serial.print(msg->snd);
    Serial.print(F("; ORIG: ")); Serial.print(msg->orig);
    Serial.print(F("; ID: ")); Serial.println(msg->id);
    Serial.print(F("BAT: ")); Serial.println((float)msg->bat_100/100);
    Serial.print(F("TS: ")); Serial.println(msg->timestamp);
    Serial.println(F("Data:"));
    Serial.print(F("    [0] ")); Serial.print(msg->data[0]);
    Serial.print(F("    [1] ")); Serial.print(msg->data[1]);
    Serial.print(F("    [2] ")); Serial.print(msg->data[2]);
    Serial.print(F("    [3] ")); Serial.print(msg->data[3]);
    Serial.print(F("    [4] ")); Serial.print(msg->data[4]);
    Serial.print(F("    [5] ")); Serial.print(msg->data[5]);
    Serial.print(F("    [6] ")); Serial.print(msg->data[6]);
    Serial.print(F("    [7] ")); Serial.print(msg->data[7]);
    Serial.print(F("    [8] ")); Serial.print(msg->data[8]);
    Serial.print(F("    [9] ")); Serial.println(msg->data[9]);
}

static char* logline() {
    char str[200]; // 155+16 is current theoretical max
    sprintf(str,
        "%4.2f,%i,%i,%i,%i,%3.2f,%i,%i,%i,%i,%i,%i,%i,%i,%i,%i,%i",
        (float)(msg->ver_100)/100, msg->net, msg->snd, msg->orig, msg->id, (float)(msg->bat_100)/100, msg->timestamp,
        msg->data[0], msg->data[1], msg->data[2], msg->data[3], msg->data[4],
        msg->data[5], msg->data[6], msg->data[7], msg->data[8], msg->data[9]);
    return str;
}

static void warnNoGPSConfig() {
    Serial.println(F("WARNING! GPS data specified in data registers, but no GPS_MODULE in config"));
}


static uint32_t getDataByTypeName(char* type) {
    Serial.print("Getting data for type: "); Serial.println(type);
    if (!strcmp(type, "GPS_FIX")) {
        if (!gpsModule) warnNoGPSConfig();
        return GPS.fix;
    }
    if (!strcmp(type, "GPS_SATS")) {
        if (!gpsModule) warnNoGPSConfig();
        return GPS.satellites;
    }
    if (!strcmp(type, "GPS_SATFIX")) {
        if (!gpsModule) warnNoGPSConfig();
        if (GPS.fix) return GPS.satellites;
        return -1 * GPS.satellites;
    }
    if (!strcmp(type, "GPS_LAT_DEG")){
        if (!gpsModule) warnNoGPSConfig();
        Serial.println(GPS.lastNMEA());
        Serial.print("LATITUDE "); Serial.println(GPS.latitudeDegrees, DEC);
        return (int32_t)(roundf(GPS.latitudeDegrees * 1000));
    }
    if (!strcmp(type, "GPS_LON_DEG")) {
        if (!gpsModule) warnNoGPSConfig();
        Serial.println(GPS.lastNMEA()); // These are looking pretty ugly. Looks like a bad
                                        // overwrite of NMEA is corrupting the string from
                                        // around the end of the longitude field on GPGGA strings
        Serial.print("LONGITUDE "); Serial.println(GPS.longitudeDegrees, DEC);
        return (int32_t)(roundf(GPS.longitudeDegrees * 1000));
    }
    if (!strcmp(type, "SI7021_TEMP")) {
        return (int32_t)(sensorSi7021TempHumidity.readTemperature()*100);
    }
    if (!strcmp(type, "SI7021_HUMIDITY")) {
        return (int32_t)(sensorSi7021TempHumidity.readHumidity()*100);
    }
    if (!strcmp(type, "SI1145_VIS")) {
        return (int32_t)sensorSi1145UV.readVisible();
    }
    if (!strcmp(type, "SI1145_IR")) {
        return (int32_t)sensorSi1145UV.readIR();
    }
    if (!strcmp(type, "SI1145_UV")) {
        return (int32_t)sensorSi1145UV.readUV();
    }
    if (!strcmp(type, "FAKE_3")) {
        return 3333333;
    }
    if (!strcmp(type, "FAKE_4")) {
        return 4444444;
    }
    if (!strcmp(type, "FAKE_5")) {
        return 5555555;
    }
    if (!strcmp(type, "FAKE_6")) {
        return 6666666;
    }
    if (!strcmp(type, "FAKE_7")) {
        return 7777777;
    }
    if (!strcmp(type, "FAKE_8")) {
        return 8888888;
    }
    if (!strcmp(type, "FAKE_9")) {
        return 9999999;
    }
    Serial.print(F("WARNING! Unknown named data type: ")); Serial.println(type);
    return 0;
}

static uint32_t getRegisterData(char* registerName, char* defaultType) {
    char* type = getConfig(registerName);
    if (!type) {
        type = defaultType;
    }
    return getDataByTypeName(type);
}

static uint32_t getRegisterData(char* registerName) {
    char* type = getConfig(registerName);
    if (type) {
        return getDataByTypeName(type);
    } else {
        Serial.print(F("Data register ")); Serial.print(registerName);
        Serial.println(F(" not configured"));
        return 0;
    }
}

static void fillCurrentMessageData() {
      clearBuffer();
      uint16_t ver = (uint16_t)(roundf(protocolVersion * 100));
      msg->ver_100 = ver;
      msg->net = networkID;
      msg->snd = nodeID;
      msg->orig = nodeID;
      msg->id = MSG_ID;
      msg->bat_100 = (int16_t)(roundf(batteryLevel() * 100));
      msg->timestamp = rtc.now().unixtime();
      memcpy(msg->data, {0}, sizeof(msg->data));

      if (gpsModule) {
          /* If GPS_MODULE is set in config file, these data will default to the first
           *  3 data registers, but other configs can be set in the config file.
           */
          msg->data[0] = getRegisterData("DATA_0", "GPS_SATFIX");
          msg->data[1] = getRegisterData("DATA_1", "GPS_LAT_DEG");
          msg->data[2] = getRegisterData("DATA_2", "GPS_LON_DEG");
      } else {
          msg->data[0] = getRegisterData("DATA_0");
          msg->data[1] = getRegisterData("DATA_1");
          msg->data[2] = getRegisterData("DATA_2");
      }
      msg->data[3] = getRegisterData("DATA_3");
      msg->data[4] = getRegisterData("DATA_4");
      msg->data[5] = getRegisterData("DATA_5");
      msg->data[6] = getRegisterData("DATA_6");
      msg->data[7] = getRegisterData("DATA_7");
      msg->data[8] = getRegisterData("DATA_8");
      msg->data[9] = getRegisterData("DATA_9");

}

void transmit() {
      MSG_ID++;
      fillCurrentMessageData();
      printMessageData();

      /**
       * Unable to get WINC1500 and Adalogger to share SPI bus for logger writes.
       * WiFi does not want to reconnect after losing the SPI. Things tried:
       *
       * Setting Chip select pin modes (WiFi CS HIGH during logger write, logger LOW with
       *     reset to WiFi LOW after write)
       * WIFI_CLIENT.stop() ... begin()
       * WiFi.end()
       * WiFi.refresh()
       * SPI.endTransaction()
       * SPI.end() .. begin()
       * various time delays
       *
       * In the end, unable to get WiFi to continue after writing to the SD card.
       * For now settle with incompatibility between logging and WiFi API posts.
       * SD card read for configs still seems to be ok.
       *
       * This issue is logged as: https://github.com/NUKnightLab/SensorGrid/issues/2
       */

      if (!WiFiPresent && logfile) {
          char* line = logline();
          Serial.print(F("LOGLINE (")); Serial.print(strlen(line)); Serial.println("):");
          Serial.println(line);
          digitalWrite(SD_CHIP_SELECT_PIN, LOW);
          writeToSD(logfile, line);
          digitalWrite(SD_CHIP_SELECT_PIN, HIGH);
      }

      if (!WiFiPresent || !postToAPI(
            getConfig("WIFI_SSID"), getConfig("WIFI_PASS"), getConfig("API_SERVER"),
            getConfig("API_HOST"), atoi(getConfig("API_PORT")),
            charBuf, msgLen)) {
          flashLED(3, HIGH);
          Serial.println("Sending current message");
          sendCurrentMessage();
      }
}

static void _receive() {
    if (rf95.recv(buf, &msgLen)) {
        Serial.print(F(" ..RX (ver: ")); Serial.print(msg->ver_100);
        Serial.print(F(")"));
        Serial.print(F("    RSSI: ")); // min recommended RSSI: -91
        Serial.println(rf95.lastRssi(), DEC);
        uint16_t ver = (uint16_t)(roundf(protocolVersion * 100));
        if (msg->ver_100 != ver) {
            Serial.print(F("SKIP: unknown protocol version: ")); Serial.print(msg->ver_100/100);
            return;
        }
        if (msg->net > 0 && msg->net != networkID) {
            Serial.println(F("SKIP: out-of-network msg"));
            return;
        }
        if ( msg->orig == nodeID) {
            Serial.print(F("SKIP: own msg from: ")); Serial.println(msg->snd);
            Serial.print(F("LOST ACK: ")); Serial.println(msg->id - (lastAck+1));
            lastAck = msg->id;
            return;
        }
        printMessageData();
        flashLED(1, HIGH);
        if (oledOn) {
            display.fillRect(35, 16, 128, 32, BLACK);
            display.display();
            display.setTextColor(WHITE);
            display.setCursor(35, 16);
            display.print(msg->snd); display.print(":");
            display.print(msg->orig); display.print(".");
            display.print(msg->id);
            display.print(" ");display.print(rf95.lastRssi());
            display.display();
        }
        if (msg->id <= maxIDs[msg->orig]) {
            Serial.print(F("Ignore old Msg: "));
            Serial.print(msg->orig); Serial.print("."); Serial.print(msg->id);
            Serial.print(F(" (Current Max: ")); Serial.print(maxIDs[msg->orig]);
            Serial.println(F(")"));
            if (maxIDs[msg->orig] - msg->id > 5) {
                // Large spread of current max from received ID is indicative of a
                // node reset. TODO: Can we store the last ID in EEPROM and continue?
                // EEPROM on M0 Feather boards may be complex (if available at all?). See:
                // https://forums.adafruit.com/viewtopic.php?f=22&t=88272
                Serial.print(F("Reset Max ID. Node: "));
                Serial.println(msg->orig);
                maxIDs[msg->orig] = 0;
            }
        } else {
            msg->snd = nodeID;
            if (!WiFiPresent || !postToAPI(
                  getConfig("WIFI_SSID"), getConfig("WIFI_PASS"), getConfig("API_SERVER"),
                  getConfig("API_HOST"), atoi(getConfig("API_PORT")),
                  charBuf, msgLen)) {
                delay(RETRANSMIT_DELAY);
                Serial.print(F("RETRANSMITTING ..."));
                Serial.print(F("  snd: ")); Serial.print(msg->snd);
                Serial.print(F("; orig: ")); Serial.print(msg->orig);
                sendCurrentMessage();
                maxIDs[msg->orig] = msg->id;
                Serial.println(F("  ...RETRANSMITTED"));
            }
        }
    }
}

void receive() {
    /**
     * Randomized receive cycle to avoid loop sync across nodes
     */
    clearBuffer();
    int delta = 1000 + rand() % 1000;
    Serial.print(F("NODE ")); Serial.print(nodeID);
    Serial.print(F(" LISTEN: ")); Serial.print(delta);
    Serial.println(F("ms"));
    if (rf95.waitAvailableTimeout(delta)) {
        _receive();
    } else {
        Serial.println(F("  ..NO MSG REC"));
    }
}

void _writeToSD(char* filename, char* str) {
  Serial.print(F("Init SD card .."));
  if (!SD.begin(10)) {
        Serial.println(F(" .. SD card init failed!"));
        return;
  }
  if (false) {  // true to check available SD filespace
      Serial.print("SD free: ");
      uint32_t volFree = SD.vol()->freeClusterCount();
      float fs = 0.000512*volFree*SD.vol()->blocksPerCluster();
      Serial.println(fs);
  }
  File file;
  Serial.print(F("Writing to ")); Serial.print(filename);
  Serial.println(F(":")); Serial.println(str);
  file = SD.open(filename, O_WRITE|O_APPEND|O_CREAT);
  file.println(str);
  file.close();
  Serial.println("File closed");
}

void writeToSD(char* filename, char* str) {
  digitalWrite(8, HIGH);
  _writeToSD(filename, str);
  digitalWrite(8, LOW);
}
