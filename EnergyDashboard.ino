/*
// *************************************************************************************
  V2.0  First publish
  Toont de dynamische stroomtarieven en de gasprijs van diverse aanbieders op een tft-schermpje. Bron: Enever.nl.

  Code geschreven op 23 juni 2023 door Martijn Overman voor Reshift Digital https://reshift.nl/ 
  Deze software mag worden verspreid onder de bepalingen van de GNU General Public License https://www.gnu.org/
  Code op 15 oktober 2023 aangepast door PA3CNO voor gebruik met het PI4RAZ ESP32 board met 2.8" scherm
  Code op 22 mei 2024 aangepast door PA2RDK ivm settings menu en OTA
  *************************************************************************************
*/

// Board in Arduino IDE: ESP32 Dev Module //
// LET OP! Aanpassingen in User_Setup.h in TFT_eSPI //
#include <WiFiClientSecure.h>
#include "EEPROM.h"
#include "ArduinoJson.h"
#include "time.h"
#include <TFT_eSPI.h>
#include <SPI.h>
#include <RDKOTA.h>

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite sprite= TFT_eSprite(&tft);

#define offsetEEPROM  0x10
#define EEPROM_SIZE   250

#define cyaan 0x0FF0
#define zwart 0x0000
#define rood 0xF000
#define groen 0x0F00
#define geel 0xFF00
#define wit 0xFFF0

#define OTAHOST      "https://www.rjdekok.nl/Updates/EnergyDashboard"
#define VERSION      "v2.0"

RDKOTA rdkOTA(OTAHOST);

struct StoreStruct {
  byte chkDigit;
  char ssid[25];
  char password[27];
  char apikey[40];      // Ga voor een API-sleutel naar https://enever.nl/prijzenfeeds/
  char telefoon[15];    // Gebruik de landcode en laat de eerste 0 van je abonneenummer weg
  char apikey_cmb[10];  // Ga voor een API-sleutel naar https://www.callmebot.com/
  char aanbieder[10];   // Vul hier een aanbieder in uit de onderstaande lijst
  bool WhatsApp;        // Vul hier 'true' in als je een appje wilt ontvangen en bovenstaande gegevens hebt ingevuld, anders 'false' laten staan
};

// #include "RDK_Settings.h"
 #include "All_Settings.h"

char receivedString[128];
char chkGS[3] = "GS";

/*
prijsAA   =  Atoom Alliantie
prijsAIP  =  All in power
prijsANWB =  ANWB Energie
prijsEE   =  EasyEnergy
prijsEVO  =  Energie VanOns
prijsEZ   =  Energy Zero
prijsFR   =  Frank Energie
prijsGSL  =  Groenestroom Lokaal
prijsMDE  =  Mijndomein Energie
prijsNE   =  NextEnergy
prijsTI   =  Tibber
prijsVDB  =  Vandebron
prijsVON  =  Vrij op naam
prijsWE   =  Wout Energie
prijsZG   =  ZonderGas
prijsZP   =  Zonneplan

Zie voor ontbrekende aanbieders de leganda op https://enever.nl/prijzenfeeds/
*/

// Deze waarden kun je laten staan
const char* host_cmb = "api.callmebot.com";
const String url_cmb = "/whatsapp.php?phone=" + String(storage.telefoon) + "&apikey=" + String(storage.apikey_cmb) + "&text=Stroomprijs+nu:+";
bool berichtverzonden = false;
uint32_t color = 0;
bool triangle = false;
bool toonVandaag = false;
bool toonMorgen = false;
bool hideGraph = false;
const char* host = "enever.nl";
const int httpsPort = 443;
String url_v = "/api/stroomprijs_vandaag.php?token=" + String(storage.apikey);
String url_m = "/api/stroomprijs_morgen.php?token=" + String(storage.apikey);
String url_g = "/api/gasprijs_vandaag.php?token=" + String(storage.apikey);
const char* ntpserver = "pool.ntp.org";
const unsigned long interval = 300000; // 5 minuten - tijd tussen twee pogingen om data op te halen
const unsigned long interval_kort = 60000; // 1 minuut - voor het verversen van het scherm
const int max_g = 100; // Hoogte grafiek in pixels, aangepast aan 2.8" scherm
unsigned long previousMillis_blink = 0;
unsigned long previousMillis_kort = 0;
unsigned long previousMillis_lang = 0;
unsigned long previousMillis = 0;
unsigned long previousMillis_v = 0;
unsigned long previousMillis_m = 0;
bool firstrun = true;
bool firstrun_v = true;
bool firstrun_m = true;
bool firstrun_g = true;
bool vandaagbinnen = false;
bool morgenbinnen = false;
bool gasbinnen = false;
bool vandaag = false;
bool morgen = false;
bool vrijgave_v = true;
bool vrijgave_m = true;
bool vrijgave_k = true;
bool vrijgave_t = false;
bool vrijgave_r = true;
bool datumKlopt = false;
bool knopActive = false;
bool vrijgave_g = true;
bool laagstePrijs = false;
bool negprijs = false;
String line = "";
float bigArr[25]; // Tijdelijk waarden opslaan i.v.m. zomer- en wintertijd
float arr_v[24];
float arr_m[24];
int arr_cv[24]; // Array met grafiekwaarden in cent vandaag
int arr_cm[24]; // Array met grafiekwaarden in cent morgen
float schaalStap = 0.0;
float prijsnu = 0.0;
float prijsgas = 0.0;
float gasprijs = 0.0;
int Dag = 0;
int Uur = 0;
int Minuut = 0;
int max_Val = 0;
int min_Val = 0;
int Prijs_nu = 0;
String u = "";
String u_min = "";
String u_max = "";
String m = "";
const uint8_t led_groen = 25; // Moet nog aangepast aan te gebruiken I/O - PA3CNO
const uint8_t led_rood = 26; // Moet nog aangepast aan te gebruiken I/O - PA3CNO
const int knop = 33; // Moet nog aangepast aan te gebruiken I/O - PA3CNO
int Y_min = 0;
const int pwmChannel = 0;
const int frequence = 1000;
const int resolution = 8;
const int pwmPin = 14;

