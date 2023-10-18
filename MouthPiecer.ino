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

#define USE_DHCP
//#define DEBUG_VAL
//#define DEBUG_PRINT
//#define DEBUG_PLOT
#define ON_Thr 4 //comment calibrer cette valeur? Il faut que si on ne souffle pas, aucun son ne soit produit. Mais attention, il faut éviter un point dur au démarrage du souffle.
#define PB_Thr 0 //-115 //comment calibrer cette valeur? Il faut que pitchbendAnalog au repos donne une valeur MIDI de 0 pour le pitchbend
#define PB_MIN -8192
#define PB_MAX 8191
#define PB_STEP 1

int breathLevel;
int oldBreath=0;
int pressureMin = 1023;
int pressureMax = 0;
int pitchbendMin = 5000;
int pitchbendMax = -5000;
u_int16_t pitchBendAnalog = 0;
int oldPitchbendValue = 0;
long lastPitchbendTime = 0;

byte LedPin = 13;    // select the pin for the LED

ADC *adc = new ADC();
USBHost usbhost;
USBHub hub1(usbhost);
USBHub hub2(usbhost);
MIDIDevice usbhostMIDI(usbhost);

byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
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
  Serial.println("End of ethInit thread");
}

//  Debug function using USB serial port
void debugVal(String str){
  #ifdef DEBUG_VAL
  Serial.println(str);
  #endif
}
void debugPrint(String str){
  #ifdef DEBUG_PRINT
  Serial.print(str);
  #endif
}
void debugPrintln(String str){
  #ifdef DEBUG_PRINT
  Serial.println(str);
  #endif
}

void debugPlot(String varname, int val, int next = 0){
  #ifdef DEBUG_PLOT
  Serial.print(varname); Serial.print(":"); 
  Serial.print(val);
  if (next) {Serial.print(", ");} else {Serial.print("\n");}
  #endif
}

void SendNoteOn(byte note, byte velocity = 127, byte channel = 1) {
  // midiSend((0x90 | (channel-1))), 0x50, 127);
  usbMIDI.sendNoteOn(note, velocity, channel);
}

void SendNoteOff(byte note, byte channel = 1) {
  // midiSend((0x80 | (channel-1))), 0x50, 127);
  usbMIDI.sendNoteOff(note, 0, channel);
}

void SendControlChange(byte cc, byte breathLevel, byte channel = 1) {
  //midiSend((0xB0 | (channel-1)), cc, breathLevel);
  usbMIDI.sendControlChange(cc, breathLevel, channel);
}

void SendPitchBend(int pitchBend, byte channel = 1) {
  /*debugPlot("PB_MIN", PB_MIN, 1);
  debugPlot("PB_MAX", PB_MAX, 1);
  debugPlot("pitchBend", pitchBend);*/
  usbMIDI.sendPitchBend(pitchBend, channel);
}

  //  Send a three byte midi message  
/*void midiSend(byte midistatus, byte data1, byte data2) 
{
  digitalWrite(LedPin,HIGH);  // indicate we're sending MIDI data
  Serial1.write(midistatus);
  Serial1.write(data1);
  Serial1.write(data2);
  digitalWrite(LedPin,LOW);  // indicate we're sending MIDI data
}*/

void CaptureBreath(){
  //breathLevel = adc->analogRead(A0);
  //adc->resetError();

  /*debugPlot("minP", pressureMin, 1);
  debugPlot("breathP", breathLevel, 1);
  debugPlot("maxP", pressureMax,1);*/

  breathLevel = oldBreath*0.0+breathLevel*1.0;
  oldBreath = breathLevel;
  breathLevel = map(constrain(breathLevel, pressureMin + ON_Thr, pressureMax), pressureMin + ON_Thr, pressureMax, 0, 127);
  breathLevel = 127; //Force to disable
  //SendControlChange(7, breathLevel);
}

