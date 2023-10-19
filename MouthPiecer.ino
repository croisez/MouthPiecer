// https://forum.pjrc.com/threads/66835-T4-Using-USB-Host-with-MIDI
// https://little-scale.blogspot.com/2021/04/midi-usb-host-to-and-from-midi-5-pin.html
// https://github.com/vjmuzik/NativeEthernet/tree/master/examples
// https://www.electronicshub.org/hall-effect-sensor-with-arduino/
// https://github.com/FortySevenEffects/arduino_midi_library
// https://www.pjrc.com/teensy/td_libs_MIDI.html
// https://www.pjrc.com/teensy/td_midi.html
#include <USBHost_t36.h>
#include <ADC.h>
#include <ADC_util.h>
#include <MIDI.h>

#define VERSION    "1"
#define SUBVERSION "1-beta"

// Your configuration here =>
// You can send MIDIOUT through DIN-5 plug, or through usbMidi device. Use USE_DIN_FOR_MIDIOUT define for this purpose.
//#define USE_DIN_FOR_MIDIOUT
// You can use a Teensy4.1 Ethernet adapter to have access to web service for configuration and debug. Use USE_ETHERNET define for this purpose.
//#define USE_ETHERNET
// You can generate fake MIDI traffic as a demo, when no MIDI device is connected on the USBHost MIDI port. Use USE_DEMO define for this purpose.
//#define USE_DEMO
#define PB_MIN -8192
#define PB_MAX 8191
// <= Your configuration here

#ifdef USE_DIN_FOR_MIDIOUT
  MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, MIDI);
#endif

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
boolean connectedToEth = false;
long lastEthernetClientTime = 0;


void debugPlot(String varname, int val, int next = 0){
  Serial.print(varname); Serial.print(":");  Serial.print(val);
  if (next) {Serial.print(", ");} else {Serial.print("\n");}
}

void SendNoteOn(byte note, byte velocity = 127, byte channel = 1) {
#ifdef USE_DIN_FOR_MIDIOUT
  MIDI.sendNoteOn(note, velocity, channel);
#else
  usbMIDI.sendNoteOn(note, velocity, channel);
#endif
}

void SendNoteOff(byte note, byte channel = 1) {
#ifdef USE_DIN_FOR_MIDIOUT
  MIDI.sendNoteOff(note, 0, channel);
#else
  usbMIDI.sendNoteOff(note, 0, channel);
#endif
}

void SendControlChange(byte cc, byte value, byte channel = 1) {
#ifdef USE_DIN_FOR_MIDIOUT
  MIDI.sendControlChange(cc, value, channel);
#else
  usbMIDI.sendControlChange(cc, value, channel);
#endif
}

void SendPitchBend(int pitchBend, byte channel = 1) {
#ifdef USE_DIN_FOR_MIDIOUT
  MIDI.sendPitchBend(pitchBend, channel);
#else
  usbMIDI.sendPitchBend(pitchBend, channel);
#endif
}


// Exemple: le controleur USB_O2 envoie 32 messages maximum de 0 à 8191, et idem de -8192 à 0, en 128 ms. Ce qui fait 4 ms par message de PitchBend.
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
    
    //debugPlot("PB_MIN", PB_MIN, 1); debugPlot("PB_MAX", PB_MAX, 1); debugPlot("pitchBend", pitchBend);
    //Serial.println(String(pitchbendMin) + "," + String(pitchBendAnalog) + "," + String(pitchbendMax));
    //Serial.println(PB_MIN + "," + String(pitchBendValue) + "," + PB_MAX);
  }

  oldPitchbendAnalog = pitchBendAnalog;
}

// ########################################### S E T U P ###########################################
void setup() {
  Serial.begin(115200);
  Serial.println("####  MouthPiecer v" + String(VERSION) + "." + String(SUBVERSION) + "  ####");

  pinMode(LedPin,OUTPUT);
  pinMode(A1, INPUT);

  adc->adc1->setAveraging(32);                                    // set number of averages
  adc->adc1->setResolution(16);                                   // set bits of resolution
  adc->adc1->setConversionSpeed(ADC_CONVERSION_SPEED::MED_SPEED); // change the conversion speed
  adc->adc1->setSamplingSpeed(ADC_SAMPLING_SPEED::MED_SPEED);     // change the sampling speed

  delay(1500); // Wait at least 1.5 seconds before turning USBHost ON.

#ifdef USE_DIN_FOR_MIDIOUT
  MIDI.begin();
#endif

  usbhost.begin();
  usbhostMIDI.setHandleNoteOn(onMidiHostNoteOn);
  usbhostMIDI.setHandleNoteOff(onMidiHostNoteOff);
  usbhostMIDI.setHandleControlChange(onMidiHostControlChange);

#ifdef USE_ETHERNET
  EthernetBegin();
#endif

  digitalWrite(LedPin, HIGH); delay(200); digitalWrite(LedPin, LOW); delay(200); 
  digitalWrite(LedPin, HIGH); delay(200); digitalWrite(LedPin, LOW); delay(200); 
  digitalWrite(LedPin, HIGH); delay(200); digitalWrite(LedPin, LOW); delay(200); 

  Serial.println("Starting MouthPiecer main loop");
}

// ########################################### L O O P ###########################################
void loop() {
#ifdef USE_ETHERNET
  if (connectedToEth && (millis() - lastEthernetClientTime > 500)) { //500ms => do not give too much priority to web screen refresh
    listenForEthernetClients();
    lastEthernetClientTime = millis();
  }
#endif

  ProcessMidiLoop();

#ifdef USE_DEMO
  demo1();
#endif
}

// ########################################### DEMOS ###########################################
// Use the demos when you want to generate MIDI traffic without pluging a real MIDI device on the USBHost MIDI port
void demo1() {
    //byte note = random(30,85);
    byte note = 60;
    SendNoteOn(note);
    SendControlChange(2, 100);
    delay(200);
    SendNoteOff(note);
}
void demo2() {
    int note = random(30,85);
    SendNoteOn(note);
    for(int i=0;i<400;i++){
      ProcessMidiLoop();
      delay(10);
    }
    SendNoteOff(note);
}
// ########################################### DEMOS ###########################################

void ProcessMidiLoop() {
  usbhost.Task();
  usbhostMIDI.read();

#ifndef USE_DIN_FOR_MIDIOUT
  usbMIDI.read();
#endif

  if (millis() - lastPitchbendTime > 4) { //Do not send another pitch bend message sooner than 4ms.
    CapturePitchBend();
    lastPitchbendTime = millis();
  }
}

void onMidiHostNoteOn(byte channel, byte note, byte velocity) {
  SendNoteOn(note, velocity, channel);  
}
void onMidiHostNoteOff(byte channel, byte note, byte velocity) {
  SendNoteOff(note);
}
void onMidiHostControlChange(byte channel, byte control, byte value) {
  SendControlChange(control, value);
}