void setTimezone(String timezone){
  Serial.printf("  Setting Timezone to %s\n",timezone.c_str());
  setenv("TZ",timezone.c_str(),1); // Now adjust the TZ.  Clock settings are adjusted to show the new local time
  tzset();
}

bool initTime(String timezone){
  int i = 0;
  struct tm timeinfo;
  Serial.println("Setting up time");
  configTime(0, 0, ntpserver); // First connect to NTP server, with 0 TZ offset
  while(!getLocalTime(&timeinfo)){
    delay(500);
    Serial.print(".");
    i++;
    if (i >= 24) {
      return false;
    }
  }
  Serial.println("  Got the time from NTP");
  // Now we can set the real timezone
  setTimezone(timezone);
  return true;
}

void printLocalTime(){
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return;
  }
  Dag = timeinfo.tm_mday;
  Uur = timeinfo.tm_hour;
  Minuut = timeinfo.tm_min;
}

bool wifi() {
  if(WiFi.status() == WL_CONNECTED){
    return true;
  }
  else {
    int i = 0;
    Serial.println();
    Serial.print("connecting to ");
    Serial.println(storage.ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(storage.ssid, storage.password);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
      i++;
      if (i >= 240) {
        return false;
      }
    }
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    return true;
  }
}

void sendMessage(String msg) {
  String url_msg = url_cmb + msg + "+cent";
  WiFiClientSecure client;
  client.setInsecure();
  Serial.print("connecting to ");
  Serial.println(host_cmb);
  if (!client.connect(host_cmb, httpsPort)) {
    Serial.println("connection failed");
    return;
  }
  Serial.print("requesting URL: ");
  Serial.println(url_msg);
  client.print(String("GET ") + url_msg + " HTTP/1.1\r\n" +
               "Host: " + host_cmb + "\r\n" +
               "User-Agent: ComputerTotaal_ESP32\r\n" +
               "Connection: close\r\n\r\n");
  Serial.println("request sent");
  while (client.connected()) {
    String line_cmb = client.readStringUntil('\n');
    if (line_cmb == "\r") {
      Serial.println("headers received");
      break;
    }
  }
  String line_cmb = client.readString();
  Serial.println("reply was:");
  Serial.println("==========");
  Serial.println(line_cmb);
  Serial.println("==========");
  Serial.println("closing connection");
  client.stop();
  berichtverzonden = true;
}

