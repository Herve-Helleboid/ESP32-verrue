#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <ETH.h> 
#include <WiFi.h>
#include <Preferences.h>    // pour utiliser la flash car ESP32 n'a pas d'EEprom
#include "Send_Spi.h"
#include <esp_system.h>     // pour commande de redemarrage à froid
#include <WiFiClient.h>

Preferences preferences;

#define ETH_ADDR        0
#define ETH_POWER_PIN   -1 // Do not use it, it can cause conflict during the software reset.
//#define ETH_POWER_PIN_ALTERNATIVE 17
#define ETH_MDC_PIN    23
#define ETH_MDIO_PIN   18
#define ETH_TYPE       ETH_PHY_LAN8720
#define ETH_CLK_MODE    ETH_CLOCK_GPIO0_IN
WiFiServer server; // Declare server objects

#define MOSI 13
#define MISO 12
#define SCK 14
#define SS 15

#define Relais1 32

#define RW_MODE false
#define RO_MODE true

const int BUFFER_SIZE = 50;
char buf[BUFFER_SIZE];
bool Relais = false;

int longMaxValue = 9500;
int valMax =935;
int AttX[8] = {935,935,935,935,935,935,935,935};
byte nbAttMax = 8;

unsigned long LedVie = 0;             // compteur
unsigned long interval = 40000;      // environ 1 seconde 8000000 = 10 minutes
uint8_t ipAdress[4]; // adresse IP a extraire de la réception

WiFiServer httpServer(80); // Déclare un serveur web sur le port 80

volatile uint32_t timerCounter = 0;
bool isConnected = false;

void IRAM_ATTR onTimer() {
  timerCounter++;
  if (timerCounter >= (isConnected ? 4 : 1)) {
    timerCounter = 0;
    if (Relais == true) {
      digitalWrite(Relais1, LOW);
      Relais = false;
    } else {
      digitalWrite(Relais1, HIGH);
      Relais = true;
    }
  }
}


void setup() {
  pinMode(Relais1, OUTPUT);
  pinMode(MOSI, OUTPUT);
  SPI.begin(SCK, MISO, MOSI, SS);
  Serial.begin(38400);
  Serial.setTimeout(3000); // attends la complétion \n de notre entrée
  delay(4000);              // petit délai à la mise sous tension
  //Serial.print("MOSI: ");   // affichage de mes pins utilisées pour le SPI, a virer plus tard !
  //Serial.println(MOSI);
  //Serial.print("MISO: ");
  //Serial.println(MISO);
  //Serial.print("SCK: ");
  //Serial.println(SCK);
  //Serial.print("SS: ");
  //Serial.println(SS); 

  preferences.begin("monIP", RO_MODE); // ouvre en lecture seule le namespace (read only mode ou rw)

  bool tpInit = preferences.isKey("ipByte");       // Test l'existence de la clé nommée ipByte
  if (tpInit == false) {
      // si tpInit est 'false', la clé "ipByte" n'existe pas encore
      // est utilisé la première fois seulement. Pour le besoin de Preferences namespace keys. Alors...
      preferences.end();                             // ferme le namespace en mode RO mode et...

      preferences.begin("monIP", RW_MODE);        //  réouvre en mode RW.
      ipAdress[0] = 0;ipAdress[1] = 0;ipAdress[2] = 0;ipAdress[3] = 0;  // IP
      ipAdress[4] = 0;ipAdress[5] = 0;ipAdress[6] = 0;ipAdress[7] = 0;  // GATEWAY
      ipAdress[8] = 0;ipAdress[9] = 0;ipAdress[10] = 0;ipAdress[11] = 0; // MASK
      preferences.putBytes("ipByte", ipAdress, 12); //12 octets
  }

  ETH.begin(ETH_ADDR, ETH_POWER_PIN, ETH_MDC_PIN, ETH_MDIO_PIN, ETH_TYPE, ETH_CLK_MODE); // Enable ETH
  preferences.getBytes("ipByte", ipAdress, 12);   // Lire l'adresse IP stockée à l'adresse "my-ip" dans la flash
  preferences.end();

  if (ipAdress[0] != 0){                 // si pas d'IP valide dans la flash, on affiche pas.
    Serial.print("IP Address in flash: ");
    for (int i = 0; i < 4; i++) {  // boucle pour afficher les 4 octets de l'adresse IP
      Serial.print(ipAdress[i]);
      if (i < 3) {  // ajoute un point après chaque octet, sauf le dernier
        Serial.print(".");
      }
    }
    Serial.print(" GateWay:");  
    for (int i = 4; i < 8; i++) {  // boucle pour afficher les 4 octets du Gateway
      Serial.print(ipAdress[i]);
      if (i < 7) {  // ajoute un point après chaque octet, sauf le dernier
        Serial.print(".");
      }
    }
    Serial.print(" Mask:");  
    for (int i = 8; i < 12; i++) {  // boucle pour afficher les 4 octets du Mask
      Serial.print(ipAdress[i]);
      if (i < 11) {  // ajoute un point après chaque octet, sauf le dernier
        Serial.print(".");
      }
    }
    Serial.println(" ");
    
    ETH.config(IPAddress(ipAdress[0],ipAdress[1],ipAdress[2],ipAdress[3]), IPAddress(192, 168, 0, 254), IPAddress(255, 255, 255, 0));// + dns1 et 2 si necessaire
    Serial.print("Using static IP ");
    Serial.println(ETH.localIP());
  }
  else {
    if ((uint32_t)ETH.localIP()){     //reçu une IP
    Serial.print("[LAN Connected - DHCP - IP Address: ");
    Serial.print(ETH.localIP());
    Serial.println("]");
    }
  }
   
  
  Serial.println("let's go"); // LAN configuré  à présent
  server.begin(10001); // Serveur ecoute port 10001
  //SPI.begin();              // Initialise le bus SPI en mode Master
  httpServer.begin(); // Serveur web écoute port 80 

  // Configure le timer pour le clignotement du relais
  hw_timer_t * timer = NULL;
  timer = timerBegin(0, 80, true); // Timer 0, diviseur 80, compte à la hausse
  timerAttachInterrupt(timer, &onTimer, true); // Attache l'interruption au timer
  timerAlarmWrite(timer, 1000000, true); // Déclenche l'interruption toutes les 1000000 microsecondes (1 seconde)
  timerAlarmEnable(timer); // Active l'alarme du timer

}

  void verif(int att){                 // verifie si le n° d'atténuateur existe et attribue sa valeur
   int RX = buf[att]-'0';            // doit être compris entre 1 et nbAttMax(attenuateurs)
   if (RX > 0 && RX<= nbAttMax){
   int value = (((buf[att+2]-48)*100) + ((buf[att+3]-48)*10) + buf[att+4]-48);  //48 = valeur décimale (ASCII)
   if (value > valMax)
    {
      value == valMax;              // si on dépasse la val max d'un att alors val = max
    }
    AttX[att-1]={value};            // mémorise
  }
}


