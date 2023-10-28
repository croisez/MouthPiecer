#include <USBHost_t36.h>
#include <ADC.h>
#include <ADC_util.h>
#include <MIDI.h>

#define VERSION    "1"
#define SUBVERSION "3-alpha"

// Your configuration here =>

// You can send MIDIOUT through DIN-5 plug, or through usbMidi device. Use USE_DIN_FOR_MIDIOUT define for this purpose.
//#define USE_DIN_FOR_MIDIOUT

// You can use a Teensy4.1 Ethernet adapter to have access to web service for configuration and debug. Use USE_ETHERNET define for this purpose.
//#define USE_ETHERNET

// You can generate fake MIDI traffic as a demo, when no MIDI device is connected on the USBHost MIDI port. Use USE_DEMO define for this purpose.
//#define USE_DEMO

// Uncomment to print PitchBend values in serial terminal
//#define DEBUG_PITCHBEND

// Definition of Low-Pass filter coef, order 1
#define SMOOTHING_COEF 0.3

#define PB_MIN -8192
#define PB_MAX 8191
#define CC_TRAVELSAX2 2
#define BUTTON_PIN 8
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
long lastDemoTime = 0;
byte breathValueMin = 0;
byte currentBreathValue;
long lastButtonTime = 0;

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

float pbvMem = 0.0;

int SmoothPitchBend(int pbv) {
  float pvbSmooth = pbvMem + SMOOTHING_COEF * (pbv - pbvMem);
  pbvMem = pvbSmooth;
  return (int)pvbSmooth;
}

// Exemple: le controleur USB_O2 envoie 32 messages maximum de 0 à 8191, et idem de -8192 à 0, en 128 ms. Ce qui fait 4 ms par message de PitchBend.
void CapturePitchBend() {
  pitchBendAnalog = adc->analogRead(A1);
  adc->resetError();

  if (pitchbendMin > pitchBendAnalog) pitchbendMin = pitchBendAnalog;
  if (pitchbendMax < pitchBendAnalog) pitchbendMax = pitchBendAnalog;

//  if (abs(pitchBendAnalog - oldPitchbendAnalog) > 0) {
    digitalWrite(LedPin, HIGH);
    int pitchBendValue = map( constrain(pitchBendAnalog, pitchbendMin, pitchbendMax) , pitchbendMin, pitchbendMax , PB_MAX , PB_MIN);
    //int pitchBendValueLP = SmoothPitchBend(pitchBendValue);
    SendPitchBend(pitchBendValue);
    digitalWrite(LedPin, LOW);
    
#ifdef DEBUG_PITCHBEND  
    //debugPlot("PB_MIN", PB_MIN, 1); debugPlot("PB_MAX", PB_MAX, 1); debugPlot("pitchBend", pitchBend);
    Serial.println(String(pitchbendMin) + "," + String(pitchBendAnalog) + "," + String(pitchbendMax));
    //Serial.println(String(PB_MIN) + "," + String(pitchBendValueLP) + "," + String(PB_MAX));
#endif
//  }

  oldPitchbendAnalog = pitchBendAnalog;
}

// ########################################### S E T U P ###########################################
void setup() {
  Serial.begin(115200);
  Serial.println("####  MouthPiecer v" + String(VERSION) + "." + String(SUBVERSION) + "  ####");

  pinMode(LedPin,OUTPUT);
  pinMode(A1, INPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

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
#ifdef USE_DEMO
  Serial.println("DEMO mode activated");
#endif
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

  if (millis() - lastButtonTime > 200) {
    if (! digitalRead(BUTTON_PIN)) {
      Serial.println("Button pressed");
      breathValueMin = currentBreathValue;
      pitchbendMin = 5000;
      pitchbendMax = -5000;
    }
    lastButtonTime = millis();
  }

#ifdef USE_DEMO
  if (millis() - lastDemoTime > 3000) {
    demo();
    lastDemoTime = millis();
  }
#endif
}

// ########################################### DEMOS>> ###########################################
// Use the demos when you want to generate MIDI traffic without pluging a real MIDI device on the USBHost MIDI port
void demo() {
    byte note = random(30,85);
    //byte note = 60;
    SendNoteOn(note);
    SendControlChange(2, 100);
    //delay(200);
    //SendNoteOff(note);
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
// ########################################### <<DEMOS ###########################################

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
  //Serial.print("Note On, ch="); Serial.print(channel); Serial.print(", note="); Serial.print(note); Serial.print(", velocity="); Serial.println(velocity);
  SendNoteOn(note, velocity, channel);  
}
void onMidiHostNoteOff(byte channel, byte note, byte velocity) {
  SendNoteOff(note);
}
void onMidiHostControlChange(byte channel, byte control, byte value) {
  int ccValue;
  byte breathValueMax = 127;
  byte BV_MIN = 0;
  byte BV_MAX = 127;

  if (control == CC_TRAVELSAX2) {
    currentBreathValue = value;
    ccValue = map( constrain(value, breathValueMin, breathValueMax) , breathValueMin, breathValueMax , BV_MIN , BV_MAX);
  } else {
    ccValue = value;
  }

  SendControlChange(control, ccValue);
}