bool getData(String url, float arr[24], bool nu) {
  WiFiClientSecure client;
  client.setInsecure();
  Serial.print("connecting to ");
  Serial.println(host);
  if (!client.connect(host, httpsPort)) {
    Serial.println("connection failed");
    return false;
  }
  Serial.print("requesting URL: ");
  Serial.println(url);
  client.print(String("GET ") + url + " HTTP/1.0\r\n" +
               "Host: " + host + "\r\n" +
               "User-Agent: ComputerTotaal_ESP32\r\n" +
               "Connection: close\r\n\r\n");
  Serial.println("request sent");
  while (client.connected()) {
    line = client.readStringUntil('\n');
    if (line == "\r") {
      Serial.println("headers received");
      break;
    }
  }
  line = client.readStringUntil('{');
  line = client.readString();
  // Serial.println("reply was:");
  // Serial.println("==========");
  // Serial.println(line);
  // Serial.println("=========="); 
  Serial.println("closing connection");
  client.stop();
  if (line == "" || line.indexOf("[]") > 0 || line.indexOf(storage.aanbieder) < 0) {
    return false;
  }
  else {
    line = "{" + line;
    DynamicJsonDocument doc(line.length()*2);
    DeserializationError error = deserializeJson(doc, line);
    if (error) {
      Serial.print("deserializeJson() failed: ");
      Serial.println(error.c_str());
      return false;
    }

  const char* Dag_j = doc["data"][0]["datum"];
  int Dag_m = (10*(Dag_j[8] - '0')) + Dag_j[9] - '0';
  if(nu && Dag == Dag_m){
    datumKlopt = true;
    Serial.println("Datum klopt!");
  }
  else if(nu){
    datumKlopt = false;
    Serial.println("Datum klopt niet!");
  }
  int iter = 0;
  memset(bigArr, 0, sizeof(bigArr)); // Leeg tijdelijk array
  for (JsonObject data_item : doc["data"].as<JsonArray>()) {
    // const char* data_item_datum = data_item["datum"];
    // const char* data_item_prijs = data_item["prijs"];
    const char* uurprijs = data_item[storage.aanbieder];
    float myFloat = atof(uurprijs);
    // Waarden opslaan in groter array, van daaruit kopiëren
    bigArr[iter] = myFloat;
    iter++;
  }
  if (bigArr[23] == 0) {
    Serial.println("Naar zomertijd");
    arr[0] = bigArr[0];
    arr[1] = bigArr[1];
    for (int U = 2; U <= 23; U++) {
      arr[U] = bigArr[U - 1]; // Kopieert de waarde van 01.00 uur naar 02.00 uur om array te vullen
    }
  }
  else if (bigArr[24] !=0) {
    Serial.println("Naar wintertijd");
    arr[0] = bigArr[0];
    arr[1] = bigArr[1];
    for (int U = 2; U <= 23; U++) {
      arr[U] = bigArr[U + 1]; // Gebruikt alleen de waarde van de tweede keer dat het 02.00 uur is
    }
  }
  else {
    Serial.println("Standaardsituatie");
    for (int U = 0; U <= 23; U++) {
      arr[U] = bigArr[U];
    }
  }
  // const char* endcode = doc["code"];
  return true;
  }
}

void minmax(float arr[24], int arr_c[24], bool nu) { 
  int j = 0;
  int k = 0;
  float maxVal = arr[0];
  float minVal = arr[0];
  for (int i = 0; i <= 23; i++) {
    if (arr[i] > maxVal) {
      maxVal = arr[i];
      j = i;
    }
    if (arr[i] < minVal) {
      minVal = arr[i];
      k = i;
    }
  }
  if(j < 10){u_max = "0";}else {u_max = "";}
  if(k < 10){u_min = "0";}else {u_min = "";}
  prijsnu = arr[Uur];
  Serial.println("");
  if(nu) {
    toonVandaag = true;
    toonMorgen = false;
    Serial.print("Huidige stroomprijs: "); Serial.println(prijsnu);
	/* Haal deze regel weg om onderstaande code uit te voeren.
	if(prijsnu < 0 && !negprijs) {
		negprijs = true;
		Serial.println("Negatieve stroomprijs!");
		// Voer hier de gewenste actie(s) uit bij een negatieve stroomprijs.
	}
	else if(prijsnu >= 0 && negprijs) {
		negprijs = false;
	}
	Haal deze regel weg om bovenstaande code uit te voeren. */
  }
  else {
    toonVandaag = false;
    toonMorgen = true;
  }
  Serial.print("Minimumprijs "); Serial.print(minVal); Serial.print(" Uur: "); Serial.print(u_min); Serial.print(k);Serial.println(".00");
  Serial.print("Maximumprijs "); Serial.print(maxVal); Serial.print(" Uur: "); Serial.print(u_max); Serial.print(j);Serial.println(".00");
  Prijs_nu = round(prijsnu * 100.0);
  min_Val = round(minVal * 100.0);
  max_Val = round(maxVal * 100.0);
  schaalStap = (max_Val*1.0 - min_Val*1.0) / 5; // Variabele alleen gebruiken als er negatieve bedragen in array staan
  if(min_Val < 0) {
    Y_min = min_Val;
  }
  else {
    Y_min = 0;
  }
  for (int i = 0; i <= 23; i++) {
    int j = round(arr[i] * 100);
    arr_c[i] = map(j, Y_min, max_Val, 0, max_g); // Vul array met waarden tussen 0 of negatief bedrag en maximale grafiekhoogte.
  }
  if(min_Val == max_Val && nu) {
    Serial.println("Leds uit wegens gelijke extremen");
    digitalWrite(led_groen, LOW);
    digitalWrite(led_rood, LOW);
    laagstePrijs = false;
    berichtverzonden = false;
  }
  else if(min_Val == Prijs_nu && nu) {
    Serial.println("Groen aan");
    digitalWrite(led_groen, HIGH); // Groene led aan tijdens minimum
    digitalWrite(led_rood, LOW);
    laagstePrijs = true;
  }
  else if(max_Val == Prijs_nu && nu) {
    Serial.println("Rood aan");
    digitalWrite(led_groen, LOW);
    digitalWrite(led_rood, HIGH); // Rode led aan tijdens maximum
    laagstePrijs = false;
    berichtverzonden = false;
  }
  else if(nu) {
    Serial.println("Leds uit");
    digitalWrite(led_groen, LOW);
    digitalWrite(led_rood, LOW);
    laagstePrijs = false;
    berichtverzonden = false;
  }
}

