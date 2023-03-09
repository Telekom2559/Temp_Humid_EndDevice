#include <BLEDevice.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Ticker.h>
#include <SPIFFS.h>
#include <TinyGsmClientSIM7600.h>

#define SerialAT Serial1
#define SerialMon Serial

#define MIN_SPACE 500000
#define UART_BAUD 115200
#define PIN_DTR 25
#define PIN_TX 26
#define PIN_RX 27
#define PWR_PIN 4
#define PIN_RST 5
#define PIN_SUP 12
#define BAT_ADC 35

// #define MJ_ADDR "A4:C1:38:54:6E:F2"
#define apiKey (String) "pk.71031a62fba9814c0898ae766b971df1"

#define FTPS_ADDR               "188.166.217.51"
#define FTPS_PRT                7021
#define FTPS_LOG_PATH           "/DEV/Log/" // "/BER/" 
// #define FTPS_CONF_PATH          "/DEV/Config/"
#define FTPS_USRN               "tung"
#define FTPS_PASS               "anundaJJ795"
#define FTPS_TYPE               1
// #define CONF_FNAME              "ble_config.json"

// Sensor config
#define CONF_ADDR               "161.246.35.199"
#define CONF_PRT                80
#define CONF_FULL_FNAME         "/~tung/Frozen_Proj/Config/sensor_config.json"
#define DEV_ID                  "froz0001"


String makefilename;
String json_str = "{\"config\":[{\"dev_id\":\"froz0001\",\"target\":[\"A4:C1:38:54:6E:F2\",\"A4:C1:38:A9:4B:B3\",\"A4:C1:38:24:27:29\",\"A4:C1:38:1D:F5:24\",\"A4:C1:38:30:D3:A3\",\"A4:C1:38:EF:1C:30\"]},{\"dev_id\":\"froz0002\",\"target\":[\"A4:C1:38:30:D3:A3\",\"A4:C1:38:EF:1C:30\"]}]}";
String mijia_list[] = {"A4:C1:38:54:6E:F2","A4:C1:38:A9:4B:B3","A4:C1:38:24:27:29","A4:C1:38:1D:F5:24","A4:C1:38:30:D3:A3","A4:C1:38:EF:1C:30"};

TinyGsmSim7600 modem(SerialAT);
TinyGsmSim7600::GsmClientSim7600 client(modem);

BLEClient* pClient;

struct RTC_INFO
{
    String date ="";
    String time ="";
};
RTC_INFO rtc_info;

struct SENSOR_DATA {
    String temp; // Use 3
    String humi; // Use 4
};

// struct SENSOR_DATA {
//     float temp; // Use 3
//     float humi; // Use 4
// };

SENSOR_DATA tempandhumi;

struct NETWORK_INFO {
    // Location
    String date; // Use 1
    String time; // Use 2
    String lat; // Use 5
    String lon; // Use 6

    // Cell site information
    char type[10];
    char mode[10];
    String mcc; 
    String mnc; 
    int lac = 0;
    String cid; 
    char freq_b[15];
    double rsrq = 0;
    double rsrp = 0;
    double rssi = 0;
    int rssnr; 
};

NETWORK_INFO networkinfo;

struct BATT_INFO {
    String batt_volt;
    String batt_level;
};

BATT_INFO battinfo;

// The remote service we wish to connect to.
static BLEUUID serviceUUID("ebe0ccb0-7a0a-4b0c-8a1a-6ff2997da3a6");

// The characteristic of the remote service we are interested in.
static BLEUUID charUUID("ebe0ccc1-7a0a-4b0c-8a1a-6ff2997da3a6");

void decrypted(BLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify)
{
    int16_t tmp_data = (pData[0] | (pData[1] << 8));
    tempandhumi.temp = ((float)tmp_data*0.01);
    tempandhumi.humi = (float)pData[2];
}

void connectToSensor(BLEAddress pAddress)
{
    pClient = BLEDevice::createClient();
    pClient->connect(pAddress);
    delay(200);
    if (pClient->isConnected())
    {
        BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
        if (pRemoteService == nullptr) {
            Serial.print("Failed to find our service UUID: ");
            Serial.println(serviceUUID.toString().c_str());
            pClient->disconnect();
        }

        BLERemoteCharacteristic* pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
        if (pRemoteCharacteristic == nullptr) {
            Serial.print("Failed to find our characteristic UUID: ");
            Serial.println(charUUID.toString().c_str());
            pClient->disconnect();
        }
        pRemoteCharacteristic->registerForNotify(decrypted); // Call decrypt here
        delay(5000); //(5000)
        pClient->disconnect();
        delay(1000); // (2000)
    }
    delete pClient; // https://github.com/espressif/arduino-esp32/issues/3335
}