float fmap(float x, float in_min, float in_max, float out_min, float out_max)
{
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// Le controleur USB_O2 envoie 32 messages maximum de 0 à 8191, et idem de -8192 à 0, en 128 ms. Ce qui fait 4 ms par message de PitchBend.
void CapturePitchBend() {
  pitchBendAnalog = adc->analogRead(A1);
  adc->resetError();

  //debugVal(pitchBendValue);

  int pitchBendValue = map( constrain(pitchBendAnalog, pitchbendMin + PB_Thr, pitchbendMax), pitchbendMin + PB_Thr, pitchbendMax, PB_MIN, PB_MAX);

  if (abs(pitchBendValue - oldPitchbendValue) >= 2) {
    digitalWrite(LedPin, HIGH);
    SendPitchBend(pitchBendValue);
    digitalWrite(LedPin, LOW);
    //Serial.println(String(pitchbendMin) + "," + String(pitchBendAnalog) + "," + String(pitchbendMax));
  }
  /*if (oldPitchbendValue < pitchBendValue){
    for (int pbv=oldPitchbendValue; pbv < pitchBendValue; pbv += PB_STEP) SendPitchBend(pbv);
  } else if (oldPitchbendValue > pitchBendValue) {
    for (int pbv=oldPitchbendValue; pbv > pitchBendValue; pbv -= PB_STEP) SendPitchBend(pbv);
  }*/

  oldPitchbendValue = pitchBendValue;
}

void setup() {
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

  delay(500); 
  Serial.begin(115200);
  delay(2000);// Wait at least 1.5 seconds before turning on USB Host.

  usbhost.begin();
  usbhostMIDI.setHandleNoteOn(onMidiHostNoteOn);
  usbhostMIDI.setHandleNoteOff(onMidiHostNoteOff);
  usbhostMIDI.setHandleControlChange(onMidiHostControlChange);

  calibrateSensors();

  threads.addThread(ethInit);
  Serial.println("Searching for ethernet connection...");
  long ethSavedTime = millis();
  while(!connectedToEth && millis() - ethSavedTime < 10000) { delay(1000); }
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

void loop() {
  if (connectedToEth && (millis() - lastEthernetClientTime > 500)) {
    listenForEthernetClients();
    lastEthernetClientTime = millis();
  }
  
  ProcessMidiLoop();
  //demo2();
}

/* *********************************DEMO******************************************* */
void demo1() {
    SendNoteOn(80);
    SendControlChange(7,127);
    while(1){
      ProcessMidiLoop();
    }
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
/* *********************************DEMO******************************************* */

void ProcessMidiLoop() {
  usbhost.Task();
  usbhostMIDI.read();
  usbMIDI.read();
  if (millis() - lastPitchbendTime > 4) {
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
  /*Serial.print("Min pressure: "); Serial.print(pressureMin); Serial.print(", Max pressure: "); Serial.print(pressureMax);*/
  Serial.print("Min pitchBend: "); Serial.print(pitchbendMin); Serial.print(", Max pitchBend: "); Serial.println(pitchbendMax);
  delay(3000);
  //for (u_int16_t i = pitchbendMin - 5; i < pitchbendMax + 5; i++) { int pbv = map( constrain(i, pitchbendMin, pitchbendMax), pitchbendMin, pitchbendMax, PB_MIN, PB_MAX); Serial.print(i); Serial.print("; ");Serial.println(pbv);}
}

void EthernetRequestDhcpLease() {
  switch (Ethernet.maintain()) {
  case 1:
    //renewed fail
    Serial.println("Error: renewed fail");
    return false;
    break;

  case 2:
    //renewed success
    Serial.println("Renewed success");
    //print your local IP address:
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
    Serial.println("Rebind success");
    //print your local IP address:
    Serial.print("IP address: ");
    Serial.println(Ethernet.localIP());
    return true;
    break;

  default:
    //nothing happened
    return true;
    break;
  }
}
void listenForEthernetClients() {
  // listen for incoming clients
  EthernetClient client = server.available();
  if (client) {
    //Serial.println("Got a client");
    boolean currentLineIsBlank = true;// an http request ends with a blank line
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        // if you've gotten to the end of the line (received a newline character) and the line is blank, the http request has ended, so you can send a reply
        if (c == '\n' && currentLineIsBlank) {
          // send a standard http response header
          client.println("HTTP/1.1 200 OK");
          client.println("Content-Type: text/html");
          client.println();
          int pbv = map( constrain(pitchBendAnalog, pitchbendMin + PB_Thr, pitchbendMax), pitchbendMin + PB_Thr, pitchbendMax, PB_MIN, PB_MAX);
          // print the current readings, in HTML format:
          client.print("AnalogPitch: [" + String(pitchbendMin) + " < " + String(pitchBendAnalog) + " < " + String(pitchbendMax) + "]");
          client.println("<br />");
          client.print("Pitch: " + String(pbv));
          client.println("<br />");
          break;
        }
        if (c == '\n') {
          currentLineIsBlank = true;// you're starting a new line
        } else if (c != '\r') {
          currentLineIsBlank = false;// you've gotten a character on the current line
        }
      }
    }
    delay(1);// give the web browser time to receive the data
    client.stop();// close the connection
  }
}