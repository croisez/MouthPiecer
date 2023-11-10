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
#define DEBUG_PBSAMPLING     // Uncomment to print HP-filtered values in serial terminal
#define RESET_PB_MINMAX_WITH_BUTTONPRESS  // Uncomment to allow reset of PitchBend min & max values while button is pressed
#define PB_MIN -8192
#define PB_MAX 8191
#define BREATH_VALUE_MIN 19
#define CC_BREATH 2
#define CC_FFT 70
#define F_VALUE_MIN 0.0
#define F_VALUE_MAX 10.0
#define BUTTON_PIN 8
#define PB_ARRAY_SIZE 16
#define SMOOTHING_COEF 0.01
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
double state = 0;
double cutoff_frequency = 1.0;
double gain = cutoff_frequency / (2 * PI * 1000);
double highpass(double input) {
    double retval = input - state;
    state += gain * retval;
    return retval;
}


//___________________________________________________________________________________________________
// https://fiiir.com/
// https://nl.mathworks.com/help/signal/ref/blackmanharris.html
#define COEFF_LEN 173
#define ENERGY_LEN 100
double A[] = {
    0.000000000000000009,
    -0.000000000000000004,
    -0.000000000016941159,
    -0.000000000105163907,
    -0.000000000120989658,
    0.000000000483032403,
    0.000000001619099468,
    0.000000001288545253,
    -0.000000002321030380,
    -0.000000006559953706,
    -0.000000005857743454,
    -0.000000000188240331,
    0.000000003061635534,
    0.000000001301017809,
    0.000000004549254257,
    0.000000019713752288,
    0.000000031656230013,
    0.000000018358352239,
    -0.000000013509938571,
    -0.000000032117930565,
    -0.000000029768688266,
    -0.000000039034193872,
    -0.000000073786322758,
    -0.000000084409110367,
    -0.000000020464217223,
    0.000000079657757525,
    0.000000129855168978,
    0.000000129994548428,
    0.000000171952535102,
    0.000000275075825372,
    0.000001721608253252,
    0.000003937270910520,
    -0.000009970203806001,
    -0.000022516995539984,
    0.000015942137992628,
    0.000062732773528691,
    -0.000000142596166544,
    -0.000119397142732102,
    -0.000058637721267684,
    0.000172641450118456,
    0.000176042813331605,
    -0.000187606918528672,
    -0.000353151446188583,
    0.000117727376176041,
    0.000565675297979197,
    0.000084278585835079,
    -0.000757207998121924,
    -0.000449688355723006,
    0.000839558157566463,
    0.000973522406592537,
    -0.000703344445916908,
    -0.001594862984850984,
    0.000241289836585292,
    0.002184948245840072,
    0.000617581855426486,
    -0.002549977406291130,
    -0.001868184123607336,
    0.002452989854200734,
    0.003382924450134986,
    -0.001702381753182806,
    -0.005007978665886176,
    0.000000228769188417,
    0.006339050867003497,
    0.003071570834396242,
    -0.005649376350180134,
    -0.004735958931876637,
    0.006624802909820017,
    0.009845490707363734,
    -0.004059719774528771,
    -0.016076483379906074,
    -0.006947279175183695,
    0.008851820704905628,
    0.002829168355081443,
    -0.017833243347888883,
    -0.013848793007448044,
    0.022810231944591669,
    0.045150374190854450,
    0.019307698353246801,
    -0.014249006168378958,
    0.002543883781907048,
    0.040310340771088277,
    0.003117693452932625,
    -0.120049057130154041,
    -0.189989837868556999,
    -0.073527227428230854,
    0.159660449099533475,
    0.279988304302934532,
    0.159660449099532087,
    -0.073527227428230674,
    -0.189989837868557415,
    -0.120049057130153902,
    0.003117693452932682,
    0.040310340771088776,
    0.002543883781907286,
    -0.014249006168378913,
    0.019307698353246715,
    0.045150374190854305,
    0.022810231944591738,
    -0.013848793007448046,
    -0.017833243347888869,
    0.002829168355081368,
    0.008851820704905663,
    -0.006947279175183658,
    -0.016076483379905991,
    -0.004059719774528795,
    0.009845490707363767,
    0.006624802909820020,
    -0.004735958931876597,
    -0.005649376350180173,
    0.003071570834396245,
    0.006339050867003509,
    0.000000228769188442,
    -0.005007978665886147,
    -0.001702381753182821,
    0.003382924450134976,
    0.002452989854200722,
    -0.001868184123607326,
    -0.002549977406291141,
    0.000617581855426494,
    0.002184948245840077,
    0.000241289836585297,
    -0.001594862984850996,
    -0.000703344445916905,
    0.000973522406592536,
    0.000839558157566468,
    -0.000449688355723004,
    -0.000757207998121914,
    0.000084278585835088,
    0.000565675297979205,
    0.000117727376176035,
    -0.000353151446188579,
    -0.000187606918528663,
    0.000176042813331598,
    0.000172641450118464,
    -0.000058637721267677,
    -0.000119397142732092,
    -0.000000142596166534,
    0.000062732773528687,
    0.000015942137992625,
    -0.000022516995539988,
    -0.000009970203806006,
    0.000003937270910518,
    0.000001721608253272,
    0.000000275075825387,
    0.000000171952535114,
    0.000000129994548422,
    0.000000129855168971,
    0.000000079657757530,
    -0.000000020464217227,
    -0.000000084409110358,
    -0.000000073786322757,
    -0.000000039034193866,
    -0.000000029768688259,
    -0.000000032117930579,
    -0.000000013509938575,
    0.000000018358352244,
    0.000000031656230006,
    0.000000019713752281,
    0.000000004549254257,
    0.000000001301017819,
    0.000000003061635532,
    -0.000000000188240336,
    -0.000000005857743451,
    -0.000000006559953691,
    -0.000000002321030381,
    0.000000001288545243,
    0.000000001619099476,
    0.000000000483032418,
    -0.000000000120989649,
    -0.000000000105163910,
    -0.000000000016941166,
    -0.000000000000000006,
    0.000000000000000007
};
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