void clear_buf(){                     // nettoie le buffer réception
  while (Serial.available() > 0) {
  Serial.read();
  } 
}


void analyse(int ctrl,WiFiClient client) {     // ctrl = 0 = LAN    (1 = serial USB)
  // Recherche de la première occurrence d'un motif d'adresse IP, de la commande "ATT", ou de la commande "XYZ"
  char *start = strstr(buf, "IP?");
  if (start != NULL) {
    Serial.print("IP= ");
    Serial.println(ETH.localIP()); 
    }

// **************** COMMANDE RECUE = ATT ****************************
  start = strstr(buf, "ATT ");
    if (start != NULL){
      start += 4;
      char* token = strtok(start, ";"); // Découpe la chaîne à chaque point-virgule
      while (token != NULL) {
      // Récupère la valeur d'atténuation et l'atténuateur correspondant
      int attNum;
      int attValue;
      sscanf(token, "%d %d", &attNum, &attValue);   
      AttX[attNum - 1] = attValue;
      token = strtok(NULL, ";");  //est utilisée pour récupérer la sous-chaîne suivante à partir de la chaîne de caractères start en utilisant le caractère ; comme délimiteur
      }
    // transforme la valeur longue en 2 octets utilisables pour les nbAttMax atténuateurs
    int octet1 = 0;
    int octet2 = 0;
    int val = 0;
  for (int attn = 0; attn < nbAttMax; attn++) // prendra chaque valeur d'atténuateur jusqu'à nbAttMax
  {
    if (AttX[attn] > 9500){
      AttX[attn] = 9500;                       // limite la valeur maximale
    }
    int valattxattn = AttX[attn];
    if (valattxattn > 999){                       // 4 chiffres donc   ex: 9475 (94.75dB)
      int lastTwoDigits = (valattxattn % 100);   // extrait les 2 derniers chiffres ex: 75 (0.75dB)
      if (valattxattn > 9350){                    // on est dans le cas pour l'exemple
        int difference = (valattxattn - 9350);    // difference 125 (1.25dB) (150 max)
        switch (difference) {
          case 150:
            octet2 = 15;
            break;
          case 125:
            octet2 = 7;
            break;
          case 100:
            octet2 = 5;
            break;
          case 75:
            octet2 = 3;
            break;
          case 50:
            octet2 = 1;
            break;
          case 25:
            octet2 = 2;
            break;
        } 
        
        // continuer ici ********************
      }

    }

    int z = (AttX[attn]/10);              // divise atténuateur par 10
    
    if (z < 63){
      if (z < 31){
        val = z & 31;
      }
      else{
        val = ((z - 31) & 31) + 32;
      }
    }
    else{
      val = ((z - 62) & 31) + 96;
    }

    val = val << 1;
    int pointCinq = z*10;                 // recherche le reste après multiplication
    if ((AttX[attn] - pointCinq) != 0)    // vrai si terminé par 5
    {  
      val |= (1 << 0);
    }
    //Serial.println(val);
    SPI.transfer(val);
  }  
  //  sendSPI(AttX,nbAttMax);
    clear_buf();   //nettoie le buffer de réception
    } 
// **************  fin du ATT ***********************

// **************** COMMANDE RECUE = STA ****************************
  start = strstr(buf, "STA?");
    if (start != NULL){
      if (ctrl == 1){
        Serial.print("STA ");
        for (int i = 1; i<9; i++) {
          Serial.print(i);
          Serial.print(" ");
          Serial.print(AttX[i-1]);
          if (i<8){
            Serial.print(";");
          }
        }
      Serial.println();
      }
      else{
        client.print("STA ");
        for (int i = 1; i<9; i++) {
          client.print(i);
          client.print(" ");
          client.print(AttX[i-1]);
          if (i<8){
            client.print(";");
          }
        }
      client.println();
      }
    clear_buf(); 
    } 
// **************  fin du STA ***********************

// **************** COMMANDE RECUE = RESTART ****************************
  start = strstr(buf, "RESTART");
  if (start != NULL){
    esp_restart();
  }
// **************  fin du RESTART ***********************

// **************** COMMANDE RECUE = DHCP ****************************
  start = strstr(buf, "DHCP");
  if (start != NULL){
    ipAdress[0] = 0;
    ipAdress[1] = 0;
    ipAdress[2] = 0;
    ipAdress[3] = 0;
    preferences.begin("monIP", RW_MODE);          //  reopen it in RW mode. true
    preferences.putBytes("ipByte", ipAdress, 4);  //& permet de récupérer l'adresse mémoire de la variable "ipAdress"
    preferences.end();                            // ferme
    if (ctrl == 1){                               // dirige vers serial ou LAN
      Serial.println("Done DHCP");
    } else {
      client.println("Done DHCP");
    }
  }
// **************  fin du DHCP ***********************

// **************** COMMANDE RECUE = IP ****************************
  start = strstr(buf, "IP ");
    if (start != NULL){
      start += 3;    // utilisation de sscanf pour extraire ip sous forme de 4 entiers séparés
      int result = sscanf(start, "%hhu.%hhu.%hhu.%hhu", &ipAdress[0], &ipAdress[1], &ipAdress[2],&ipAdress[3]);
      // changer %hhu par %u si on est sûr que les parties de l'adresse IP ne dépasseront jamais 255. unsigned char
      if (result == 4) {      // si extraction réussie on écrit dans la flash
        preferences.begin("monIP", RW_MODE);        //  reopen it in RW mode. true
        preferences.putBytes("ipByte", ipAdress, 4);  //& permet de récupérer l'adresse mémoire de la variable "ipAdress"
        preferences.end();                           // ferme
        Serial.println("Done IP");
      }
    }
  clear_buf();  
  }