void modemPowerOn()
{
    pinMode(PIN_SUP, OUTPUT);
    digitalWrite(PIN_SUP, HIGH);
    pinMode(PIN_RST, OUTPUT);
    digitalWrite(PIN_RST, LOW);
    delay(100);
    digitalWrite(PIN_RST, HIGH);
    delay(3000);
    digitalWrite(PIN_RST, LOW);
    pinMode(PWR_PIN, OUTPUT);
    digitalWrite(PWR_PIN, LOW);
    delay(100);
    digitalWrite(PWR_PIN, HIGH);
    delay(1000);
    digitalWrite(PWR_PIN, LOW);
}

void readBattlevel()
{
    int batt_adc_avg = 0;
    int batt_val_idx = 0;
    // Store batt adc for 1000 values tehn average them
    do {
        batt_adc_avg += analogRead(BAT_ADC);
        batt_val_idx++;
    } while (batt_val_idx < 100000);
    delay(1000);
    batt_adc_avg = (batt_adc_avg / batt_val_idx) + 212;
    float batt_v = (2 * batt_adc_avg * 3.3) / 4096;
    battinfo.batt_volt = String(batt_v, 2);
    float batt_level = 100 * (1 - ((4.24 - batt_v) / (4.24 - 2.5))); // Batt off @ v = 2.5 v, full @ v = 4.12 v
    battinfo.batt_level = String(batt_level, 2);
}

String sendAT(String _command, int interval, boolean _debug)
{
    String _response = "";
    SerialAT.println(_command);
    long int starttime = millis();
    while (((millis() - starttime)) < interval) {
        while (SerialAT.available() > 0) {
            int _read = SerialAT.read();
            _response += char(_read);
        }
    }
    SerialAT.flush();
    if (_debug) 
    {
      SerialMon.print(_response);
    }
    return _response;
}

void connect2LTE()
{
    // ref
    // https://m2msupport.net/m2msupport/tcp-ip-testing-with-simcom-sim7500-and-sim7600-modules/
    boolean DEBUG = 1;
    SerialMon.println("\n1. Close network\n");
    delay(1000);
    sendAT("AT+NETCLOSE", 8000, DEBUG);
    SerialMon.println("\n2. Check SIM insertion\n");
    delay(1000);
    sendAT("AT+CPIN?", 5000, DEBUG);
    SerialMon.println("\n3. Open network\n");
    delay(1000);
    sendAT("AT+CSOCKSETPN=1", 5000, DEBUG);
    sendAT("AT+NETOPEN", 5000, DEBUG);
    sendAT("AT+IPADDR", 5000, DEBUG);
    SerialMon.println("\n4. Ping checking\n");
    delay(1000);
    sendAT("AT+CPING=\"www.google.co.th\",1,4", 10000, DEBUG);
    // AT+CPING=<dest_addr>,<dest_addr_type>, < num_pings>
    // response : +CPING: 3,4,4,0,55,80,66
    // response :
    //<result_type>,<num_pkts_sent>,<num_pkts_recvd>,<num_pkts_lost>,<min_rtt>,<max_rtt>, < avg_rtt>
}