bool getData_gas(String url) {
  WiFiClientSecure client;
  client.setInsecure();
  Serial.print("connecting to ");
  Serial.println(host);
  if (!client.connect(host, httpsPort)) {
    Serial.println("connection failed");
    return false;
  }
  Serial.print("requesting URL: ");
  Serial.println(url);
  client.print(String("GET ") + url + " HTTP/1.0\r\n" +
               "Host: " + host + "\r\n" +
               "User-Agent: ComputerTotaal_ESP32\r\n" +
               "Connection: close\r\n\r\n");
  Serial.println("request sent");
  while (client.connected()) {
    line = client.readStringUntil('\n');
    if (line == "\r") {
      Serial.println("headers received");
      break;
    }
  }
  line = client.readStringUntil('{');
  line = client.readString();
  /*  Serial.println("reply was:");
  Serial.println("==========");
  Serial.println(line);
  Serial.println("=========="); */
  Serial.println("closing connection");
  client.stop();
  if (line == "" || line.indexOf("[]") > 0 || line.indexOf(storage.aanbieder) < 0) {
    return false;
  }
  else {
    line = "{" + line;
    DynamicJsonDocument doc(line.length()*2);
    DeserializationError error = deserializeJson(doc, line);
    if (error) {
      Serial.print("deserializeJson() failed: ");
      Serial.println(error.c_str());
      return false;
    }
  // const char* status = doc["status"]; // "true"
  const char* Dag_j = doc["data"][0]["datum"];
  int Dag_m = (10*(Dag_j[8] - '0')) + Dag_j[9] - '0';
  if(Dag == Dag_m){
    Serial.println("Datum klopt!");
  }
  else {
    Serial.println("Datum klopt niet!");
  return false;
  }
  for (JsonObject data_item : doc["data"].as<JsonArray>()) {
    // const char* data_item_datum = data_item["datum"];
    const char* prijs = data_item[storage.aanbieder];
    prijsgas = atof(prijs);
    gasprijs = prijsgas,2;
    Serial.print("Gasprijs: ");
    Serial.println(gasprijs);
  }
  // const char* endcode = doc["code"];
  return true;
  }
}

void setup() {
  tft.init();
  tft.setRotation(3);
  sprite.setColorDepth(8); // Otherwise short of memory - TNX Robert PA2RDK!
  sprite.createSprite(320,240);
  sprite.fillRect(0,0,320,240,zwart); // Overschrijf alles met zwart
  sprite.pushSprite(0,0);
  pinMode(pwmPin, OUTPUT); // Helderheid scherm
  pinMode(knop, INPUT_PULLUP); // Schakelaar
  pinMode(led_groen, OUTPUT); // Groene led
  pinMode(led_rood, OUTPUT); // Rode led
  digitalWrite(led_groen, LOW);
  digitalWrite(led_rood, LOW);
  Serial.begin(115200);

  if (!EEPROM.begin(EEPROM_SIZE)) {
    tft.fillScreen(TFT_BLACK);
    tft.setCursor(0, 0);
    tft.println(F("failed to initialise EEPROM"));
    Serial.println(F("failed to initialise EEPROM"));
    while (1)
      ;
  }
  if (EEPROM.read(offsetEEPROM) != storage.chkDigit) {
    Serial.println(F("Writing defaults...."));
    saveConfig();
  }

  loadConfig();
  printConfig();

  Serial.println(F("Type GS to enter setup:"));
  tft.println(F("Wait for setup"));
  delay(5000);
  if (Serial.available()) {
    Serial.println(F("Check for setup"));
    if (Serial.find(chkGS)) {
      tft.println(F("Setup entered"));
      Serial.println(F("Setup entered..."));
      setSettings(1);
    }
  }

  tft.fillScreen(TFT_BLACK);
  tft.setCursor(0, 0);
  tft.println(F("Wait for WiFi"));

  if(wifi() && initTime("CET-1CEST,M3.5.0,M10.5.0/3")) { // Set for Amsterdam/NL
    Serial.println("Tijdzone ingesteld");
  }
  else {
    ESP.restart(); // Je moet toch wat
  }

  if (rdkOTA.checkForUpdate(VERSION)){
    if (questionBox("Installeer update", TFT_WHITE, TFT_NAVY, 5, 100, 310, 48)){
      messageBox("Installing update", TFT_YELLOW, TFT_NAVY, 5, 100, 310, 48);
      rdkOTA.installUpdate();
    } 
  }

  tft.fillScreen(TFT_BLACK);
  tft.setCursor(0, 0);
  tft.println(F("Wait for Data"));
  ledcSetup(pwmChannel, frequence, resolution);
  ledcAttachPin(pwmPin, pwmChannel);
}