// **************  fin du IP ***********************  


// BOUCLE DU PROGRAMME **************************************************
void loop(){
  WiFiClient client = server.available(); // Try to create a client object
  WiFiClient httpClient = httpServer.available();

  if (httpClient) {
    //Serial.println("[HTTP client connected]");
    
    //String response = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n<!DOCTYPE HTML>\r\n<html>\r\n<head>\r\n<title>ESP32</title>\r\n</head>\r\n<body>\r\n<h1>HYTEM: Only Use Telnet please</h1>\r\n</body>\r\n</html>\r\n";
    String response = "<!DOCTYPE HTML>\r\n<html>\r\n<head>\r\n<title>ESP32</title>\r\n</head>\r\n<body>\r\n<h1>Only Use Telnet please</h1>\r\n<a href=\"http://maintenance.hytem.net\">http://maintenance.hytem.net</a>\r\n</body>\r\n</html>\r\n";
    httpClient.print(response);
    //Serial.println("[HTTP message sent]");

    delay(10);
    httpClient.stop();
    //Serial.println("[HTTP client disconnected]");
  }

  if (client){ // If current customer is available
    Serial.println("[Client connected]");
    isConnected = true;
    String readBuff;
    while (client.connected()){ // If the client is connected
      if (client.available() > 0){ // If there is readable data
        int recu = client.readBytesUntil('\n',buf,BUFFER_SIZE);
        //client.print(recu); //  recu = nombre d'octets
        /*for(int i=0;i<recu;i++)
        {  
        Serial.print(buf[i]);  
        }*/
        analyse(0,client);    //1 mode serie - 0 mode LAN et client
      }
    }
      client.stop(); //End the current connection
      Serial.println("[Client disconnected]");
      isConnected = false;
  }

  /*LedVie += 1;
  if (LedVie > interval){
    LedVie = 0;
    if (Relais == true){
      digitalWrite (Relais1, LOW);
      Relais = false;
    }
    else{
      digitalWrite (Relais1, HIGH);
      Relais = true;
    }
  } */

  if (Serial.available()>0){
    int recu = Serial.readBytesUntil('\n',buf,BUFFER_SIZE);
    /*Serial.print(recu);
    for(int i=0;i<recu;i++)
    Serial.print(buf[i]); */
    analyse(1,client);  //1 mode serie - 0 mode LAN
  }
}
  