double X[COEFF_LEN] = {0};
double Y[COEFF_LEN] = {0};

double HPass(double in) {
  double out = 0.0;

  for (int i=1; i<COEFF_LEN; i++) {
    X[i-1] = X[i];
    Y[i-1] = Y[i];
  }

  X[COEFF_LEN - 1] = in;

  for (int i=0; i<COEFF_LEN; i++) {
    out += A[COEFF_LEN - i] * X[i] * BlackmanHarris[i];
  }

  Y[COEFF_LEN - 1] = out;
  return out;
}


//___________________________________________________________________________________________________
double ComputeSignalEnergy() {
  double energy = 0.0;

  for (int i = COEFF_LEN - ENERGY_LEN; i < COEFF_LEN; i++) {
    energy += Y[i] * Y[i];
  }

  return energy;
}

//___________________________________________________________________________________________________
void ProcessPbSample() {
  int pb = adc->analogRead(A1);
  double sample_in =  pbMax - pb;// - (pbMax - pbMin)/2 - pbMin;
  double sample = HPass(sample_in);
  //double sample = highpass(sample_in);

  double energy = ComputeSignalEnergy();

  if (energy > 10.0) {
    byte ccValue = fmap( constrain(energy, 0.0, 25.0), 0.0, 25.0, 0, 127);
    SendControlChange(CC_FFT, ccValue);
  }

#ifdef DEBUG_PBSAMPLING
  Serial.print("-3, ");
  Serial.print("3, ");
  Serial.println(energy);
#endif
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
  pbArray[fftIdx++] = pbMax - pb;// - (pbMax - pbMin)/2 - pbMin;

  if (fftIdx >= PB_ARRAY_SIZE) {
    fftIdx = 0;

    Fast4::FFT(pbArray, PB_ARRAY_SIZE);

    //For each frequency band, low-pass and save energy in array 
    for (int i = 0; i < PB_ARRAY_SIZE; i++) {
      if (i==4) F[i] = LowPass( sqrt(pbArray[i].re() * pbArray[i].re() + pbArray[i].im() * pbArray[i].im()) , i);
      else      F[i] = sqrt(pbArray[i].re() * pbArray[i].re() + pbArray[i].im() * pbArray[i].im());
    }

    //Compare energy in each band, and try to isolate a wining frequency
    //for (int i = 0; i < PB_ARRAY_SIZE / 2; i++) {}
    if (F[4] > 5.0) {
      byte ccValue = fmap( constrain(F[4], F_VALUE_MIN, F_VALUE_MAX), F_VALUE_MIN, F_VALUE_MAX, 0, 127);
      SendControlChange(CC_FFT, ccValue);
    }

#ifdef DEBUG_FFT
    debugPlot(String("Max"), 50, 1);
#endif
    for (int i = 0; i < PB_ARRAY_SIZE/2 + 1; i++) {
#ifdef DEBUG_FFT
      if (i==0) debugPlotf(String(i), F[i]/50, 1);
      else debugPlotf(String(i), F[i], 1);
#endif
    }
#ifdef DEBUG_FFT
    debugPlot(String("Min"), 0, 0);
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
    //ProcessFFT();
    ProcessPbSample();
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
