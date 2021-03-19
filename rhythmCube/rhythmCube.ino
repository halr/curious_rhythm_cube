/**
 * Control rhythm pattern and tempo using gestures.
 * Rhythm patterns play over BLE as MIDI.
 * 
 * Board: Arduino MBED OS Boards -> Arduino Nano 33 BLE
 * 
 * Libraries:
 * Adafruit_NeoPixel 
 * Arduino_APDS9960
 * ArduinoBLE - https://www.arduino.cc/en/Reference/ArduinoBLE
 * 
 * NeoPixel Connections:
 * Ground = Black
 * 3.3V = red
 * Pin 10 = White
 */
#include <Adafruit_NeoPixel.h>
#include <Arduino_APDS9960.h>
#include <ArduinoBLE.h>

unsigned long previousMillis = 0;
int bpm = 60;  // beats per minute
int stepIndex = 0;
float stepDuration = 0; // duration of a quarter beat in ms, initially set in setup()
const int tempoDelta = 24; 

// 16 LED neopixel ring
const int npNum = 16;
const int npPin = 10;
const int npBrigntness = 31;  // range 0 (off) to 255 (max brightness)
Adafruit_NeoPixel ring = Adafruit_NeoPixel(npNum, npPin, NEO_GRB + NEO_KHZ800);

// MIDI
byte midiMsg[] = {0x80, 0x80, 0x00, 0x00, 0x00};

// BLE
// set up the MIDI service and MIDI message characteristic:
BLEService midiService("03B80E5A-EDE8-4B33-A751-6CE34EC4C700");
BLECharacteristic midiCharacteristic("7772E5DB-3868-4112-A1A9-F2669D106BF3",
                                     BLEWrite | BLEWriteWithoutResponse |
                                     BLENotify | BLERead, sizeof(midiMsg));

// rhythm sequences:
const int nSeq = 6;
int iSeq = 0;
int sequence[][16] = {
 //00, 01, 02, 03, 04, 05, 05, 07, 08, 09, 10, 11, 12, 13, 14, 15 = steps
 // 1               2               3               4             = beats
  {72,  0,  0,  0,  0,  0,  0,  0, 72,  0,  0,  0,  0,  0,  0,  0}, // tik tik
  {60,  0,  0,  0, 64,  0,  0,  0, 64,  0,  0,  0, 64,  0,  0,  0}, // accent
  {60,  0, 64,  0, 62,  0, 64,  0, 60,  0, 64,  0, 62,  0, 64,  0}, // blues groove
  {60,  0, 64,  0, 62,  0, 64,  0, 60,  0, 60,  0, 62,  0, 64,  0}, // ringo groove
  {60,  0, 64,  0, 62,  0, 64, 67, 60, 67, 64,  0, 62,  0, 65, 64}, // zig zag beat
  {60, 65, 64, 60, 62, 72, 60, 65, 65,  0, 64,  0, 62, 65, 60,  0}  // impluse 808
};  // notes based on the Impulse 808 drum kit in Ableton Live 10
int lastNote = 0;

// color mapping used in colorForNote() function
uint32_t bass60c =   ring.Color(255,   0,   0); // red
uint32_t snare62c =  ring.Color(255,   0, 255); // magenta
uint32_t hihato64c = ring.Color(  0, 128,   0); // green
uint32_t hihatc65c = ring.Color(  0, 255,   0); // brighter green
uint32_t clap67c =   ring.Color(  0,   0, 255); // blue
uint32_t rim72c =    ring.Color(200, 200,  20); // yellow

