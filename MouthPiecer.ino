// https://forum.pjrc.com/threads/66835-T4-Using-USB-Host-with-MIDI
// https://www.pjrc.com/teensy/td_midi.html
// https://little-scale.blogspot.com/2021/04/midi-usb-host-to-and-from-midi-5-pin.html
// https://github.com/vjmuzik/NativeEthernet/tree/master/examples
#include <USBHost_t36.h>
#include <ADC.h>
#include <ADC_util.h>
#include <SPI.h>
#include <NativeEthernet.h>
#include <TeensyThreads.h>

#define VERSION    "1"
#define SUBVERSION "0-beta"
#define USE_DHCP
#define ETHERNET_INIT_TIMEOUT 7
//#define ON_Thr 4 //comment calibrer cette valeur? Il faut que si on ne souffle pas, aucun son ne soit produit. Mais attention, il faut éviter un point dur au démarrage du souffle.
#define PB_MIN -8192
#define PB_MAX 8191

//int breathLevel;
//int oldBreath=0;
//int pressureMin = 1023;
//int pressureMax = 0;
int pitchbendMin = 5000;
int pitchbendMax = -5000;
u_int16_t pitchBendAnalog = 0;
int oldPitchbendAnalog = 0;
long lastPitchbendTime = 0;

byte LedPin = 13;

ADC *adc = new ADC();
USBHost usbhost;
USBHub hub1(usbhost);
USBHub hub2(usbhost);
MIDIDevice usbhostMIDI(usbhost);

byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED }; //Here choose MAC address for Ethernet module
#ifndef USE_DHCP
IPAddress ip(192, 168, 1, 50);
#endif
EthernetServer server(80);
EthernetClient client;
long lastEthernetClientTime = 0;
volatile boolean connectedToEth = false;

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

void debugPlot(String varname, int val, int next = 0){
  Serial.print(varname); Serial.print(":"); 
  Serial.print(val);
  if (next) {Serial.print(", ");} else {Serial.print("\n");}
}

void SendNoteOn(byte note, byte velocity = 127, byte channel = 1) {
  // midiSend((0x90 | (channel-1))), 0x50, 127);
  usbMIDI.sendNoteOn(note, velocity, channel);
}

void SendNoteOff(byte note, byte channel = 1) {
  // midiSend((0x80 | (channel-1))), 0x50, 127);
  usbMIDI.sendNoteOff(note, 0, channel);
}

void SendControlChange(byte cc, byte value, byte channel = 1) {
  //midiSend((0xB0 | (channel-1)), cc, value);
  usbMIDI.sendControlChange(cc, value, channel);
}

void SendPitchBend(int pitchBend, byte channel = 1) {
  /*debugPlot("PB_MIN", PB_MIN, 1);
  debugPlot("PB_MAX", PB_MAX, 1);
  debugPlot("pitchBend", pitchBend);*/
  usbMIDI.sendPitchBend(pitchBend, channel);
}

//  Send a three byte midi message to the MIDI DIN-5pin connector
/*void midiSend(byte midistatus, byte data1, byte data2) 
{
  digitalWrite(LedPin,HIGH);  // indicate we're sending MIDI data
  Serial1.write(midistatus);
  Serial1.write(data1);
  Serial1.write(data2);
  digitalWrite(LedPin,LOW);  // indicate we're sending MIDI data
}*/

/*
void CaptureBreath(){
  //breathLevel = adc->analogRead(A0); adc->resetError();
  //debugPlot("minP", pressureMin, 1); debugPlot("breathP", breathLevel, 1); debugPlot("maxP", pressureMax,1);

  breathLevel = oldBreath*0.0+breathLevel*1.0;
  oldBreath = breathLevel;
  breathLevel = map(constrain(breathLevel, pressureMin + ON_Thr, pressureMax), pressureMin + ON_Thr, pressureMax, 0, 127);
  SendControlChange(2, breathLevel);
}
*/

// Le controleur USB_O2 envoie 32 messages maximum de 0 à 8191, et idem de -8192 à 0, en 128 ms. Ce qui fait 4 ms par message de PitchBend.
void CapturePitchBend() {
  pitchBendAnalog = adc->analogRead(A1);
  adc->resetError();

  if (pitchbendMin > pitchBendAnalog) pitchbendMin = pitchBendAnalog;
  if (pitchbendMax < pitchBendAnalog) pitchbendMax = pitchBendAnalog;

  if (abs(pitchBendAnalog - oldPitchbendAnalog) > 2) {
    digitalWrite(LedPin, HIGH);
    int pitchBendValue = map( constrain(pitchBendAnalog, pitchbendMin, pitchbendMax) , pitchbendMin, pitchbendMax , PB_MIN , PB_MAX);
    SendPitchBend(pitchBendValue);
    digitalWrite(LedPin, LOW);
    //Serial.println(String(pitchbendMin) + "," + String(pitchBendAnalog) + "," + String(pitchbendMax));
    //Serial.println("-8192," + String(pitchBendValue) + ",8192");
  }

  oldPitchbendAnalog = pitchBendAnalog;
}

