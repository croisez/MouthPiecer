#ifdef USE_ETHERNET

#include <SPI.h>
#include <NativeEthernet.h>
#include <TeensyThreads.h>

#define USE_DHCP
#define ETHERNET_INIT_TIMEOUT 7

byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED }; //Here choose MAC address for Ethernet module
#ifndef USE_DHCP
IPAddress ip(192, 168, 1, 50);
#endif
EthernetServer server(80);
EthernetClient client;


void EthernetBegin() {
    threads.addThread(ethInit);
  Serial.println("Searching for ethernet connection...");
  long ethSavedTime = millis();
  while(!connectedToEth && millis() - ethSavedTime < 1000 * ETHERNET_INIT_TIMEOUT) { delay(1000); }
  if(connectedToEth){
    Serial.println("Connected to ethernet");
    Serial.print("IP address: ");
    Serial.println(Ethernet.localIP());
#ifdef USE_DHCP
    EthernetRequestDhcpLease();
#endif
    server.begin();
  }
  else{
    Serial.println("Running without ethernet");
  }
}

void ethInit(){
  while(!connectedToEth){
    Ethernet.begin(mac 
#ifndef USE_DHCP
    , ip
#endif
    );
    connectedToEth = true;
  }
  //Serial.println("End of ethInit thread");
}

void EthernetRequestDhcpLease() {
  switch (Ethernet.maintain()) {
  case 1: //renewed fail
    Serial.println("Error: renewed fail");
    return false;
    break;

  case 2: //renewed success
    Serial.println("Renewed success.");
    Serial.print("IP address: ");
    Serial.println(Ethernet.localIP());
    return true;
    break;

  case 3: //rebind fail
    Serial.println("Error: rebind fail");
    return false;
    break;

  case 4: //rebind success
    Serial.println("Rebind success.");
    Serial.print("IP address: ");
    Serial.println(Ethernet.localIP());
    return true;
    break;

  default: //nothing happened
    return true;
    break;
  }
}

//Embryon of a web service, for exchanging information through a web browser with this application
void listenForEthernetClients() {
  EthernetClient client = server.available(); // listen for incoming clients
  if (client) {
    boolean currentLineIsBlank = true; // an http request ends with a blank line
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        // if you've gotten to the end of the line (received a newline character) and the line is blank, the http request has ended, so you can send a reply
        if (c == '\n' && currentLineIsBlank) {
          // send a standard http response header
          client.println("HTTP/1.1 200 OK");
          client.println("Content-Type: text/html");
          client.println();
          int pbv = map( constrain(pitchBendAnalog, pitchbendMin, pitchbendMax), pitchbendMin, pitchbendMax, PB_MIN, PB_MAX);
          // print the current readings, in HTML format:
          client.print("AnalogPitch: [" + String(pitchbendMin) + " < " + String(pitchBendAnalog) + " < " + String(pitchbendMax) + "]");
          client.println("<br />");
          client.print("Pitch: " + String(pbv));
          client.println("<br />");
          break;
        }
        if (c == '\n') {
          currentLineIsBlank = true;
        } else if (c != '\r') {
          currentLineIsBlank = false;
        }
      }
    }
    delay(1);
    client.stop();
  }
}

#endif