void drawGraph(int arr[24], bool nu) {
  sprite.fillRect(0,0,320,240,zwart); // Overschrijf alles met zwart
  for(int i=0;i<=10;i=i+2){
    for(int j=0;j<300;j=j+12){
      sprite.drawPixel(20+j,227-2*((i*10)+10),cyaan); // Teken de stippellijnen
    }
  }
  sprite.setTextColor(geel,zwart); 
  for(int i=0;i<=23;i++){
    if(arr[i] == map(max_Val, Y_min, max_Val, 0, max_g)){ // Hoogste grafiekwaarde
      color=rood;
    }
    else if(arr[i] == map(min_Val, Y_min, max_Val, 0, max_g)){ // Laagste grafiekwaarde
      color=groen;
    }
    else{
      color=geel;
    }
    sprite.fillRect(22+2*(i*6),2*(104-arr[i]),8,2*arr[i],color); // Teken de grafiek
  }
  sprite.setTextColor(cyaan,zwart);
  for(int i=0;i<=10;i=i+2){
    if(min_Val < 0) {
      int j=round(min_Val+((i/2)*schaalStap));
      sprite.drawString(String(j),0,2*(100-(i*10)+2)); // Teken de schaal op de Y-as
    }
    else {
      int j=round(max_Val*i*10.0/100); // Vermenigvuldiging met 10.0 is nodig voor juiste afronding
      sprite.drawString(String(j),0,2*(100-(i*10)+2)); // Teken de schaal op de Y-as
    }
  }
  for(int i=0;i<=9;i=i+3){
    sprite.drawString(String(i),2*(12+(i*6)),225,1); // Schrijf de uren onder de 10
  }
  for(int i=12;i<=18;i=i+3){
    sprite.drawString(String(i),2*(10+(i*6)),225,1); // Schrijf de uren boven de 10
  }
  sprite.drawString(String(21),273,225,1); // Schrijf uur 21 (apart wegens breder)
  sprite.setTextColor(wit,zwart);
  if(toonVandaag){ // Toont huidige stroomprijs, grafiek morgen of info
    hideGraph = false;
    sprite.drawString("Stroom: ",44,0,1);
    sprite.drawString(String(prijsnu),130,0,1);
  }
  else if(toonMorgen){
    hideGraph = false;
    sprite.drawString("Stroomprijzen morgen",44,0,1);
  }
  else{
    hideGraph = true;
    sprite.fillRect(0,0,320,240,zwart); // Overschrijf alles met zwart
    sprite.drawString("Wacht op nieuwe data.",44,0,1);
  }
  if(gasbinnen && nu && !hideGraph){
    sprite.drawString("Gas: ",204,0,1); 
    sprite.drawString(String(gasprijs),254,0,1);
  }
  sprite.pushSprite(0,0);
}

void markUur(){
  if(triangle || toonMorgen || hideGraph){ // Verbergt pijltje onder grafiek morgen of laat het knipperen onder grafiek vandaag
    sprite.fillRect(34, 210, 286, 9, zwart);
    sprite.pushSprite(0,0);
    triangle = false;
  }
  else{ // Wijst huidige uur aan, verschuift per uur zes pixels op de X-as
    sprite.fillTriangle(2*(17+Uur*6)-12, 218, 2*(19+Uur*6)-12, 210, 2*(21+Uur*6)-12, 218, cyaan);
    sprite.pushSprite(0,0);
    triangle = true;
  }
}

