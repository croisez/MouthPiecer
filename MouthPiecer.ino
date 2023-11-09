/*
 *      MOUTHPIECE'R  MIDI  DEVICE : device for adding CC to your TravelSax 
 *      Coded by LM CROISEZ, November 2023
 *      License: LGPL
 */

#include <USBHost_t36.h>
#include <ADC.h>
#include <ADC_util.h>
#include <MIDI.h>
#include <Fast4ier.h>
#include <complex.h>

// Your configuration here =>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
#define DOUBLE                 //if you have alot of RAM, uncoment the following for extra FFT precision.
//#define USE_DIN_FOR_MIDIOUT  // You can send MIDIOUT through DIN-5 plug, or through usbMidi device. Use USE_DIN_FOR_MIDIOUT define for this purpose.
//#define USE_ETHERNET         // You can use a Teensy4.1 Ethernet adapter to have access to web service for configuration and debug. Use USE_ETHERNET define for this purpose.
//#define USE_DEMO             // You can generate fake MIDI traffic as a demo, when no MIDI device is connected on the USBHost MIDI port. Use USE_DEMO define for this purpose.
//#define DEBUG_PITCHBEND      // Uncomment to print PitchBend values in serial terminal
//#define DEBUG_MIDI_MSG       // Uncomment to print MIDI messages in serial terminal
#define DEBUG_FFT            // Uncomment to print FFT values in serial terminal
#define RESET_PB_MINMAX_WITH_BUTTONPRESS  // Uncomment to allow reset of PitchBend min & max values while button is pressed
#define PB_MIN -8192
#define PB_MAX 8191
#define BREATH_VALUE_MIN 19
#define CC_BREATH 2
#define CC_FFT 70
#define F_VALUE_MIN 0.0
#define F_VALUE_MAX 30.0
#define BUTTON_PIN 8
#define PB_ARRAY_SIZE 8
#define SMOOTHING_COEF 0.3
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<= Your configuration here

#define VERSION    "1"
#define SUBVERSION "5"

//#############################################################################################################################################
#ifdef USE_DIN_FOR_MIDIOUT
  MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, MIDI);
#endif

ADC *adc = new ADC();
USBHost usbhost;
USBHub hub1(usbhost);
USBHub hub2(usbhost);
MIDIDevice usbhostMIDI(usbhost);

int pbMin = 2000;
int pbMax = -2000;
int pbAnalog = 0;
int oldpbAnalog = 0;
long lastPitchbendTime = 0;
byte LedPin = 13;

complex pbArray[PB_ARRAY_SIZE];
int fftIdx = 0;
long lastFftTime = 0;
double F[PB_ARRAY_SIZE];
double fmem[PB_ARRAY_SIZE];

boolean connectedToEth = false;
long lastEthernetClientTime = 0;
long lastDemoTime = 0;
byte breathValueMin = BREATH_VALUE_MIN;
byte currentBreathValue;
long lastButtonTime = 0;

//___________________________________________________________________________________________________
void debugPlot(String varname, int val, int next = 0){
  Serial.print(varname); Serial.print(":");  Serial.print(val);
  if (next) {Serial.print(", ");} else {Serial.print("\n");}
}

//___________________________________________________________________________________________________
void debugPlotf(String varname, double val, int next = 0){
  Serial.print(varname); Serial.print(":");  Serial.print(val);
  if (next) {Serial.print(", ");} else {Serial.print("\n");}
}

//___________________________________________________________________________________________________
void SendNoteOn(byte note, byte velocity = 127, byte channel = 1) {
#ifdef DEBUG_MIDI_MSG
  Serial.println("NOTEON: " + String(note) + " velocity=" + String(velocity));
#endif

#ifdef USE_DIN_FOR_MIDIOUT
  MIDI.sendNoteOn(note, velocity, channel);
#else
  usbMIDI.sendNoteOn(note, velocity, channel);
#endif
}

//___________________________________________________________________________________________________
void SendNoteOff(byte note, byte channel = 1) {
#ifdef DEBUG_MIDI_MSG
  Serial.println("NOTEOFF: " + String(note));
#endif

#ifdef USE_DIN_FOR_MIDIOUT
  MIDI.sendNoteOff(note, 0, channel);
#else
  usbMIDI.sendNoteOff(note, 0, channel);
#endif
}

//___________________________________________________________________________________________________
void SendControlChange(byte cc, byte value, byte channel = 1) {
//#ifdef DEBUG_MIDI_MSG
//  Serial.println("CC" + String(cc) + " = " + String(value));
//#endif

#ifdef USE_DIN_FOR_MIDIOUT
  MIDI.sendControlChange(cc, value, channel);
#else
  usbMIDI.sendControlChange(cc, value, channel);
#endif
}