RTC_INFO getRTC()
{
    RTC_INFO _res_rtc_info;
    // Before use AT, you must take AT+CTZU=1 to set automaticaly timezone update!!
    String _res_rtc = sendAT("AT+CCLK?",3000,1);
    uint8_t _res_head_idx = _res_rtc.indexOf("+CCLK: \"");
    String _date = _res_rtc.substring(_res_head_idx+sizeof("+CCLK: \"")-1,_res_head_idx+sizeof("+CCLK: \"")+7);
    String _time = _res_rtc.substring(_res_head_idx+sizeof("+CCLK: \"")+8,_res_head_idx+sizeof("+CCLK: \"")+16);
    String _dz = _res_rtc.substring(_res_head_idx+sizeof("+CCLK: \"")+16,_res_head_idx+sizeof("+CCLK: \"")+19);

    int8_t _offset_h = atoi(_dz.c_str())/4;

    int colonIndex = 0;
    int lastColonIndex = 0;
    String tmp_time[3];
    for (int i = 0; i < 3; i++) 
    {
        colonIndex = _time.indexOf(':', lastColonIndex);
        String temp = _time.substring(lastColonIndex, colonIndex);
        tmp_time[i] = temp;
        lastColonIndex = colonIndex + 1;
    }
    int8_t _hh = atoi(tmp_time[0].c_str());
    int8_t _MM = atoi(tmp_time[1].c_str());
    int8_t _ss = atoi(tmp_time[2].c_str());
    int _sod = _hh*3600 + _MM*60 + _ss; 

    int commaIndex = 0;
    int lastCommaIndex = 0;
    String values[3];
    for (int i = 0; i < 3; i++) 
    {
        commaIndex = _date.indexOf('/', lastCommaIndex);
        String temp = _date.substring(lastCommaIndex, commaIndex);
        values[i] = temp;
        lastCommaIndex = commaIndex + 1;
    }
    int8_t _yy = atoi(values[0].c_str());
    int8_t _mm = atoi(values[1].c_str());
    int8_t _dd = atoi(values[2].c_str());

    if (strcmp(_dz.c_str(),"+00") == 0) return _res_rtc_info;
    _sod = _sod - (3600*_offset_h);
    if (_offset_h > 0) // Local is faster than UTC
    {
        if (_sod < 0)
        {
            _sod = 86400 + _sod;
            if (_dd > 1) 
            {
                _dd = _dd - 1;
            }
            else if (_dd == 1)
            {
                if (_mm == 1)
                {
                    _mm = 12;
                    _yy = _yy - 1;
                    _dd = 31;
                }
                else
                {
                    if ((_mm == 5) || (_mm == 7) || (_mm == 10) || (_mm == 12) )
                    {
                        _dd = 30;
                    }
                    else if (_mm == 3)
                    {
                        ((_yy%4) == 0)? _dd = 29 : _dd = 28;
                    }
                    else
                    {
                        _dd = 31;
                    }
                    _mm = _mm - 1;
                }
            }
        }
    }
    else if (_offset_h < 0) // Local is slower than UTC
    {
        if (_sod > 86400)
        {
            _sod = _sod - 86400;
            if (_mm == 12)
            {
                if (_dd == 31)
                {
                    _dd = 1;
                    _mm = 1;
                    _yy += 1;
                }
                else
                {
                    _dd += 1;
                }
            }
            else if(_mm == 2)
            {
                if ((_yy%4) == 0)
                {
                    if (_dd > 28)
                    {
                        _mm = 3;
                        _dd = 1;
                    }
                    else
                    {
                        _dd += 1;
                    }
                }
                else
                {
                    if (_dd == 28)
                    {
                        _mm = 3;
                        _dd = 1;
                    }
                    else
                    {
                        _dd += 1;
                    }
                }
            }
            else if ((_mm == 1) || (_mm == 3) || (_mm == 5) || (_mm == 7) || (_mm == 8) || (_mm == 10))
            {
                if (_dd == 31)
                {
                    _dd = 1;
                }
                else
                {
                    _dd += 1;
                }
                _mm += 1;
            }
            else if ((_mm == 4) || (_mm == 6) || (_mm == 9) || (_mm == 11))
            {
                if (_dd == 30)
                {
                    _dd == 1;
                }
                else
                {
                    _dd += 1;
                }
                _mm += 1;
            }
        }
    }
    _hh = _sod/3600;
    _MM = (_sod/60) - (60*_hh);
    _ss = _sod - (3600*_hh + 60*_MM);
    String _utc_fm_str = "";
    (String(_hh).length() < 2)? _utc_fm_str+="0%d" : _utc_fm_str+="%d";
    _utc_fm_str+=":";
    (String(_MM).length() < 2)? _utc_fm_str+="0%d" : _utc_fm_str+="%d";
    _utc_fm_str+=":";
    (String(_ss).length() < 2)? _utc_fm_str+="0%d" : _utc_fm_str+="%d";

    String _date_fm_str = "20"; // The current century
    (String(_yy).length() < 2)? _date_fm_str+="0%d" : _date_fm_str+="%d";
    _date_fm_str+="/";
    (String(_mm).length() < 2)? _date_fm_str+="0%d" : _date_fm_str+="%d";
    _date_fm_str+="/";
    (String(_dd).length() < 2)? _date_fm_str+="0%d" : _date_fm_str+="%d";  

    char _o_date[12]; char _o_utc[10];  memset(&_o_date,0,sizeof(_o_date)); memset(&_o_utc,0,sizeof(_o_utc));

    sprintf(_o_date,_date_fm_str.c_str(),_yy,_mm,_dd);
    sprintf(_o_utc,_utc_fm_str.c_str(),_hh,_MM,_ss);

    _res_rtc_info.date = String(_o_date);
    _res_rtc_info.time = String(_o_utc);
    return _res_rtc_info;
}

