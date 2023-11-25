/*
 *      MOUTHPIECE'R  MIDI  DEVICE : device for adding CC to your TravelSax 
 *      Coded by LM CROISEZ, November 2023
 *      License: LGPL
 */

#include <USBHost_t36.h>
#include <ADC.h>
#include <ADC_util.h>
#include <MIDI.h>

// Your configuration here =>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
#define DOUBLE  //if you have alot of RAM, uncoment the following for extra FFT precision.
//#define USE_DIN_FOR_MIDIOUT  // You can send MIDIOUT through DIN-5 plug, or through usbMidi device. Use USE_DIN_FOR_MIDIOUT define for this purpose.
//#define USE_ETHERNET         // You can use a Teensy4.1 Ethernet adapter to have access to web service for configuration and debug. Use USE_ETHERNET define for this purpose.
//#define USE_DEMO             // You can generate fake MIDI traffic as a demo, when no MIDI device is connected on the USBHost MIDI port. Use USE_DEMO define for this purpose.
//#define DEBUG_PITCHBEND      // Uncomment to print PitchBend values in serial terminal
//#define DEBUG_MIDI_MSG       // Uncomment to print MIDI messages in serial terminal
//#define DEBUG_CCSAMPLING     // Uncomment to print HP-filtered values in serial terminal
//#define DEBUG_BUTTON_EVENTS  // Uncomment to debug button events

#define RESET_PB_MINMAX_WITH_BUTTONPRESS  // Uncomment to allow reset of PitchBend min & max values while button is pressed
#define BREATH_VALUE_MIN 19
#define CC_BREATH 2
#define CC_SAMPLING 115
#define ENERGY_THRESH 40.0
#define ENERGY_MAX 150.0
#define BUTTON_PIN 8
#define PB_ARRAY_SIZE 16
#define BUTTON_SCANNING_SPEED_MS 20
#define BUTTON_LONGPRESS_DURATION 30
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<= Your configuration here

#define VERSION "1"
#define SUBVERSION "6"

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

long lastSamplingTime = 0;
double energyMax = 0.0;

boolean connectedToEth = false;
long lastEthernetClientTime = 0;
long lastDemoTime = 0;
byte breathValueMin = BREATH_VALUE_MIN;
byte currentBreathValue;
long lastButtonTimer = 0;

boolean buttonStateMem = false;
boolean buttonState = false;
boolean isButtonRising = false;
boolean isButtonFalling = false;
byte btnCount = 0;
byte btnLong = 0;
int program = 0;

//___________________________________________________________________________________________________
void debugPlot(String varname, int val, int next = 0) {
  Serial.print(varname);
  Serial.print(":");
  Serial.print(val);
  if (next) {
    Serial.print(", ");
  } else {
    Serial.print("\n");
  }
}

//___________________________________________________________________________________________________
void debugPlotf(String varname, double val, int next = 0) {
  Serial.print(varname);
  Serial.print(":");
  Serial.print(val);
  if (next) {
    Serial.print(", ");
  } else {
    Serial.print("\n");
  }
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
#ifdef DEBUG_MIDI_MSG
  if (cc == CC_SAMPLING) {
    Serial.println("CC" + String(cc) + " = " + String(value));
  }
#endif

#ifdef USE_DIN_FOR_MIDIOUT
  MIDI.sendControlChange(cc, value, channel);
#else
  usbMIDI.sendControlChange(cc, value, channel);
#endif
}