void setup() {
  stepDuration = clacSubBeatDuration(bpm);
  
  // initialize serial communication
  //Serial.begin(9600);
  // initialize built in LED:
  pinMode(LED_BUILTIN, OUTPUT);
  // set up neopixels
  ring.begin();
  ring.setBrightness(npBrigntness);    // Batteries have limited sauce
  
  if (!APDS.begin()) {
    //Serial.println("Error initializing APDS9960 sensor.");
    //digitalWrite(RED, LOW); // turn the LED on (HIGH is the voltage level)
    while (true); // Stop forever
  }
  // for setGestureSensitivity(..) a value between 1 and 100 is required.
  // Higher values makes the gesture recognition more sensible but less accurate
  // (a wrong gesture may be detected). Lower values makes the gesture recognition
  // more accurate but less sensible (some gestures may be missed).
  // Default is 80
  //APDS.setGestureSensitivity(80);
  
  // Initialize BLE:
  if (!BLE.begin()) {
    Serial.println("starting BLE failed!");
    while (true);
  }
  // set local name and advertised service for BLE:
  BLE.setLocalName("MIDI_BLE");
  BLE.setAdvertisedService(midiService);

  // add the characteristic and service:
  midiService.addCharacteristic(midiCharacteristic);
  BLE.addService(midiService);

  // start advertising
  BLE.advertise();
}

void loop() {
  // check if a gesture reading is available
  if (APDS.gestureAvailable()) {
    int gesture = APDS.readGesture();
    switch (gesture) {
      case GESTURE_UP:
        bpm = bpm+tempoDelta;
        stepDuration = clacSubBeatDuration(bpm);
        break;
      case GESTURE_DOWN:
        bpm = bpm-tempoDelta;
        stepDuration = clacSubBeatDuration(bpm);
        break;
      case GESTURE_LEFT:
        iSeq++;
        // keep the note in the range from 0 - nSeq using modulo:
        iSeq = iSeq % nSeq;
        break;
      case GESTURE_RIGHT:
        iSeq--;
        if (iSeq < 0) {
          iSeq = nSeq-1;
        } else {
          // keep the note in the range from 0 - nSeq using modulo:
          iSeq = iSeq % nSeq;
        }
        break;
      default:
        // ignore
        break;
    }
  }
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= stepDuration) {
    previousMillis = currentMillis;

    int note = sequence[iSeq][stepIndex];
    ring.clear();
    if ((lastNote != 0) || (note != 0)) {
      // wait for a BLE central
      BLEDevice central = BLE.central();
      // if a central is connected to the peripheral:
      if (central) {
        // turn on LED to indicate connection:
        digitalWrite(LED_BUILTIN, HIGH);
        if (lastNote != 0) {
          // turn the note off:
          midiCommand(0x80, lastNote, 0);
        }
        if (note != 0) {
          // play note
          midiCommand(0x90, note, 127);
          // light the led
          uint32_t noteColor = colorForNote(note);
          ring.setPixelColor(stepIndex, noteColor);
        }
      } // end if connected
    } // end if we need to send MIDI
    // when the central disconnects, turn off the LED:
    digitalWrite(LED_BUILTIN, LOW);
    lastNote = note;
    ring.show();
    // increment the note number for next time through the loop:
    stepIndex++;
    // keep the note in the range from 0 - 15 using modulo:
    stepIndex = stepIndex % npNum;
  } //end step
}

float clacSubBeatDuration(int beatsPerMinte) {
  return (60.0 / beatsPerMinte) * 62.5;
}

// send a 3-byte midi message
void midiCommand(byte cmd, byte data1, byte  data2) {
  // MIDI data goes in the last three bytes of the midiMsg array:
  midiMsg[2] = cmd;
  midiMsg[3] = data1;
  midiMsg[4] = data2;
  midiCharacteristic.setValue(midiMsg, sizeof(midiMsg));
}

uint32_t colorForNote(int note) {
  uint32_t mappedColor = 0;
  switch (note) {
    case 60:
      mappedColor = bass60c;
      break;
    case 62:
      mappedColor = snare62c;
      break;
    case 64:
      mappedColor = hihato64c;
      break;
    case 65:
      mappedColor = hihatc65c;
      break;
    case 67:
      mappedColor = clap67c;
      break;
    case 72:
    default:
      mappedColor = rim72c;
      break;
  }
  return mappedColor;
}