void listAllFile()
{
    if (!SPIFFS.begin(true)) {
        SerialMon.println("An Error has occurred while mounting SPIFFS");
        return;
    }
    File folder = SPIFFS.open("/");
    File file = folder.openNextFile();
    while (file) {
        SerialMon.print("\nFILE name : ");
        SerialMon.println(file.name());
        file = folder.openNextFile();
    }
    SerialMon.print("\n\n");
}

bool writelog(String filename, String data2write)
{
    char mode[5];
    String selectedfile;
    SPIFFS.begin();
    // 1. Mount SPIFFS
    if (!SPIFFS.begin()) {
        SerialMon.println("An Error has occurred while mounting SPIFFS");
        return false;
    }
    //   return false;

    // 2. Check SPIFFS's space. If not enough, delete all file except .conf file
    if (int(SPIFFS.totalBytes() - SPIFFS.usedBytes()) < MIN_SPACE) {
        SerialMon.println("SPIFFS does not have enough space.");
        File folder = SPIFFS.open("/");
        File file = folder.openNextFile();
        while (file) {
            SerialMon.println(file.name());
            selectedfile = String(file.name());
            SPIFFS.remove(file.name());
            file = folder.openNextFile();
        }
        ESP.restart();
        return false;
    }
    // listAllFile();
    // 3. Check file exist
    if (!SPIFFS.exists(filename)) {
        sprintf(mode, "w"); // If file is not exist, write i w mode
    }
    else {
        sprintf(mode, "a"); // If this file already exist, let's append file.
    }
    delay(100);
    // SerialMon.println("Mode is " + String(mode));
    SerialMon.print("\n");
    // 4. Open file and write data
    File file = SPIFFS.open(filename, mode);
    file.println(data2write); // Write file and count the written bytes.
    // 5. Close file
    file.close();
    // 6. Unmount SPIFFS
    SPIFFS.end();
    return true;
}

String readlocation()
{
    String _makefilename = "";
    String sentence = sendAT("AT+CLBS=4", 10000, 1);
    if(sentence.indexOf("ERROR") != -1) // If CLBS time unavailable, get from RTC instead
    {
      String YY = ""; String DD = "";
      rtc_info = getRTC();
      SerialMon.println("rtc_info.date is "+rtc_info.date);
      SerialMon.println("rtc_info.time is "+rtc_info.time);
      if ((rtc_info.date == "") || (rtc_info.time == "")) return _makefilename;
      String tmp_date[3]; String tmp_utc[3];
      int slashIndex = 0;
      int lastslashIndex = 0;
      for (int i = 0; i < 3; i++) 
      {
          slashIndex = rtc_info.date.indexOf('/', lastslashIndex);
          String temp = rtc_info.date.substring(lastslashIndex, slashIndex);
          tmp_date[i] = temp;
          lastslashIndex = slashIndex + 1;
      }
      YY = String(tmp_date[0].toInt() - 100*int(tmp_date[0].toInt()/100));
      DD = tmp_date[2];
      _makefilename = "/"+DD+YY+".txt";
      networkinfo.date = rtc_info.date;
      networkinfo.time = rtc_info.time;
    }
    else // If CLBS available, cut only time instead!!
    {
      int startIndex = sentence.indexOf("+CLBS: ");
      sentence = sentence.substring(startIndex + 7, startIndex + 55);
      sentence.replace("\r", "");
      sentence.replace("\n", "");
      sentence.replace("AT+CLBS=4ERROR", "");
      sentence.replace("AT+CPSI?+CPSI: NO SERVICE", "");
      // SerialMon.println("Location = "+sentence);
      int commaIndex = 0;
      int lastCommaIndex = 0;
      String values[6];
      for (int i = 0; i < 6; i++) {
          commaIndex = sentence.indexOf(',', lastCommaIndex);
          String temp = sentence.substring(lastCommaIndex, commaIndex);
          values[i] = temp;
          lastCommaIndex = commaIndex + 1;
          networkinfo.lat = values[1];
          networkinfo.lon = values[2];
      }
      int startIndexdate = sentence.indexOf("2023/");
      networkinfo.date = sentence.substring(startIndexdate, startIndexdate + 10);
      networkinfo.time = sentence.substring(startIndexdate + 11, startIndexdate + 19);
      String name = sentence.substring(startIndexdate + 5, startIndexdate + 7)+sentence.substring(startIndexdate+7, startIndexdate + 10);
      name.replace("/", "");
      name.toLowerCase();
      _makefilename = "/"+name+".txt";
    }
    return _makefilename;
}