// ########################################### S E T U P ###########################################
void setup() {
  Serial.begin(115200);
  Serial.println("####  MouthPiecer v" + String(VERSION) + "." + String(SUBVERSION) + "  ####");

  pinMode(LedPin,OUTPUT);
  //pinMode(A0, INPUT);
  pinMode(A1, INPUT);

  //adc->adc0->setAveraging(16);                                    // set number of averages
  //adc->adc0->setResolution(12);                                   // set bits of resolution
  //adc->adc0->setConversionSpeed(ADC_CONVERSION_SPEED::MED_SPEED); // change the conversion speed
  //adc->adc0->setSamplingSpeed(ADC_SAMPLING_SPEED::MED_SPEED);     // change the sampling speed
  adc->adc1->setAveraging(32);                                    // set number of averages
  adc->adc1->setResolution(16);                                   // set bits of resolution
  adc->adc1->setConversionSpeed(ADC_CONVERSION_SPEED::MED_SPEED); // change the conversion speed
  adc->adc1->setSamplingSpeed(ADC_SAMPLING_SPEED::MED_SPEED);     // change the sampling speed

  delay(1500);// Wait at least 1.5 seconds before turning on USB Host.

  usbhost.begin();
  usbhostMIDI.setHandleNoteOn(onMidiHostNoteOn);
  usbhostMIDI.setHandleNoteOff(onMidiHostNoteOff);
  usbhostMIDI.setHandleControlChange(onMidiHostControlChange);

  //calibrateSensors();

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
  
  digitalWrite(LedPin, HIGH); delay(200); digitalWrite(LedPin, LOW); delay(200); 
  digitalWrite(LedPin, HIGH); delay(200); digitalWrite(LedPin, LOW); delay(200); 
  digitalWrite(LedPin, HIGH); delay(200); digitalWrite(LedPin, LOW); delay(200); 

  Serial.println("Starting MouthPiecer main loop");
}

// ########################################### L O O P ###########################################
void loop() {
  if (connectedToEth && (millis() - lastEthernetClientTime > 500)) { //500ms = do not give too much priority to web screen refresh
    listenForEthernetClients();
    lastEthernetClientTime = millis();
  }
  
  ProcessMidiLoop();
#ifdef DEMO
  demo1();
#endif
}

// ########################################### DEMOS ###########################################
void demo1() {
    //byte note = random(30,85);
    byte note = 60;
    SendNoteOn(note);
    SendControlChange(2,100);
    delay(200);
    SendNoteOff(note);
}

void demo2() {
    int note = random(30,85);
    SendNoteOn(note);
    for(int i=0;i<400;i++){
      //CaptureBreath();
      ProcessMidiLoop();
      delay(10);
    }
    SendNoteOff(note);
}
/* **************************************************************************** */

void ProcessMidiLoop() {
  usbhost.Task();
  usbhostMIDI.read();
  usbMIDI.read();
  if (millis() - lastPitchbendTime > 4) { //Do not send another pitch bend message sooner than 4ms.
    CapturePitchBend();
    lastPitchbendTime = millis();
  }
}

void onMidiHostNoteOn(byte channel, byte note, byte velocity) {
  // debugPrint("Note On, ch="); debugPrint(channel); debugPrint(", note="); debugPrint(note); debugPrint(", velocity="); debugPrintln(velocity);
  SendNoteOn(note, velocity, channel);  
}
void onMidiHostNoteOff(byte channel, byte note, byte velocity) {
  // debugPrint("Note Off, ch="); debugPrint(channel); debugPrint(", note="); debugPrint(note); debugPrint(", velocity="); debugPrintln(velocity);
  SendNoteOff(note);
}
void onMidiHostControlChange(byte channel, byte control, byte value) {
  // debugPrint("Control Change, ch="); debugPrint(channel); debugPrint(", control="); debugPrint(control); debugPrint(", value="); debugPrint(value);
  SendControlChange(control, value);
}

#if 0
void calibrateSensors(){
  Serial.println("Start of sensors calibration for 5s...");
 
  for (int i=0; i<50; i++){
      digitalWrite(LedPin, HIGH);
      /*
      int valueP = adc->analogRead(A0);
      if (pressureMin > valueP) pressureMin = valueP;
      if (pressureMax < valueP) pressureMax = valueP;
      delay(100);
      */
      int valueB = adc->analogRead(A1);
      if (pitchbendMin > valueB) pitchbendMin = valueB;
      if (pitchbendMax < valueB) pitchbendMax = valueB;
      delay(100);
      digitalWrite(LedPin, LOW);
  }
 
  Serial.println("End of sensors calibration:");
  //Serial.print("Min pressure: "); Serial.print(pressureMin); Serial.print(", Max pressure: "); Serial.print(pressureMax);
  Serial.print("Min pitchBend: "); Serial.print(pitchbendMin); Serial.print(", Max pitchBend: "); Serial.println(pitchbendMax);
  delay(3000);
  //for (u_int16_t i = pitchbendMin - 5; i < pitchbendMax + 5; i++) { int pbv = map( constrain(i, pitchbendMin, pitchbendMax), pitchbendMin, pitchbendMax, PB_MIN, PB_MAX); Serial.print(i); Serial.print("; ");Serial.println(pbv);}
}
#endif

void EthernetRequestDhcpLease() {
  switch (Ethernet.maintain()) {
  case 1:
    //renewed fail
    Serial.println("Error: renewed fail");
    return false;
    break;

  case 2:
    //renewed success
    Serial.println("Renewed success.");
    Serial.print("IP address: ");
    Serial.println(Ethernet.localIP());
    return true;
    break;

  case 3:
    //rebind fail
    Serial.println("Error: rebind fail");
    return false;
    break;

  case 4:
    //rebind success
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