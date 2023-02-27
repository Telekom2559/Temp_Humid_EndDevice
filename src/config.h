// This file contains with basic parameters to track temperature & humidity
// There are MAC@, service UUID, char UUID, sleep cycle, minimum free SPIFFs, mqtt accession
#include <Arduino.h>

char* mac_sensor[] = {"A4:C1:38:54:6E:F2"};
String apiKey = "pk.71031a62fba9814c0898ae766b971df1";
char* serv_uuid = "ebe0ccb0-7a0a-4b0c-8a1a-6ff2997da3a6";
char* char_uuid = "ebe0ccc1-7a0a-4b0c-8a1a-6ff2997da3a6";
double full_v_batt = 4.12;
double low_v_batt = 2.89;