void loop() {
  uint16_t x = 0, y = 0;
  if (wifi()) {
    printLocalTime();
    if(Uur < 10){u = "0";}else {u = "";}
    if(Minuut < 10) {m = "0";}else {m = "";}
  }
  if (Uur == 0 && Minuut == 0 && vrijgave_r) { // Voorkomt dat oude data worden gebruikt
    vrijgave_r = false; // Voorkomt een lus omdat toestand een minuut duurt
    digitalWrite(led_groen, LOW);
    digitalWrite(led_rood, LOW);
    memset(arr_v, 0, sizeof(arr_v)); // Wist oude data van vandaag
    memset(arr_cv, 0, sizeof(arr_cv)); 
    memset(arr_m, 0, sizeof(arr_m)); // Wist oude data van morgen
    memset(arr_cm, 0, sizeof(arr_cm)); 
    min_Val = 0;
    max_Val = 0;
    Prijs_nu = 0;
    prijsnu = 0.0;
    vandaagbinnen = false;
    morgenbinnen = false;
    toonVandaag = false;
    toonMorgen = false;
    vrijgave_t = true;
    vandaag = false;
    morgen = false;
    vrijgave_v = true;
    vrijgave_m = true;
    berichtverzonden = false;
    laagstePrijs = false;
  }
  if (Uur == 0 && Minuut == 1 && vrijgave_v) { // Haal data van vandaag binnen
    vrijgave_v = false; // Voorkomt een lus omdat toestand een minuut duurt
    vrijgave_r = true; // Lusbescherming middernacht opgeheven
    vandaag = true; // Data van vandaag kunnen worden opgehaald
  }
  if (Uur == 16 && Minuut == 1 && vrijgave_m) { // Haal data van morgen binnen
    vrijgave_m = false; // Voorkomt een lus omdat toestand een minuut duurt
    morgen = true; // Data van morgen kunnen worden opgehaald
  }
  if (Uur == 7 && Minuut == 0) {
    gasbinnen = false;
  }
  if ((firstrun_g || ((Uur == 7 || Uur == 8 || Uur == 9) && Minuut == 1)) && !gasbinnen && vrijgave_g && wifi()) { // Haal gasprijs van vandaag op
    firstrun_g = false;
    vrijgave_g = false; // Voorkomt lus omdat toestand een minuut duurt
    if (getData_gas(url_g)) {
      gasbinnen = true;
    }
    else {
      gasbinnen = false;
      Serial.println("Ophalen gasprijs niet gelukt");
    }
  }
  if(Minuut != 1 && !vrijgave_g){
    vrijgave_g = true;
  }
  unsigned long currentMillis = millis();
  if (((currentMillis - previousMillis_v >= interval && !vandaagbinnen && vandaag) || firstrun_v) && wifi()) {
    firstrun_v = false;
    if (getData(url_v, arr_v, true) && datumKlopt) {
      vandaagbinnen = true;
      Serial.print("Vandaag: ");
      for(int i=0; i<=23; i++) { // Print de inhoud van array met prijzen van vandaag.
        Serial.print(arr_v[i], 6);
        Serial.print(",");
      }
    Serial.println("");
    }
    previousMillis_v = currentMillis;
  }
  if (((currentMillis - previousMillis_m >= interval && !morgenbinnen && morgen) || firstrun_m) && wifi()) {
    firstrun_m = false;
    if (getData(url_m, arr_m, false)) {
      morgenbinnen = true;
      Serial.print("Morgen: ");
      for(int i=0; i<=23; i++) { // Print de inhoud van array met prijzen van morgen.
        Serial.print(arr_m[i], 6);
        Serial.print(",");
      }
    Serial.println("");
    }
    previousMillis_m = currentMillis;
  }
  if (currentMillis - previousMillis_blink >= 500){
    markUur();
    previousMillis_blink = currentMillis;
  }
  bool pressed = tft.getTouch(&x, &y);
  if(currentMillis - previousMillis >= interval_kort || firstrun || ((!digitalRead(knop) || pressed) && vrijgave_k) || vrijgave_t) {
    firstrun = false;
    vrijgave_t = false;
    if (!digitalRead(knop) || pressed) { // Knop wordt ingedrukt
      vrijgave_k = false; // Lusbescherming knop
      knopActive = true;
      previousMillis = currentMillis; // Houd dezelfde toestand gedurende de intervaltijd
      Serial.println("Knop ingedrukt");
    }
    else { // Knop wordt niet ingedrukt
      knopActive = false;
      vrijgave_k = true; // Lusbescherming knop kan er na de intervaltijd weer af
    }
    if (vandaagbinnen && !knopActive) { 
      minmax(arr_v, arr_cv, true); 
      drawGraph(arr_cv, true);
    }
    else if (morgenbinnen && knopActive) { 
      minmax(arr_m, arr_cm, false);
      drawGraph(arr_cm, false);
    }
    else {
      toonVandaag = false;
      toonMorgen = false;
      drawGraph(arr_cv, false); // Grafiek wordt niet getoond, maar parameter wordt verwacht
      Serial.println("Wachten op nieuwe data");
    }
    Serial.print("Tijd: ");Serial.print(u);Serial.print(Uur);Serial.print(".");Serial.print(m);Serial.println(Minuut);
    if(Uur <= 6 || Uur >= 19) {
      ledcWrite(pwmChannel, 200);
    }
    else {
      ledcWrite(pwmChannel, 63);
    }
    if(!berichtverzonden && laagstePrijs && storage.WhatsApp && wifi()) {
      sendMessage(String(Prijs_nu));
    }
    previousMillis = currentMillis;
  }
}

