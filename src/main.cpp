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

#define MJ_ADDR "A4:C1:38:54:6E:F2"
#define apiKey (String) "pk.71031a62fba9814c0898ae766b971df1"

#define FTPS_ADDR               "188.166.217.51"
#define FTPS_PRT                7021
#define FTPS_PATH               "/BER/"
#define FTPS_USRN               "tung"
#define FTPS_PASS               "anundaJJ795"
#define FTPS_TYPE               1
String makefilename;
String filenameno;

TinyGsmSim7600 modem(SerialAT);
TinyGsmSim7600::GsmClientSim7600 client(modem);
BLEClient* pClient;

struct SENSOR_DATA {
    String temp; // Use 3
    String humi; // Use 4
};

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
    float batt_volt;
    String batt_level;
};

BATT_INFO battinfo;

// The remote service we wish to connect to.
static BLEUUID serviceUUID("ebe0ccb0-7a0a-4b0c-8a1a-6ff2997da3a6");

// The characteristic of the remote service we are interested in.
static BLEUUID charUUID("ebe0ccc1-7a0a-4b0c-8a1a-6ff2997da3a6");

void decrypted(BLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify)
{
    // Serial.printf("pData[0] = %x ",pData[0]);
    // Serial.printf("pData[1] = %x ",pData[1]);
    // Serial.println();
    int16_t tmp_data = (pData[0] | (pData[1] << 8));
    tempandhumi.temp = ((float)tmp_data*0.01);
    tempandhumi.humi = pData[2];
}

void connectToSensor(BLEAddress pAddress)
{
    pClient->connect(pAddress);
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
    pRemoteCharacteristic->registerForNotify(decrypted);
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
    listAllFile();
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
    String sentence = sendAT("AT+CLBS=4", 10000, 1);
    writelog("/config", sentence);
    //  +CLBS: 0,13.620110,100.662918,550,2023/01/30,13:39:11
    //  response :<locationcode>,<longitude>,<latitude>,<acc>,<date>,<time>
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
    SerialMon.println("name = "+name);
    makefilename = "/"+name+".txt";
    SerialMon.println("file name  = "+makefilename);
    return makefilename;
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

String makejson()
{
    const size_t bufferSize = JSON_OBJECT_SIZE(6) + JSON_OBJECT_SIZE(4) + 100;
    DynamicJsonDocument doc(bufferSize);

    doc["Date"] = networkinfo.date;
    doc["Time"] = networkinfo.time;
    doc["Batt_Lev"] = battinfo.batt_level;
    doc["Temp"] = tempandhumi.temp;
    doc["Humi"] = tempandhumi.humi;
    doc["Lat"] = networkinfo.lat;
    doc["Lon"] = networkinfo.lon;
    // doc["MCC"] = networkinfo.mcc;
    // doc["MNC"] = networkinfo.mnc;
    // doc["LAC"] = networkinfo.lac;
    // doc["SCellID"] = networkinfo.cid;
    // doc["RSSNR"] = networkinfo.rssnr;

    String jsonString;
    serializeJson(doc, jsonString);
    SerialMon.println(jsonString);
    return jsonString;
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

    // // Go to sleep now
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
    Serial.println("Response =" + response);
    int startIndex = response.indexOf("\"lat\":");
    int endIndex = response.indexOf(",\"lon\":");
    String lat = response.substring(startIndex + 6, endIndex);
    startIndex = endIndex + 7;
    endIndex = response.indexOf(",\"accuracy\":");
    String lon = response.substring(startIndex, endIndex);
    networkinfo.lat = lat;
    networkinfo.lon = lon;
    Serial.println("Latitude: " + lat);
    Serial.println("Longitude: " + lon);
}

bool upload2FTP(String filename ,bool flag)
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

    if (!SPIFFS.exists(filename))
    {
        SerialMon.println(String(filename) + " is not existed in SPIFFS.");
        return false;
    }

    File fileToRead = SPIFFS.open(filename, "r");
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
    sprintf(ATcommand, "AT+CFTRANRX=\"C:%s\",%d", filename, file_len);
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
    sprintf(ATcommand, "AT+CFTPSLOGIN=\"%s\",%d,\"%s\",\"%s\",%d", FTPS_ADDR, FTPS_PRT, FTPS_USRN,FTPS_PASS, FTPS_TYPE);
    // SerialMon.printf(at_login);
    sendAT(ATcommand, 6000, 1);
    // 6. Upload file
    filenameno = filename;
    filenameno.replace("/", "");
    memset(ATcommand, 0, sizeof(ATcommand));
    sprintf(ATcommand, "AT+CFTPSPUTFILE=\"%s\",1", String(FTPS_PATH) +String(filenameno));
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
    sprintf(ATcommand, "AT+FSDEL=%s", filename);
    sendAT(ATcommand, 2000, 1);
    return true;
}


void setup()
{
    SerialMon.begin(115200);
    SerialAT.begin(UART_BAUD, SERIAL_8N1, PIN_RX, PIN_TX);
    pinMode(BAT_ADC, INPUT);
    delay(1000);

    // deletelog(makefilename);
    // deletelog("/config");

    modemPowerOn();
    delay(1000);
    // connect2LTE();
    // delay(1000);
    // BLEDevice::init("");
    // pClient = BLEDevice::createClient();
    // connectToSensor(BLEAddress(MJ_ADDR));
    // delay(5000);

    // readBattlevel();
    // delay(500);
    // readlocation();
    // delay(2000);
    // readcellinfo();
    // delay(2000);

    // pClient->disconnect();
    // delay(3000);

    // sendrequest();

    // writelog(makefilename, makejson());
    // delay(1000);
    // readLog(makefilename);
    // delay(2000);

    // upload2FTP(makefilename,1);
    // delay(2000);

    // modulePowerOff();
    // sleep(15);
}

void loop()
{
  vTaskDelay(100 / portTICK_PERIOD_MS);
}