//___________________________________________________________________________________________________
void SendProgramChange(byte value, byte channel = 1) {
#ifdef DEBUG_MIDI_MSG
    Serial.println("Program changed to " + String(value));
#endif
Serial.println("Program changed to " + String(value));
#ifdef USE_DIN_FOR_MIDIOUT
  MIDI.sendProgramChange(value, channel);
#else
  usbMIDI.sendProgramChange(value, channel);
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

boolean buttonGet() {
  boolean bs = (!digitalRead(BUTTON_PIN));
  if (bs) {
    buttonState = true;
    btnLong += 1;
    //Serial.println(btnLong);
  } else {
    buttonState = false;
  }
  return bs;
}

void buttonRisingEvent() {
#ifdef DEBUG_BUTTON_EVENTS
  Serial.println("RISING");
#endif
}
void buttonFallingEvent() {
  program += 1;
  if (program > 127) program = 0;
  if (program < 0)   program = 127;
  SendProgramChange((byte)program);
#ifdef DEBUG_BUTTON_EVENTS
  Serial.println("FALLING");
#endif
}
void buttonIdleEvent() {}
void buttonClickEvent() {
#ifdef DEBUG_BUTTON_EVENTS
  Serial.println("BTNCLK");
#endif
}
void buttonDblClickEvent() {
#ifdef DEBUG_BUTTON_EVENTS
  Serial.println("BTNDBLCLK");
#endif
}
void buttonLongEvent() {
  breathValueMin = currentBreathValue + 2;
  Serial.println("Minimum breath set to value " + String(breathValueMin));
#ifdef RESET_PB_MINMAX_WITH_BUTTONPRESS
  pbMin = 2000;
  pbMax = -2000;
  energyMax = 0;
#endif

#ifdef DEBUG_BUTTON_EVENTS
  Serial.println("BTNLONG");
#endif
}

void ProcessButtonEvents() {
  if (millis() - lastButtonTimer > BUTTON_SCANNING_SPEED_MS) {
    if (buttonStateMem == false && buttonState == true) {        //Handles button rising edge
      isButtonRising = true;
      isButtonFalling = false;
      buttonRisingEvent();
    } else if (buttonStateMem == true && buttonState == false) { //Handles button falling edge
      if (btnLong >= BUTTON_LONGPRESS_DURATION + 1) {
        btnLong = 0;
        buttonStateMem = false; buttonState = false;
      } else {
        btnCount += 1;
        isButtonRising = false;
        isButtonFalling = true;
        buttonFallingEvent();
        btnLong = 0;
      }
    } else {
      isButtonRising = false;
      isButtonFalling = false;
      buttonIdleEvent();
    }
    if (btnLong == BUTTON_LONGPRESS_DURATION) {
      btnLong += 1;
      buttonLongEvent();
    }

    lastButtonTimer = millis();
    buttonStateMem = buttonState;
    buttonState = buttonGet();
  }
}

// ########################################### DEMOS>> ###########################################
// Use the demos when you want to generate MIDI traffic without pluging a real MIDI device on the USBHost MIDI port
void demo() {
  byte note = random(30, 85);
  //byte note = 60;
  SendNoteOn(note);
  SendControlChange(2, 100);
  //delay(200);
  //SendNoteOff(note);
}
void demo2() {
  int note = random(30, 85);
  SendNoteOn(note);
  for (int i = 0; i < 400; i++) {
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
    ccValue = map(constrain(value, breathValueMin, breathValueMax), breathValueMin, breathValueMax, BV_MIN, BV_MAX);
  } else {
    ccValue = value;
  }

  SendControlChange(control, ccValue);
}

#define PB_MIN -8192
#define PB_MAX 8191

//___________________________________________________________________________________________________
void CapturePitchBend() {
  pbAnalog = adc->analogRead(A1);
  adc->resetError();

  if (pbMin > pbAnalog) pbMin = pbAnalog;
  if (pbMax < pbAnalog) pbMax = pbAnalog;

  //  if (abs(pbAnalog - oldpbAnalog) > 0) {
  digitalWrite(LedPin, HIGH);
  int pbValue = map(constrain(pbAnalog, pbMin, pbMax), pbMin, pbMax, PB_MAX, PB_MIN);
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
double fmap(double x, double in_min, double in_max, double out_min, double out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}



//___________________________________________________________________________________________________
// https://fiiir.com/
#define COEFF_LEN 173
#define ENERGY_LEN 100
double A[] = {
  0.000000000000000000,
  0.000000000000000001,
  0.000000000011005136,
  -0.000000000037399939,
  -0.000000000115040338,
  0.000000000097820853,
  0.000000000192401411,
  0.000000000070513039,
  0.000000000643453097,
  0.000000000510895091,
  -0.000000000271288510,
  0.000000000233369442,
  -0.000000001017766328,
  -0.000000002549608906,
  -0.000000001130934244,
  -0.000000002414101115,
  -0.000000002048409709,
  0.000000002980398270,
  0.000000002520459322,
  0.000000005218478885,
  0.000000010992156704,
  0.000000005011797464,
  0.000000003993237857,
  0.000000002610149455,
  -0.000000014996407029,
  -0.000000019308499226,
  -0.000000025227457460,
  -0.000000045626855272,
  -0.000000038346164443,
  -0.000000037398566824,
  -0.000000962825343703,
  0.000006335867833853,
  -0.000008625749837479,
  -0.000008282518446142,
  0.000040880409288012,
  -0.000051614506959335,
  -0.000000007048442537,
  0.000098199854356661,
  -0.000151489035274188,
  0.000063115696778774,
  0.000151183257273283,
  -0.000318819741787379,
  0.000229333629883962,
  0.000146009995366490,
  -0.000538852847317047,
  0.000545198476395841,
  0.000000042722945174,
  -0.000755388236647992,
  0.001035693118424937,
  -0.000390369642359173,
  -0.000857472249085699,
  0.001676698247233005,
  -0.001128089680097489,
  -0.000676452157144680,
  0.002365537343527685,
  -0.002279149921138568,
  0.000000031778751631,
  0.002898081198135451,
  -0.003838622918791428,
  0.001350590666961685,
  0.002904233313793900,
  -0.005624329733716332,
  0.003868278993228014,
  0.002659897715424061,
  -0.006475940223590691,
  0.008092340573824578,
  0.001027128446031523,
  -0.008242823011106366,
  0.009971263193903946,
  -0.007146221956632105,
  -0.013594613562336006,
  0.009633117581983286,
  -0.016152862350166382,
  -0.008702539479706114,
  0.024246096998089627,
  -0.009272318396923780,
  0.018447098565562356,
  0.049164305784063139,
  -0.012524038598875666,
  0.026397698487138667,
  0.024230100521312546,
  -0.094859310637520428,
  -0.020799810191169249,
  -0.068980697097135404,
  -0.282437437236021049,
  0.104575091753731780,
  0.539987425529383613,
  0.104575091753731142,
  -0.282437437236021049,
  -0.068980697097135807,
  -0.020799810191169048,
  -0.094859310637519900,
  0.024230100521312574,
  0.026397698487138833,
  -0.012524038598875624,
  0.049164305784062855,
  0.018447098565562377,
  -0.009272318396923811,
  0.024246096998089599,
  -0.008702539479706046,
  -0.016152862350166344,
  0.009633117581983347,
  -0.013594613562335968,
  -0.007146221956632052,
  0.009971263193903939,
  -0.008242823011106397,
  0.001027128446031537,
  0.008092340573824530,
  -0.006475940223590684,
  0.002659897715424086,
  0.003868278993228015,
  -0.005624329733716292,
  0.002904233313793913,
  0.001350590666961683,
  -0.003838622918791428,
  0.002898081198135425,
  0.000000031778751627,
  -0.002279149921138574,
  0.002365537343527693,
  -0.000676452157144667,
  -0.001128089680097514,
  0.001676698247233004,
  -0.000857472249085685,
  -0.000390369642359177,
  0.001035693118424936,
  -0.000755388236647987,
  0.000000042722945184,
  0.000545198476395843,
  -0.000538852847317034,
  0.000146009995366490,
  0.000229333629883962,
  -0.000318819741787382,
  0.000151183257273275,
  0.000063115696778787,
  -0.000151489035274187,
  0.000098199854356674,
  -0.000000007048442533,
  -0.000051614506959342,
  0.000040880409288011,
  -0.000008282518446148,
  -0.000008625749837478,
  0.000006335867833858,
  -0.000000962825343685,
  -0.000000037398566816,
  -0.000000038346164441,
  -0.000000045626855271,
  -0.000000025227457467,
  -0.000000019308499231,
  -0.000000014996407013,
  0.000000002610149459,
  0.000000003993237832,
  0.000000005011797470,
  0.000000010992156705,
  0.000000005218478883,
  0.000000002520459315,
  0.000000002980398264,
  -0.000000002048409706,
  -0.000000002414101118,
  -0.000000001130934248,
  -0.000000002549608903,
  -0.000000001017766335,
  0.000000000233369446,
  -0.000000000271288504,
  0.000000000510895087,
  0.000000000643453099,
  0.000000000070513046,
  0.000000000192401420,
  0.000000000097820853,
  -0.000000000115040335,
  -0.000000000037399936,
  0.000000000011005136,
  0.000000000000000004,
  0.000000000000000004
};
// https://www.irjet.net/archives/V5/i10/IRJET-V5I10118.pdf
// https://nl.mathworks.com/help/signal/ref/blackmanharris.html
double BlackmanHarris[] = {
  0.0001,
  0.0001,
  0.0001,
  0.0002,
  0.0004,
  0.0006,
  0.0008,
  0.0011,
  0.0015,
  0.0020,
  0.0026,
  0.0032,
  0.0040,
  0.0050,
  0.0061,
  0.0074,
  0.0089,
  0.0106,
  0.0126,
  0.0148,
  0.0173,
  0.0202,
  0.0234,
  0.0270,
  0.0309,
  0.0353,
  0.0402,
  0.0456,
  0.0514,
  0.0578,
  0.0648,
  0.0724,
  0.0806,
  0.0895,
  0.0990,
  0.1092,
  0.1201,
  0.1318,
  0.1442,
  0.1573,
  0.1712,
  0.1859,
  0.2013,
  0.2175,
  0.2344,
  0.2521,
  0.2705,
  0.2896,
  0.3094,
  0.3299,
  0.3509,
  0.3726,
  0.3948,
  0.4176,
  0.4408,
  0.4643,
  0.4883,
  0.5125,
  0.5369,
  0.5614,
  0.5861,
  0.6107,
  0.6352,
  0.6596,
  0.6838,
  0.7076,
  0.7311,
  0.7540,
  0.7764,
  0.7981,
  0.8191,
  0.8393,
  0.8586,
  0.8769,
  0.8942,
  0.9104,
  0.9254,
  0.9392,
  0.9516,
  0.9628,
  0.9725,
  0.9808,
  0.9877,
  0.9931,
  0.9969,
  0.9992,
  1.0000,
  0.9992,
  0.9969,
  0.9931,
  0.9877,
  0.9808,
  0.9725,
  0.9628,
  0.9516,
  0.9392,
  0.9254,
  0.9104,
  0.8942,
  0.8769,
  0.8586,
  0.8393,
  0.8191,
  0.7981,
  0.7764,
  0.7540,
  0.7311,
  0.7076,
  0.6838,
  0.6596,
  0.6352,
  0.6107,
  0.5861,
  0.5614,
  0.5369,
  0.5125,
  0.4883,
  0.4643,
  0.4408,
  0.4176,
  0.3948,
  0.3726,
  0.3509,
  0.3299,
  0.3094,
  0.2896,
  0.2705,
  0.2521,
  0.2344,
  0.2175,
  0.2013,
  0.1859,
  0.1712,
  0.1573,
  0.1442,
  0.1318,
  0.1201,
  0.1092,
  0.0990,
  0.0895,
  0.0806,
  0.0724,
  0.0648,
  0.0578,
  0.0514,
  0.0456,
  0.0402,
  0.0353,
  0.0309,
  0.0270,
  0.0234,
  0.0202,
  0.0173,
  0.0148,
  0.0126,
  0.0106,
  0.0089,
  0.0074,
  0.0061,
  0.0050,
  0.0040,
  0.0032,
  0.0026,
  0.0020,
  0.0015,
  0.0011,
  0.0008,
  0.0006,
  0.0004,
  0.0002,
  0.0001,
  0.0001,
  0.0001
};

double X[COEFF_LEN] = { 0 };
double Y[COEFF_LEN] = { 0 };

double FIR(double in) {
  double out = 0.0;

  for (int i = 1; i < COEFF_LEN; i++) {
    X[i - 1] = X[i];
    Y[i - 1] = Y[i];
  }

  X[COEFF_LEN - 1] = in;

  for (int i = 0; i < COEFF_LEN; i++) {
    out += A[COEFF_LEN - i] * X[i] * BlackmanHarris[i];
  }

  Y[COEFF_LEN - 1] = out;
  return out;
}


//___________________________________________________________________________________________________
double ComputeSignalEnergy() {
  double energy = 0.0;
  for (int i = COEFF_LEN - ENERGY_LEN; i < COEFF_LEN; i++) { energy += Y[i] * Y[i]; }
  return energy;
}

#define CC_MIN 0
#define CC_MAX 127

//___________________________________________________________________________________________________
void ProcessPbSamples() {
  byte ccValue = 0;
  int pb = adc->analogRead(A1);
  double sample_in = pbMax - pb;

  FIR(sample_in);                         //Band-pass FIR filter, to isolate energy coming from singing voice band
  double energy = ComputeSignalEnergy();  //Estimate energy in the filtered band, to allow to take decision to send CC or not.

  //if (energyMax < energy && energy < 250.0) energyMax = energy;
  energyMax = ENERGY_MAX;

  if (energy > ENERGY_THRESH) {
    ccValue = fmap(constrain(energy, ENERGY_THRESH, energyMax), ENERGY_THRESH, energyMax, CC_MIN, CC_MAX);
    SendControlChange(CC_SAMPLING, ccValue);
  }

#ifdef DEBUG_CCSAMPLING
  Serial.print("0, ");
  Serial.print(energy);
  Serial.print(", ");
  Serial.print(energyMax);
  Serial.print(", ");
  Serial.println(ccValue);
#endif
}


//___________________________________________________________________________________________________
void ProcessMidiLoop() {
  usbhost.Task();
  usbhostMIDI.read();

#ifndef USE_DIN_FOR_MIDIOUT
  usbMIDI.read();
#endif

  if (millis() - lastSamplingTime > 1) {
    ProcessPbSamples();
    lastSamplingTime = millis();
  }

  if (millis() - lastPitchbendTime > 4) {  //Do not send another pitch bend message sooner than 4ms.
    CapturePitchBend();
    lastPitchbendTime = millis();
  }
}

// ########################################### S E T U P ###########################################
void setup() {
  Serial.begin(115200);
  Serial.println("\r\n####  MouthPiecer v" + String(VERSION) + "." + String(SUBVERSION) + "  ####");

  pinMode(LedPin, OUTPUT);
  pinMode(A1, INPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  adc->adc1->setAveraging(0);                                      // set number of averages
  adc->adc1->setResolution(12);                                    // set bits of resolution
  adc->adc1->setConversionSpeed(ADC_CONVERSION_SPEED::MED_SPEED);  // change the conversion speed
  adc->adc1->setSamplingSpeed(ADC_SAMPLING_SPEED::MED_SPEED);      // change the sampling speed

  delay(2000);  // Wait at least 1.5 seconds before turning USBHost ON.

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

  digitalWrite(LedPin, HIGH);
  delay(200);
  digitalWrite(LedPin, LOW);
  delay(200);
  digitalWrite(LedPin, HIGH);
  delay(200);
  digitalWrite(LedPin, LOW);
  delay(200);
  digitalWrite(LedPin, HIGH);
  delay(200);
  digitalWrite(LedPin, LOW);
  delay(200);

  Serial.println("Starting MouthPiecer main loop");
#ifdef USE_DEMO
  Serial.println("DEMO mode activated");
#endif
}

// ########################################### L O O P ###########################################
void loop() {
#ifdef USE_ETHERNET
  if (connectedToEth && (millis() - lastEthernetClientTime > 500)) {  //500ms => do not give too much priority to web screen refresh
    listenForEthernetClients();
    lastEthernetClientTime = millis();
  }
#endif

  ProcessMidiLoop();
  ProcessButtonEvents();

#ifdef USE_DEMO
  if (millis() - lastDemoTime > 3000) {
    demo();
    lastDemoTime = millis();
  }
#endif
}