void saveConfig() {
  for (unsigned int t = 0; t < sizeof(storage); t++)
    EEPROM.write(offsetEEPROM + t, *((char *)&storage + t));
  EEPROM.commit();
}

void loadConfig() {
  if (EEPROM.read(offsetEEPROM + 0) == storage.chkDigit)
    for (unsigned int t = 0; t < sizeof(storage); t++)
      *((char *)&storage + t) = EEPROM.read(offsetEEPROM + t);
}

void printConfig() {
  if (EEPROM.read(offsetEEPROM + 0) == storage.chkDigit) {
    for (unsigned int t = 0; t < sizeof(storage); t++)
      Serial.write(EEPROM.read(offsetEEPROM + t));
    Serial.println();
    setSettings(0);
  }
}

void setSettings(bool doAsk) {
  int i = 0;
  Serial.print(F("SSID ("));
  Serial.print(storage.ssid);
  Serial.print(F("):"));
  if (doAsk == 1) {
    getStringValue(24);
    if (receivedString[0] != 0) {
      storage.ssid[0] = 0;
      strcat(storage.ssid, receivedString);
    }
  }
  Serial.println();

  Serial.print(F("Password ("));
  Serial.print(storage.password);
  Serial.print(F("):"));
  if (doAsk == 1) {
    getStringValue(26);
    if (receivedString[0] != 0) {
      storage.password[0] = 0;
      strcat(storage.password, receivedString);
    }
  }
  Serial.println();

  Serial.print(F("API Key ("));
  Serial.print(storage.apikey);
  Serial.print(F("):"));
  if (doAsk == 1) {
    getStringValue(39);
    if (receivedString[0] != 0) {
      storage.apikey[0] = 0;
      strcat(storage.apikey, receivedString);
    }
  }
  Serial.println();

  Serial.print(F("Telefoon ("));
  Serial.print(storage.telefoon);
  Serial.print(F("):"));
  if (doAsk == 1) {
    getStringValue(14);
    if (receivedString[0] != 0) {
      storage.telefoon[0] = 0;
      strcat(storage.telefoon, receivedString);
    }
  }
  Serial.println();

  Serial.print(F("API Key cmb ("));
  Serial.print(storage.apikey_cmb);
  Serial.print(F("):"));
  if (doAsk == 1) {
    getStringValue(9);
    if (receivedString[0] != 0) {
      storage.apikey_cmb[0] = 0;
      strcat(storage.apikey_cmb, receivedString);
    }
  }
  Serial.println();

  Serial.print(F("Aanbieder ("));
  Serial.print(storage.aanbieder);
  Serial.print(F("):"));
  if (doAsk == 1) {
    getStringValue(9);
    if (receivedString[0] != 0) {
      storage.aanbieder[0] = 0;
      strcat(storage.aanbieder, receivedString);
    }
  }
  Serial.println();

  Serial.print(F("Use WhatsApp (0 -1) ("));
  Serial.print(storage.WhatsApp);
  Serial.print(F("):"));
  if (doAsk == 1) {
    i = getNumericValue();
    if (receivedString[0] != 0) storage.WhatsApp = i;
  }
  Serial.println();

  Serial.println();

  if (doAsk == 1) {
    saveConfig();
    loadConfig();
  }
}

void getStringValue(int length) {
  serialFlush();
  receivedString[0] = 0;
  int i = 0;
  while (receivedString[i] != 13 && i < length) {
    if (Serial.available() > 0) {
      receivedString[i] = Serial.read();
      if (receivedString[i] == 13 || receivedString[i] == 10) {
        i--;
      } else {
        Serial.write(receivedString[i]);
      }
      i++;
    }
  }
  receivedString[i] = 0;
  serialFlush();
}

byte getCharValue() {
  serialFlush();
  receivedString[0] = 0;
  int i = 0;
  while (receivedString[i] != 13 && i < 2) {
    if (Serial.available() > 0) {
      receivedString[i] = Serial.read();
      if (receivedString[i] == 13 || receivedString[i] == 10) {
        i--;
      } else {
        Serial.write(receivedString[i]);
      }
      i++;
    }
  }
  receivedString[i] = 0;
  serialFlush();
  return receivedString[i - 1];
}