void readcellinfo()
{
    String info = sendAT("AT+CPSI?", 10000, 1);
    writelog("/config", info);
    // +CPSI:
    // LTE,Online,520-03,0x332,166401388,241,EUTRAN-BAND3,1450,5,0,17,71,72,2
    // +CPSI:
    //<SystemMode>,<OperationMode>,<MCC>-<MNC>,<TAC>,<SCellID>,<PCellID>,<FrequencyBand>,<earfcn>,<dlbw>,<ulbw>,<RSRQ>,<RSRP>,<RSSI>, < RSSNR>
    int startIndex = info.indexOf("+CPSI: ");
    info = info.substring(startIndex + 7, startIndex + 80);
    info.replace("\r", "");
    info.replace("\n", "");
    info.replace("AT+CLBS=4ERROR", "");
    info.replace("AT+CPSI?+CPSI: NO SERVICE", "");
    // SerialMon.println("readcellinfo = "+info);
    int commaIndex = 0;
    int lastCommaIndex = 0;
    String values[15];
    for (int i = 0; i < 15; i++) {
        commaIndex = info.indexOf(',', lastCommaIndex);
        String temp = info.substring(lastCommaIndex, commaIndex);
        values[i] = temp;
        lastCommaIndex = commaIndex + 1;
    }
    int lacDec = (int)strtol(values[3].c_str(), NULL, 16);
    networkinfo.rssnr = values[13].toInt();
    networkinfo.mcc = values[2].substring(0, 3);
    networkinfo.mnc = values[2].substring(4, 6);
    networkinfo.lac = lacDec;
    networkinfo.cid = values[4];
}

// String makejson()
// {
//     const size_t bufferSize = JSON_OBJECT_SIZE(13) + 100; //JSON_OBJECT_SIZE(6) + JSON_OBJECT_SIZE(4) + 100;
//     DynamicJsonDocument doc(bufferSize);

//     doc["Date"] = networkinfo.date;
//     doc["Time"] = networkinfo.time;
//     doc["Batt_V"] = battinfo.batt_volt;
//     doc["Batt_Lev"] = battinfo.batt_level;
//     doc["Temp"] = tempandhumi.temp;
//     doc["Humi"] = tempandhumi.humi;
//     doc["Lat"] = networkinfo.lat;
//     doc["Lon"] = networkinfo.lon;
//     doc["MCC"] = networkinfo.mcc;
//     doc["MNC"] = networkinfo.mnc;
//     doc["LAC"] = networkinfo.lac;
//     doc["SCellID"] = networkinfo.cid;
//     doc["RSSNR"] = networkinfo.rssnr;

//     String jsonString;
//     serializeJson(doc, jsonString);
//     SerialMon.println(jsonString);
//     return jsonString;
// }

String makeJson()
{
    String _output = "";
    StaticJsonDocument<500> doc;
    doc["Dev_id"] = "froz0001";
    doc["Date"] = "2023/03/08";
    doc["Time"] = "04:11:14";
    doc["Batt_Lev"] = "100.0";

    JsonObject Sensor = doc.createNestedObject("Sensor");

    JsonObject Sensor_A4_C1_38_54_6E_F2 = Sensor.createNestedObject("A4:C1:38:54:6E:F2");
    Sensor_A4_C1_38_54_6E_F2["Temp"] = "25.4";
    Sensor_A4_C1_38_54_6E_F2["Humi"] = "60.0";

    JsonObject Sensor_A4_C1_38_A9_4B_B3 = Sensor.createNestedObject("A4:C1:38:A9:4B:B3");
    Sensor_A4_C1_38_A9_4B_B3["Temp"] = "25.6";
    Sensor_A4_C1_38_A9_4B_B3["Humi"] = "57.0";

    JsonObject Location = doc.createNestedObject("Location");
    Location["Lat"] = "13.751798";
    Location["Lon"] = "100.595377";

    JsonObject Network = doc.createNestedObject("Network");
    Network["MCC"] = "520";
    Network["MNC"] = "03";
    Network["LAC"] = "824";
    Network["SCellID"] = "157455461";
    Network["RSSNR"] = "12";

    serializeJson(doc, _output);
    return _output;
}

void readLog(String filename)
{
    if (!SPIFFS.begin(true)) {
        SerialMon.println("An Error has occurred while mounting SPIFFS");
        return;
    }
    File fileRead = SPIFFS.open(filename, "r");
    SerialMon.println("File Content:");
    while (fileRead.available()) {

        SerialMon.write(fileRead.read());
    }
    fileRead.close();
    if (SPIFFS.begin()) {
        SPIFFS.end();
    }
}