void SendPitchBend(int pitchBend, byte channel = 1) {
//#ifdef DEBUG_MIDI_MSG
//  Serial.println("Bend = " + String(pitchBend));
//#endif

#ifdef USE_DIN_FOR_MIDIOUT
  MIDI.sendPitchBend(pitchBend, channel);
#else
  usbMIDI.sendPitchBend(pitchBend, channel);
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

//___________________________________________________________________________________________________
void onMidiHostNoteOn(byte channel, byte note, byte velocity) {
  SendNoteOn(note, velocity, channel);  
}

//___________________________________________________________________________________________________
void onMidiHostNoteOff(byte channel, byte note, byte velocity) {
  SendNoteOff(note);
}

//___________________________________________________________________________________________________
void onMidiHostControlChange(byte channel, byte control, byte value) {
  int ccValue;
  byte breathValueMax = 127;
  byte BV_MIN = 0;
  byte BV_MAX = 127;

  if (control == CC_BREATH) {
    currentBreathValue = value;
    ccValue = map( constrain(value, breathValueMin, breathValueMax) , breathValueMin, breathValueMax , BV_MIN , BV_MAX);
  } else {
    ccValue = value;
  }

  SendControlChange(control, ccValue);
}

//___________________________________________________________________________________________________
void CapturePitchBend() {
  pbAnalog = adc->analogRead(A1);
  adc->resetError();

  if (pbMin > pbAnalog) pbMin = pbAnalog;
  if (pbMax < pbAnalog) pbMax = pbAnalog;

//  if (abs(pbAnalog - oldpbAnalog) > 0) {
    digitalWrite(LedPin, HIGH);
    int pbValue = map( constrain(pbAnalog, pbMin, pbMax) , pbMin, pbMax , PB_MAX , PB_MIN);
    SendPitchBend(pbValue);
    digitalWrite(LedPin, LOW);
    
#ifdef DEBUG_PITCHBEND  
    //debugPlot("PB_MIN", PB_MIN, 1); debugPlot("PB_MAX", PB_MAX, 1); debugPlot("pitchBend", pitchBend);
    Serial.println(String(pbMin) + "," + String(pbAnalog) + "," + String(pbMax));
    //Serial.println(String(PB_MIN) + "," + String(pbValue) + "," + String(PB_MAX));
#endif
//  }

  oldpbAnalog = pbAnalog;
}

//___________________________________________________________________________________________________
double fmap(double x, double in_min, double in_max, double out_min, double out_max)
{
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

//___________________________________________________________________________________________________
double LowPass(double in, int idx) {
  float out = fmem[idx] + SMOOTHING_COEF * (in - fmem[idx]);
  fmem[idx] = out;
  return out;
}

//___________________________________________________________________________________________________
void ProcessFFT() {
  int pb = adc->analogRead(A1);
  pbArray[fftIdx++] = pb - (pbMax - pbMin)/2 - pbMin;

  if (fftIdx >= PB_ARRAY_SIZE) {
    fftIdx = 0;

    Fast4::FFT(pbArray, PB_ARRAY_SIZE);

    //For each frequency band, low-pass and save energy in array 
    for (int i = 0; i < PB_ARRAY_SIZE; i++) {
      F[i] = LowPass( sqrt(pbArray[i].re() * pbArray[i].re() + pbArray[i].im() * pbArray[i].im()) , i);
    }

    //Compare energy in each band, and try to isolate a wining frequency
    //for (int i = 0; i < PB_ARRAY_SIZE / 2; i++) {}
    byte ccValue = fmap( constrain(F[4], F_VALUE_MIN, F_VALUE_MAX), F_VALUE_MIN, F_VALUE_MAX, 0, 127);
    SendControlChange(CC_FFT, ccValue);

#ifdef DEBUG_FFT
    debugPlot(String("Min"), 0, 1);
#endif
    for (int i = PB_ARRAY_SIZE / 2; i < PB_ARRAY_SIZE; i++) {
#ifdef DEBUG_FFT
      debugPlotf(String(i), F[i], 1);
#endif
    }
#ifdef DEBUG_FFT
    debugPlot(String("Max"), 15, 0);
#endif
  }
}

//___________________________________________________________________________________________________
void ProcessMidiLoop() {
  usbhost.Task();
  usbhostMIDI.read();

#ifndef USE_DIN_FOR_MIDIOUT
  usbMIDI.read();
#endif

  if (millis() - lastFftTime > 1) {
    ProcessFFT();
    lastFftTime = millis();
  }

  if (millis() - lastPitchbendTime > 4) { //Do not send another pitch bend message sooner than 4ms.
    CapturePitchBend();
    lastPitchbendTime = millis();
  }
}

// ########################################### S E T U P ###########################################
void setup() {
  Serial.begin(115200);
  Serial.println("\r\n####  MouthPiecer v" + String(VERSION) + "." + String(SUBVERSION) + "  ####");

  pinMode(LedPin,OUTPUT);
  pinMode(A1, INPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  adc->adc1->setAveraging(0);                                     // set number of averages
  adc->adc1->setResolution(12);                                   // set bits of resolution
  adc->adc1->setConversionSpeed(ADC_CONVERSION_SPEED::MED_SPEED); // change the conversion speed
  adc->adc1->setSamplingSpeed(ADC_SAMPLING_SPEED::MED_SPEED);     // change the sampling speed

  delay(2000); // Wait at least 1.5 seconds before turning USBHost ON.

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
      breathValueMin = currentBreathValue + 2;
      Serial.println("Minimum breath set to value " + String(breathValueMin));
#ifdef RESET_PB_MINMAX_WITH_BUTTONPRESS
      pbMin = 2000;
      pbMax = -2000;
#endif
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