int getNumericValue() {
  serialFlush();
  byte myByte = 0;
  byte inChar = 0;
  bool isNegative = false;
  receivedString[0] = 0;

  int i = 0;
  while (inChar != 13) {
    if (Serial.available() > 0) {
      inChar = Serial.read();
      if (inChar > 47 && inChar < 58) {
        receivedString[i] = inChar;
        i++;
        Serial.write(inChar);
        myByte = (myByte * 10) + (inChar - 48);
      }
      if (inChar == 45) {
        Serial.write(inChar);
        isNegative = true;
      }
    }
  }
  receivedString[i] = 0;
  if (isNegative == true) myByte = myByte * -1;
  serialFlush();
  return myByte;
}

void serialFlush() {
  for (int i = 0; i < 10; i++) {
    while (Serial.available() > 0) {
      Serial.read();
    }
  }
}

/***************************************************************************************
**                          Draw messagebox with message
***************************************************************************************/
void messageBox(const char *msg, uint16_t fgcolor, uint16_t bgcolor) {
  messageBox(msg, fgcolor, bgcolor, 5, 240, 230, 24);
}

void messageBox(const char *msg, uint16_t fgcolor, uint16_t bgcolor, int x, int y, int w, int h) {
  uint16_t current_textcolor = tft.textcolor;
  uint16_t current_textbgcolor = tft.textbgcolor;

  //tft.loadFont(AA_FONT_SMALL);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(fgcolor, bgcolor);
  tft.fillRoundRect(x, y, w, h, 5, fgcolor);
  tft.fillRoundRect(x + 2, y + 2, w - 4, h - 4, 5, bgcolor);
  tft.setTextPadding(tft.textWidth(msg));
  tft.drawString(msg, w/2, y + (h / 2));
  tft.setTextColor(current_textcolor, current_textbgcolor);
  tft.unloadFont();
}

bool questionBox(const char *msg, uint16_t fgcolor, uint16_t bgcolor, int x, int y, int w, int h) {
  uint16_t current_textcolor = tft.textcolor;
  uint16_t current_textbgcolor = tft.textbgcolor;

  //tft.loadFont(AA_FONT_SMALL);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(fgcolor, bgcolor);
  tft.fillRoundRect(x, y, w, h, 5, fgcolor);
  tft.fillRoundRect(x + 2, y + 2, w - 4, h - 4, 5, bgcolor);
  tft.setTextPadding(tft.textWidth(msg));
  tft.drawString(msg, w/2, y + (h / 4));

  tft.fillRoundRect(x + 4, y + (h/2) - 2, (w - 12)/2, (h - 4)/2, 5, TFT_GREEN);
  tft.setTextColor(fgcolor, TFT_GREEN);
  tft.setTextPadding(tft.textWidth("Yes"));
  tft.drawString("Yes", x + 4 + ((w - 12)/4),y + (h/2) - 2 + (h/4));
  tft.fillRoundRect(x + (w/2) + 2, y + (h/2) - 2, (w - 12)/2, (h - 4)/2, 5, TFT_RED);
  tft.setTextColor(fgcolor, TFT_RED);
  tft.setTextPadding(tft.textWidth("No"));
  tft.drawString("No", x + (w/2) + 2 + ((w - 12)/4),y + (h/2) - 2 + (h/4));
  Serial.printf("Yes = x:%d,y:%d,w:%d,h:%d\r\n",x + 4, y + (h/2) - 2, (w - 12)/2, (h - 4)/2);
  Serial.printf("No  = x:%d,y:%d,w:%d,h:%d\r\n",x + (w/2) + 2, y + (h/2) - 2, (w - 12)/2, (h - 4)/2);
  tft.setTextColor(current_textcolor, current_textbgcolor);
  tft.unloadFont();

  uint16_t touchX = 0, touchY = 0;

  long startWhile = millis();
  while (millis()-startWhile<30000) {
    bool pressed = tft.getTouch(&touchX, &touchY);
    if (pressed){
      Serial.printf("Pressed = x:%d,y:%d\r\n",touchX,touchY);
      if (touchY>=y + (h/2) - 2 && touchY<=y + (h/2) - 2 + ((h - 4)/2)){
        if (touchX>=x + 4 && touchX<=x + 4 + ((w - 12)/2)) return true;
        if (touchX>=x + (w/2) + 2 && touchX<=x + (w/2) + 2 + ((w - 12)/2)) return false;
      }
    }
  }
  return false;
}