bool deletelog(String filename)
{
    if (!SPIFFS.begin(true)) {
        SerialMon.println("An Error has occurred while mounting SPIFFS");
        return false;
    }
    SerialMon.print(F("Before delete, thre are \r\n"));
    listAllFile();
    if (SPIFFS.exists(filename))
        SPIFFS.remove(filename);
    SerialMon.print(F("After delete, thre are \r\n"));
    listAllFile();
    SPIFFS.end();
    return true;
}

void sleep(int min)
{
    // Set wakeup time to 10 minutes
    esp_sleep_enable_timer_wakeup(min * 60 * 1000000);
    // Go to sleep now
    Serial.println("Going to sleep now");
    esp_deep_sleep_start();
}

void moduleOff()
{
    String _res_shutdown = sendAT("AT+CPOF", 9000, 0);
    digitalWrite(12, LOW);
    Serial.println("_res_shutdown = " + _res_shutdown);
}

void modulePowerOff()
{
    digitalWrite(4, HIGH);
    delay(3000);
    digitalWrite(4, LOW);
    delay(3000);
    digitalWrite(4, HIGH);
    digitalWrite(12, LOW);
}

void sendrequest()
{
    String payload = "{\"token\":\"" + apiKey + "\",\"radio\":\"lte\",\"mcc\":" + networkinfo.mcc + ",\"mnc\":" + networkinfo.mnc + ",\"cells\":[{\"lac\":" + networkinfo.lac + ",\"cid\":" + networkinfo.cid + ",\"psc\":0}],\"address\":1}";
    String response;
    client.connect("ap1.unwiredlabs.com", 80);
    String request = "POST /v2/process.php HTTP/1.1\r\n";
    request += "Host: ap1.unwiredlabs.com\r\n";
    request += "Content-Type: application/x-www-form-urlencoded\r\n";
    request += "Content-Length: ";
    request += String(payload.length());
    request += "\r\n\r\n";
    request += payload;
    client.print(request);
    while (client.connected()) {
        while (client.available()) {
            char c = client.read();
            response += c;
            client.write(c);
        }
    }
    client.stop();
    // Serial.println("Response =" + response);
    int startIndex = response.indexOf("\"lat\":");
    int endIndex = response.indexOf(",\"lon\":");
    String lat = response.substring(startIndex + 6, endIndex);
    startIndex = endIndex + 7;
    endIndex = response.indexOf(",\"accuracy\":");
    String lon = response.substring(startIndex, endIndex);
    networkinfo.lat = lat;
    networkinfo.lon = lon;
    // Serial.println("Latitude: " + lat);
    // Serial.println("Longitude: " + lon);
}

String getConfig(char* _conf_addr, uint16_t _conf_prt, char* _conf_full_fname)
{
    String _config = "No data";
    if (!client.connect(_conf_addr, 80)) 
    {
        SerialMon.println(" fail");
        return _config;
    }
    SerialMon.println(" OK");

    // Make a HTTP GET request:
    client.print(String("GET ") + String(_conf_full_fname) + " HTTP/1.1\r\n");
    client.print(String("Host: ") + String(_conf_addr) + "\r\n\r\n\r\n\r\n");
    client.print("Connection: close\r\n\r\n\r\n\r\n");

    long timeout = millis();
    while (client.available() == 0) {
        if (millis() - timeout > 5000L) {
        SerialMon.println(F(">>> Client Timeout !"));
        client.stop();
        }
    }
    // Grep the content length
    uint32_t contentLength = 0;
    while (client.connected()) 
    {
        String line = client.readStringUntil('\n');
        line.trim();
        // SerialMon.println(line);    // Uncomment this to show response header
        line.toLowerCase();
        if (line.startsWith("content-length:")) 
        {
            contentLength = line.substring(line.lastIndexOf(':') + 1).toInt();
        } 
        else if (line.length() == 0) 
        {
            break;
        }
    }
    while (client.available())
    {
        String _tmp_str = client.readStringUntil('\n');
        if (_tmp_str.startsWith("{"))
        {
            _config = _tmp_str.substring(0,contentLength);
        }
    }
    return _config;
} 

String* getSensorID(char* _dev_id, String _json_str, uint8_t* _sensor_num) // Code from https://arduinojson.org/v6/assistant/#/step4
{
    String* _output;
    StaticJsonDocument<500> doc;
    DeserializationError error = deserializeJson(doc, _json_str);

    if (error) 
    {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
      return _output;
    }
    uint8_t config_index = doc["config"].size();
    for (uint8_t _conf_id = 0; _conf_id < config_index; _conf_id++)
    {
      if (doc["config"][_conf_id].containsKey("dev_id"))
      {
        const char* data = doc["config"][_conf_id]["dev_id"];
        if (strcmp(data,_dev_id) == 0) 
        {
          if (doc["config"][_conf_id].containsKey("target")) 
          {
            uint8_t target_num = doc["config"][_conf_id]["target"].size();
            *_sensor_num = target_num;
            String* _tmp_str = new String[target_num];
            for (uint8_t _tg_id=0; _tg_id<target_num; _tg_id++)
            {
              const char* tgi = doc["config"][_conf_id]["target"][_tg_id];
              _tmp_str[_tg_id] = (String(tgi));
            }
            return _tmp_str;
          }
          break;
        }
      }
    }
}

bool upload2FTP(char* _FTPS_ADDR, char* _FTPS_PRT, char* _FTPS_USRN, char* _FTPS_PASS, char* _FTPS_TYPE, char* _FTPS_LOG_PATH, String _filename)
{
    char ATcommand[60];
    SPIFFS.begin();
    sendAT("AT+FSCD=C:", 5000, 1);

    // 2. Create file in SIM7600 with "fname"
    if (!SPIFFS.begin(true))
    {
        SerialMon.println("An Error has occurred while mounting SPIFFS");
        return false;
    }

    if (!SPIFFS.exists(_filename))
    {
        SerialMon.println(String(_filename) + " is not existed in SPIFFS.");
        return false;
    }

    File fileToRead = SPIFFS.open(_filename, "r");
        if (!fileToRead)
    {
        SerialMon.printf("Failed to open file for reading \r\n");
        return false;
    }

    int file_len = fileToRead.size();
    SerialMon.println("file_len = " + String(file_len) + " bytes.");
    if (file_len <= 0)
    {
        SerialMon.print("No any data in");
        return false;
    } 

    //3. Move file from SPIFFS to drive E: of sim7600
    memset(ATcommand, 0, sizeof(ATcommand));
    sprintf(ATcommand, "AT+CFTRANRX=\"C:%s\",%d", _filename, file_len);
    String writefile_response = sendAT(ATcommand, 5000, 1);
    SerialMon.println(" writefile_response is " +  writefile_response);
    while (fileToRead.available())
    {
        if (SerialAT.availableForWrite())
        {
            SerialAT.printf(fileToRead.readString().c_str());
        }
        delay(100);
    }

    fileToRead.close();
    SerialAT.flush();
    delay(5000);
    SerialMon.printf("Write file done.\r\n");
    SerialAT.flush();
    delay(5000);
    SerialMon.printf("Write file into sim7600 done.\r\n");
    sendAT("AT+FSLS", 1000, 1);
    // 4. Logout and stop all previous FTPS session.
    sendAT("AT+CFTPSLOGOUT", 2000, 1);
    sendAT("AT+CFTPSSTOP", 100, 1);
    delay(5000);

    // 5. Start FTPS session, then login
    sendAT("AT+CFTPSSTART", 500, 1);
    memset(ATcommand, 0, sizeof(ATcommand));
    sprintf(ATcommand, "AT+CFTPSLOGIN=\"%s\",%d,\"%s\",\"%s\",%d", _FTPS_ADDR, _FTPS_PRT, _FTPS_USRN,_FTPS_PASS, _FTPS_TYPE);
    // SerialMon.printf(at_login);
    sendAT(ATcommand, 6000, 1);
    // 6. Upload file
    String filenameno = _filename;
    filenameno.replace("/", "");
    memset(ATcommand, 0, sizeof(ATcommand));
    sprintf(ATcommand, "AT+CFTPSPUTFILE=\"%s\",1", String(_FTPS_LOG_PATH) +String(filenameno));
    String response_uploadfile = sendAT(ATcommand, 20000, 1);
    SerialAT.flush();
    if (response_uploadfile.indexOf("+CFTPSPUTFILE: 0") != -1) {
        Serial.println("File transfer was successful");
    } else {
        Serial.println("File transfer failed");
    }
    // 7. Log out FTPS
    sendAT("AT+CFTPSLOGOUT", 3000, 1);
    // 8. Stop FTPS
    sendAT("AT+CFTPSSTOP", 500, 1);
    // 9. Remove the uploaded file from drive E:
    memset(ATcommand, 0, sizeof(ATcommand));
    sprintf(ATcommand, "AT+FSDEL=%s", _filename);
    sendAT(ATcommand, 2000, 1);
    return true;
}

void logCreate()
{
    // StaticJsonDocument<384> doc;

    // doc["Dev_id"] = "froz0001";
    // doc["Date"] = "2023/03/08";
    // doc["Time"] = "04:11:14";
    // doc["Batt_Lev"] = "100.0";

    // JsonObject Sensor = doc.createNestedObject("Sensor");

    // JsonObject Sensor_A4_C1_38_54_6E_F2 = Sensor.createNestedObject("A4:C1:38:54:6E:F2");
    // Sensor_A4_C1_38_54_6E_F2["Temp"] = "25.4";
    // Sensor_A4_C1_38_54_6E_F2["Humi"] = "60.0";

    // JsonObject Sensor_A4_C1_38_A9_4B_B3 = Sensor.createNestedObject("A4:C1:38:A9:4B:B3");
    // Sensor_A4_C1_38_A9_4B_B3["Temp"] = "25.6";
    // Sensor_A4_C1_38_A9_4B_B3["Humi"] = "57.0";

    // JsonObject Location = doc.createNestedObject("Location");
    // Location["Lat"] = "13.751798";
    // Location["Lon"] = "100.595377";

    // JsonObject Network = doc.createNestedObject("Network");
    // Network["MCC"] = "520";
    // Network["MNC"] = "03";
    // Network["LAC"] = "824";
    // Network["SCellID"] = "157455461";
    // Network["RSSNR"] = "12";

    // serializeJson(doc, output);
}

void setup()
{
    SerialMon.begin(115200);
    SerialAT.begin(UART_BAUD, SERIAL_8N1, PIN_RX, PIN_TX);
    pinMode(BAT_ADC, INPUT);
    delay(1000);
    modemPowerOn();
    delay(2000);
    sendAT("ATE0",1000,1);
    connect2LTE();
    delay(1000);

    uint8_t sensor_num = 0;
    String res_config = getConfig(CONF_ADDR,CONF_PRT,CONF_FULL_FNAME);
    if (res_config != "No data")
    {
        String* mi_list = getSensorID(DEV_ID,res_config, &sensor_num);
        Serial.println("sensor_num is "+String(sensor_num));
        delay(1000);
        BLEDevice::init("");
        for (uint8_t mi_i=0; mi_i < sensor_num; mi_i++)
        {
            connectToSensor(BLEAddress(mi_list[mi_i].c_str()));
            Serial.println("sensor mac = "+mi_list[mi_i]+", temperature = "+tempandhumi.temp+" C., humidity = "+tempandhumi.humi+"%.");
        }
    }
    // String* mi_list = getSensorID(DEV_ID,, &sensor_num);
    // delay(1000);

    // BLEDevice::init("");

    // // for (uint8_t mi_i=0; mi_i < sizeof(mijia_list)/sizeof(mijia_list[0]); mi_i++)
    // // {
    // //     connectToSensor(BLEAddress(mijia_list[mi_i].c_str()));
    // //     Serial.println("sensor mac = "+mijia_list[mi_i]+", temperature = "+tempandhumi.temp+" C., humidity = "+tempandhumi.humi+"%.");
    // // }

    // for (uint8_t mi_i=0; mi_i < sensor_num; mi_i++)
    // {
    //     connectToSensor(BLEAddress(mi_list[mi_i].c_str()));
    //     Serial.println("sensor mac = "+mi_list[mi_i]+", temperature = "+tempandhumi.temp+" C., humidity = "+tempandhumi.humi+"%.");
    // }


    // pClient = BLEDevice::createClient();

    // connectToSensor(BLEAddress(mijia_list[mi_i]));
    
    delay(5000);
    readBattlevel();
    delay(500);
    readlocation();
    delay(2000);
    readcellinfo();
    delay(2000);
    // pClient->disconnect();

    // delay(3000);
    sendrequest();
    // writelog(makefilename, makejson());
    // delay(1000);
    // // readLog(makefilename);
    // // delay(2000);
    // upload2FTP(makefilename);
    // delay(2000);

    // modulePowerOff();
    // sleep(30);
    // updateTarget(char* _FTPS_ADDR,  _FTPS_PRT, char* _FTPS_USRN, char* _FTPS_PASS, char* _FTPS_TYPE, char* _FTPS_CONF_PATH, char* _filename)
    // String res_download = updateTarget(FTPS_ADDR,FTPS_PRT,FTPS_USRN,FTPS_PASS,FTPS_TYPE,FTPS_CONF_PATH,CONF_FNAME);
    // Serial.println("res_download = "+res_download);
    // String sensor_id = getSensorID(config);
    // String config = getConfig(CONF_ADDR, CONF_PRT, CONF_FULL_FNAME);
    // Serial.println("config = " + config);
    // delay(1000);
}

void loop()
{
  while (true) {
    if (SerialAT.available()) {
      Serial.write(SerialAT.read());
    }
    if (Serial.available()) {
      SerialAT.write(Serial.read());
    }
    delay(1);
  }
//   vTaskDelay(100 / portTICK_PERIOD_MS);
}