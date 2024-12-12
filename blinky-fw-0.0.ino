#include <Adafruit_NeoPixel.h>
#include <EEPROM.h>
#include <math.h>

#define NUM_LEDS 10
#define LED_PIN 9

#define VIBRATION_PIN 10
#define BUZZER_PIN 22
#define BATTERY_ADC_PIN 28
#define BUTTON1_PIN 17
#define BUTTON2_PIN 16

Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

// Global variables
volatile bool button1Pressed = false;
volatile bool button2StateChanged = false;
volatile bool vibrationTriggered = false;

bool musicPlaying = false;
int currentSong = 0; 
int currentNoteIndex = 0;
unsigned long noteStartTime = 0;

float globalBrightness = 1.0;
byte selectedEffect = 0;

// Button2 press timing
unsigned long button2PressStart = 0;
bool button2IsPressed = false;  
bool button2LongPressHandled = false; 
const unsigned long LONG_PRESS_TIME = 2000; // ms

// Special effect ID for battery display
#define BATTERY_DISPLAY_EFFECT 255
unsigned long batteryDisplayStartTime = 0;

// Note structure and songs
struct Note {
  int frequency;
  int duration; // ms
};

// Public domain melodies
Note song1[] = {
  {330,500},{294,500},{262,500},{294,500},{330,500},{330,500},{330,500},
  {294,500},{294,500},{294,500},{330,500},{392,500},{392,500},
  {330,500},{294,500},{262,500},{294,500},{330,500},{330,500},{330,500},
  {294,500},{294,500},{330,500},{294,500},{262,1000}
};

Note song2[] = {
  {262,500},{262,500},{392,500},{392,500},{440,500},{440,500},{392,1000},
  {349,500},{349,500},{330,500},{330,500},{294,500},{294,500},{262,1000}
};

Note song3[] = {
  {294,500},{330,500},{349,500},{294,500},{294,500},{330,500},{349,500},{294,500},
  {349,500},{392,500},{440,1000},{349,500},{392,500},{440,1000}
};

Note *songs[] = {NULL, song1, song2, song3};
int songLengths[] = {0, sizeof(song1)/sizeof(Note), sizeof(song2)/sizeof(Note), sizeof(song3)/sizeof(Note)};

// ISR functions 
void button1ISR() {
  button1Pressed = true;
}

void button2ISR() {
  button2StateChanged = true;
}

void vibrationISR() {
  vibrationTriggered = true;
}

void setup() {
  strip.begin();
  strip.show(); // all off

  pinMode(BUTTON1_PIN, INPUT_PULLUP);
  pinMode(BUTTON2_PIN, INPUT_PULLUP);

  // Pin is normally low (sensor closed to GND), goes high on vibration (sensor opens)
  pinMode(VIBRATION_PIN, INPUT);
  
  pinMode(BUZZER_PIN, OUTPUT);
  noTone(BUZZER_PIN);

  attachInterrupt(BUTTON1_PIN, button1ISR, FALLING);
  attachInterrupt(BUTTON2_PIN, button2ISR, CHANGE);
  attachInterrupt(VIBRATION_PIN, vibrationISR, RISING);

  // Seed randomness
  randomSeed(analogRead(A0));

  EEPROM.get(0, selectedEffect);
  if (selectedEffect > 18) {
    selectedEffect = 0;
    EEPROM.put(0, selectedEffect);
  }
}

void loop() {
  float voltage = readBatteryVoltage();
  updateGlobalBrightness(voltage);

  handleButton2State();  
  updateMusicPlayer();

  // Handle Button1 (random effect)
  if (button1Pressed) {
    button1Pressed = false;
    // If currently displaying battery, wait until done
    if (selectedEffect != BATTERY_DISPLAY_EFFECT) {
      selectedEffect = getRandomEffect();
      EEPROM.put(0, selectedEffect);
    }
    handleButton2State();
    updateMusicPlayer();
  }

  // Handle vibration sensor (go to battery display)
  if (vibrationTriggered) {
    vibrationTriggered = false;
    if (selectedEffect != BATTERY_DISPLAY_EFFECT) {
      selectedEffect = BATTERY_DISPLAY_EFFECT;
      batteryDisplayStartTime = millis();
    }
  }

  handleButton2State();
  updateMusicPlayer();

  // If currently displaying battery effect
  if (selectedEffect == BATTERY_DISPLAY_EFFECT) {
    displayBatteryPercentageEffect(voltage);

    // After 1 second, go to random effect
    if (millis() - batteryDisplayStartTime >= 1000) {
      selectedEffect = getRandomEffect();
      EEPROM.put(0, selectedEffect);
    }

    return;
  }

  // Otherwise, run the selected LED effect
  runSelectedEffect();
}

// ---------------- New or Modified Functions ----------------

// Return a random effect from 0 to 18
byte getRandomEffect() {
  return (byte)random(0,19); // 0-18 inclusive
}

void handleButton2State() {
  if (button2StateChanged) {
    button2StateChanged = false;
    int state = digitalRead(BUTTON2_PIN);
    if (state == LOW) {
      // Button pressed
      button2IsPressed = true;
      button2PressStart = millis();
      button2LongPressHandled = false;
    } else {
      // Button released
      if (button2IsPressed) {
        unsigned long pressDuration = millis() - button2PressStart;
        button2IsPressed = false;
        if (pressDuration < LONG_PRESS_TIME) {
          // Short press
          if (!musicPlaying) {
            currentSong = 1;
            startSong(currentSong);
          } else {
            currentSong++;
            if (currentSong > 3) currentSong = 1;
            startSong(currentSong);
          }
        }
      }
    }
  }

  // Check for long press
  if (button2IsPressed && (millis() - button2PressStart > LONG_PRESS_TIME) && !button2LongPressHandled) {
    button2LongPressHandled = true;
    stopMusic();
  }
}

void updateGlobalBrightness(float voltage) {
  float highVolt = 3.8;
  float lowVolt = 3.6;
  float maxBright = 1.0;
  float minBright = 0.1;

  if (voltage >= highVolt) {
    globalBrightness = maxBright;
  } else if (voltage <= lowVolt) {
    globalBrightness = minBright;
  } else {
    float ratio = (voltage - lowVolt) / (highVolt - lowVolt);
    globalBrightness = minBright + ratio * (maxBright - minBright);
  }
}

void displayBatteryPercentageEffect(float voltage) {
  int percentage = (int)((voltage - 3.0) * (100.0 / (4.2 - 3.0))); 
  if (percentage < 0) percentage = 0;
  if (percentage > 100) percentage = 100;

  int ledsToLight = (percentage + 9) / 10; // e.g. 50% -> 5 leds
  
  // Set LEDs according to battery level
  for (int i = 0; i < ledsToLight; i++) {
    setPixel(i, 0, 255, 0); // green
    
  }
  for (int i = ledsToLight; i < NUM_LEDS; i++) {
    setPixel(i, 0, 0, 0); // off
  }
  showStrip();

  handleButton2State();
  updateMusicPlayer();
}

void startSong(int songNumber) {
  if (songNumber < 1 || songNumber > 3) return;
  
  musicPlaying = true;
  currentNoteIndex = 0;
  noteStartTime = millis();
  tone(BUZZER_PIN, songs[currentSong][currentNoteIndex].frequency);
}

void stopMusic() {
  musicPlaying = false;
  currentSong = 0;
  noTone(BUZZER_PIN);
}

void updateMusicPlayer() {
  if (!musicPlaying || currentSong == 0) return;
  
  unsigned long currentTime = millis();
  Note currentNote = songs[currentSong][currentNoteIndex];
  if (currentTime - noteStartTime >= (unsigned long)currentNote.duration) {
    currentNoteIndex++;
    if (currentNoteIndex >= songLengths[currentSong]) {
      stopMusic();
    } else {
      noteStartTime = currentTime;
      tone(BUZZER_PIN, songs[currentSong][currentNoteIndex].frequency);
    }
  }
}

float readBatteryVoltage() {
  int raw = analogRead(BATTERY_ADC_PIN);
  float measuredVoltage = (raw * 3.3) / 4095.0;
  float batteryVoltage = measuredVoltage * 2.0;
  return batteryVoltage;
}

void runSelectedEffect() {
  switch (selectedEffect) {
    case 0: RGBLoop(); break;
    case 1: FadeInOut(0xFF, 0x00, 0x00); FadeInOut(0xFF, 0xFF, 0xFF); FadeInOut(0x00, 0x00, 0xFF); break;
    case 2: Strobe(0xFF, 0xFF, 0xFF, 10, 50, 1000); break;
    case 3: HalloweenEyes(0xFF, 0x00, 0x00, 1, 4, true, random(5, 50), random(50, 150), random(1000, 10000)); break;
    case 4: CylonBounce(0xFF, 0x00, 0x00, 4, 10, 50); break;
    case 5: NewKITT(0xFF, 0x00, 0x00, 8, 10, 50); break;
    case 6: Twinkle(0xFF, 0x00, 0x00, 10, 100, false); break;
    case 7: TwinkleRandom(20, 100, false); break;
    case 8: Sparkle(0xFF, 0xFF, 0xFF, 0); break;
    case 9: SnowSparkle(0x10, 0x10, 0x10, 20, random(100, 1000)); break;
    case 10: RunningLights(0xFF, 0x00, 0x00, 50); RunningLights(0xFF, 0xFF, 0xFF, 50); RunningLights(0x00, 0x00, 0xFF, 50); break;
    case 11: colorWipe(0x00, 0xFF, 0x00, 50); colorWipe(0x00, 0x00, 0x00, 50); break;
    case 12: rainbowCycle(20); break;
    case 13: theaterChase(0xFF, 0, 0, 50); break;
    case 14: theaterChaseRainbow(50); break;
    case 15: Fire(55, 120, 15); break;
    case 16: {
      byte onecolor[1][3] = {{0xFF,0x00,0x00}};
      BouncingColoredBalls(1, onecolor, false);
      break;
    }
    case 17: {
      byte colors[3][3] = {{0xFF,0x00,0x00},{0xFF,0xFF,0xFF},{0x00,0x00,0xFF}};
      BouncingColoredBalls(3, colors, false);
      break;
    }
    case 18: meteorRain(0xFF,0xFF,0xFF,10,64,true,30); break;
  }
}

void delayWithUpdates(unsigned long ms) {
  unsigned long start = millis();
  while (millis() - start < ms) {
    updateMusicPlayer();
    handleButton2State();
  }
}

// Make sure all the effect functions (RGBLoop, FadeInOut, etc.) already use delayWithUpdates and call handleButton2State() & updateMusicPlayer() as shown previously.


// Do similar changes for all other effects: replace delayWithUpdates(...) with delayWithUpdates(...) and ensure music and button states are checked frequently.
void RGBLoop(){
  for(int j = 0; j < 3; j++ ) { 
    // Fade IN
    for(int k = 0; k < 256; k++) { 
      switch(j) { 
        case 0: setAll(k,0,0); break;
        case 1: setAll(0,k,0); break;
        case 2: setAll(0,0,k); break;
      }
      showStrip();
      delayWithUpdates(3);
    }
    // Fade OUT
    for(int k = 255; k >= 0; k--) { 
      switch(j) { 
        case 0: setAll(k,0,0); break;
        case 1: setAll(0,k,0); break;
        case 2: setAll(0,0,k); break;
      }
      showStrip();
      delayWithUpdates(3);
    }
  }
}

void FadeInOut(byte red, byte green, byte blue){
  float r, g, b;
      
  for(int k = 0; k < 256; k=k+1) { 
    r = (k/256.0)*red;
    g = (k/256.0)*green;
    b = (k/256.0)*blue;
    setAll(r,g,b);
    showStrip();
  }
     
  for(int k = 255; k >= 0; k=k-2) {
    r = (k/256.0)*red;
    g = (k/256.0)*green;
    b = (k/256.0)*blue;
    setAll(r,g,b);
    showStrip();
  }
}

void Strobe(byte red, byte green, byte blue, int StrobeCount, int FlashDelay, int EndPause){
  for(int j = 0; j < StrobeCount; j++) {
    setAll(red,green,blue);
    showStrip();
    delayWithUpdates(FlashDelay);
    setAll(0,0,0);
    showStrip();
    delayWithUpdates(FlashDelay);
  }
 
 delayWithUpdates(EndPause);
}

void HalloweenEyes(byte red, byte green, byte blue, 
                   int EyeWidth, int EyeSpace, 
                   boolean Fade, int Steps, int FadeDelay,
                   int EndPause){
  randomSeed(analogRead(0));
  
  int i;
  int StartPoint  = random( 0, NUM_LEDS - (2*EyeWidth) - EyeSpace );
  int Start2ndEye = StartPoint + EyeWidth + EyeSpace;
  
  for(i = 0; i < EyeWidth; i++) {
    setPixel(StartPoint + i, red, green, blue);
    setPixel(Start2ndEye + i, red, green, blue);
  }
  
  showStrip();
  
  if(Fade==true) {
    float r, g, b;
  
    for(int j = Steps; j >= 0; j--) {
      r = j*(red/Steps);
      g = j*(green/Steps);
      b = j*(blue/Steps);
      
      for(i = 0; i < EyeWidth; i++) {
        setPixel(StartPoint + i, r, g, b);
        setPixel(Start2ndEye + i, r, g, b);
      }
      
      showStrip();
      delayWithUpdates(FadeDelay);
    }
  }
  
  setAll(0,0,0); // Set all black
  
  delayWithUpdates(EndPause);
}

void CylonBounce(byte red, byte green, byte blue, int EyeSize, int SpeedDelay, int ReturnDelay){

  for(int i = 0; i < NUM_LEDS-EyeSize-2; i++) {
    setAll(0,0,0);
    setPixel(i, red/10, green/10, blue/10);
    for(int j = 1; j <= EyeSize; j++) {
      setPixel(i+j, red, green, blue); 
    }
    setPixel(i+EyeSize+1, red/10, green/10, blue/10);
    showStrip();
    delayWithUpdates(SpeedDelay);
  }

  delayWithUpdates(ReturnDelay);

  for(int i = NUM_LEDS-EyeSize-2; i > 0; i--) {
    setAll(0,0,0);
    setPixel(i, red/10, green/10, blue/10);
    for(int j = 1; j <= EyeSize; j++) {
      setPixel(i+j, red, green, blue); 
    }
    setPixel(i+EyeSize+1, red/10, green/10, blue/10);
    showStrip();
    delayWithUpdates(SpeedDelay);
  }
  
  delayWithUpdates(ReturnDelay);
}

void NewKITT(byte red, byte green, byte blue, int EyeSize, int SpeedDelay, int ReturnDelay){
  RightToLeft(red, green, blue, EyeSize, SpeedDelay, ReturnDelay);
  LeftToRight(red, green, blue, EyeSize, SpeedDelay, ReturnDelay);
  OutsideToCenter(red, green, blue, EyeSize, SpeedDelay, ReturnDelay);
  CenterToOutside(red, green, blue, EyeSize, SpeedDelay, ReturnDelay);
  LeftToRight(red, green, blue, EyeSize, SpeedDelay, ReturnDelay);
  RightToLeft(red, green, blue, EyeSize, SpeedDelay, ReturnDelay);
  OutsideToCenter(red, green, blue, EyeSize, SpeedDelay, ReturnDelay);
  CenterToOutside(red, green, blue, EyeSize, SpeedDelay, ReturnDelay);
}

// used by NewKITT
void CenterToOutside(byte red, byte green, byte blue, int EyeSize, int SpeedDelay, int ReturnDelay) {
  for(int i =((NUM_LEDS-EyeSize)/2); i>=0; i--) {
    setAll(0,0,0);
    
    setPixel(i, red/10, green/10, blue/10);
    for(int j = 1; j <= EyeSize; j++) {
      setPixel(i+j, red, green, blue); 
    }
    setPixel(i+EyeSize+1, red/10, green/10, blue/10);
    
    setPixel(NUM_LEDS-i, red/10, green/10, blue/10);
    for(int j = 1; j <= EyeSize; j++) {
      setPixel(NUM_LEDS-i-j, red, green, blue); 
    }
    setPixel(NUM_LEDS-i-EyeSize-1, red/10, green/10, blue/10);
    
    showStrip();
    delayWithUpdates(SpeedDelay);
  }
  delayWithUpdates(ReturnDelay);
}

// used by NewKITT
void OutsideToCenter(byte red, byte green, byte blue, int EyeSize, int SpeedDelay, int ReturnDelay) {
  for(int i = 0; i<=((NUM_LEDS-EyeSize)/2); i++) {
    setAll(0,0,0);
    
    setPixel(i, red/10, green/10, blue/10);
    for(int j = 1; j <= EyeSize; j++) {
      setPixel(i+j, red, green, blue); 
    }
    setPixel(i+EyeSize+1, red/10, green/10, blue/10);
    
    setPixel(NUM_LEDS-i, red/10, green/10, blue/10);
    for(int j = 1; j <= EyeSize; j++) {
      setPixel(NUM_LEDS-i-j, red, green, blue); 
    }
    setPixel(NUM_LEDS-i-EyeSize-1, red/10, green/10, blue/10);
    
    showStrip();
    delayWithUpdates(SpeedDelay);
  }
  delayWithUpdates(ReturnDelay);
}

// used by NewKITT
void LeftToRight(byte red, byte green, byte blue, int EyeSize, int SpeedDelay, int ReturnDelay) {
  for(int i = 0; i < NUM_LEDS-EyeSize-2; i++) {
    setAll(0,0,0);
    setPixel(i, red/10, green/10, blue/10);
    for(int j = 1; j <= EyeSize; j++) {
      setPixel(i+j, red, green, blue); 
    }
    setPixel(i+EyeSize+1, red/10, green/10, blue/10);
    showStrip();
    delayWithUpdates(SpeedDelay);
  }
  delayWithUpdates(ReturnDelay);
}

// used by NewKITT
void RightToLeft(byte red, byte green, byte blue, int EyeSize, int SpeedDelay, int ReturnDelay) {
  for(int i = NUM_LEDS-EyeSize-2; i > 0; i--) {
    setAll(0,0,0);
    setPixel(i, red/10, green/10, blue/10);
    for(int j = 1; j <= EyeSize; j++) {
      setPixel(i+j, red, green, blue); 
    }
    setPixel(i+EyeSize+1, red/10, green/10, blue/10);
    showStrip();
    delayWithUpdates(SpeedDelay);
  }
  delayWithUpdates(ReturnDelay);
}

void Twinkle(byte red, byte green, byte blue, int Count, int SpeedDelay, boolean OnlyOne) {
  setAll(0,0,0);
  
  for (int i=0; i<Count; i++) {
     setPixel(random(NUM_LEDS),red,green,blue);
     showStrip();
     delayWithUpdates(SpeedDelay);
     if(OnlyOne) { 
       setAll(0,0,0); 
     }
   }
  
  delayWithUpdates(SpeedDelay);
}

void TwinkleRandom(int Count, int SpeedDelay, boolean OnlyOne) {
  setAll(0,0,0);
  
  for (int i=0; i<Count; i++) {
     setPixel(random(NUM_LEDS),random(0,255),random(0,255),random(0,255));
     showStrip();
     delayWithUpdates(SpeedDelay);
     if(OnlyOne) { 
       setAll(0,0,0); 
     }
   }
  
  delayWithUpdates(SpeedDelay);
}

void Sparkle(byte red, byte green, byte blue, int SpeedDelay) {
  int Pixel = random(NUM_LEDS);
  setPixel(Pixel,red,green,blue);
  showStrip();
  delayWithUpdates(SpeedDelay);
  setPixel(Pixel,0,0,0);
}

void SnowSparkle(byte red, byte green, byte blue, int SparkleDelay, int SpeedDelay) {
  setAll(red,green,blue);
  
  int Pixel = random(NUM_LEDS);
  setPixel(Pixel,0xff,0xff,0xff);
  showStrip();
  delayWithUpdates(SparkleDelay);
  setPixel(Pixel,red,green,blue);
  showStrip();
  delayWithUpdates(SpeedDelay);
}

void RunningLights(byte red, byte green, byte blue, int WaveDelay) {
  int Position=0;
  
  for(int i=0; i<NUM_LEDS*2; i++)
  {
      Position++; // = 0; //Position + Rate;
      for(int i=0; i<NUM_LEDS; i++) {
        // sine wave, 3 offset waves make a rainbow!
        //float level = sin(i+Position) * 127 + 128;
        //setPixel(i,level,0,0);
        //float level = sin(i+Position) * 127 + 128;
        setPixel(i,((sin(i+Position) * 127 + 128)/255)*red,
                   ((sin(i+Position) * 127 + 128)/255)*green,
                   ((sin(i+Position) * 127 + 128)/255)*blue);
      }
      
      showStrip();
      delayWithUpdates(WaveDelay);
  }
}

void colorWipe(byte red, byte green, byte blue, int SpeedDelay) {
  for(uint16_t i=0; i<NUM_LEDS; i++) {
      setPixel(i, red, green, blue);
      showStrip();
      delayWithUpdates(SpeedDelay);
  }
}

void rainbowCycle(int SpeedDelay) {
  byte *c;
  uint16_t i, j;

  for(j=0; j<256*5; j++) { // 5 cycles of all colors on wheel
    for(i=0; i< NUM_LEDS; i++) {
      c=Wheel(((i * 256 / NUM_LEDS) + j) & 255);
      setPixel(i, *c, *(c+1), *(c+2));
    }
    showStrip();
    delayWithUpdates(SpeedDelay);
  }
}

// used by rainbowCycle and theaterChaseRainbow
byte * Wheel(byte WheelPos) {
  static byte c[3];
  
  if(WheelPos < 85) {
   c[0]=WheelPos * 3;
   c[1]=255 - WheelPos * 3;
   c[2]=0;
  } else if(WheelPos < 170) {
   WheelPos -= 85;
   c[0]=255 - WheelPos * 3;
   c[1]=0;
   c[2]=WheelPos * 3;
  } else {
   WheelPos -= 170;
   c[0]=0;
   c[1]=WheelPos * 3;
   c[2]=255 - WheelPos * 3;
  }

  return c;
}

void theaterChase(byte red, byte green, byte blue, int SpeedDelay) {
  for (int j=0; j<10; j++) {  //do 10 cycles of chasing
    for (int q=0; q < 3; q++) {
      for (int i=0; i < NUM_LEDS; i=i+3) {
        setPixel(i+q, red, green, blue);    //turn every third pixel on
      }
      showStrip();
     
      delayWithUpdates(SpeedDelay);
     
      for (int i=0; i < NUM_LEDS; i=i+3) {
        setPixel(i+q, 0,0,0);        //turn every third pixel off
      }
    }
  }
}

void theaterChaseRainbow(int SpeedDelay) {
  byte *c;
  
  for (int j=0; j < 256; j++) {     // cycle all 256 colors in the wheel
    for (int q=0; q < 3; q++) {
        for (int i=0; i < NUM_LEDS; i=i+3) {
          c = Wheel( (i+j) % 255);
          setPixel(i+q, *c, *(c+1), *(c+2));    //turn every third pixel on
        }
        showStrip();
       
        delayWithUpdates(SpeedDelay);
       
        for (int i=0; i < NUM_LEDS; i=i+3) {
          setPixel(i+q, 0,0,0);        //turn every third pixel off
        }
    }
  }
}

void Fire(int Cooling, int Sparking, int SpeedDelay) {
  static byte heat[NUM_LEDS];
  int cooldown;
  
  // Step 1.  Cool down every cell a little
  for( int i = 0; i < NUM_LEDS; i++) {
    cooldown = random(0, ((Cooling * 10) / NUM_LEDS) + 2);
    
    if(cooldown>heat[i]) {
      heat[i]=0;
    } else {
      heat[i]=heat[i]-cooldown;
    }
  }
  
  // Step 2.  Heat from each cell drifts 'up' and diffuses a little
  for( int k= NUM_LEDS - 1; k >= 2; k--) {
    heat[k] = (heat[k - 1] + heat[k - 2] + heat[k - 2]) / 3;
  }
    
  // Step 3.  Randomly ignite new 'sparks' near the bottom
  if( random(255) < Sparking ) {
    int y = random(7);
    heat[y] = heat[y] + random(160,255);
    //heat[y] = random(160,255);
  }

  // Step 4.  Convert heat to LED colors
  for( int j = 0; j < NUM_LEDS; j++) {
    setPixelHeatColor(j, heat[j] );
  }

  showStrip();
  delayWithUpdates(SpeedDelay);
}

void setPixelHeatColor (int Pixel, byte temperature) {
  // Scale 'heat' down from 0-255 to 0-191
  byte t192 = round((temperature/255.0)*191);
 
  // calculate ramp up from
  byte heatramp = t192 & 0x3F; // 0..63
  heatramp <<= 2; // scale up to 0..252
 
  // figure out which third of the spectrum we're in:
  if( t192 > 0x80) {                     // hottest
    setPixel(Pixel, 255, 255, heatramp);
  } else if( t192 > 0x40 ) {             // middle
    setPixel(Pixel, 255, heatramp, 0);
  } else {                               // coolest
    setPixel(Pixel, heatramp, 0, 0);
  }
}

void BouncingColoredBalls(int BallCount, byte colors[][3], boolean continuous) {
  float Gravity = -9.81;
  int StartHeight = 1;
  
  float Height[BallCount];
  float ImpactVelocityStart = sqrt( -2 * Gravity * StartHeight );
  float ImpactVelocity[BallCount];
  float TimeSinceLastBounce[BallCount];
  int   Position[BallCount];
  long  ClockTimeSinceLastBounce[BallCount];
  float Dampening[BallCount];
  boolean ballBouncing[BallCount];
  boolean ballsStillBouncing = true;
  
  for (int i = 0 ; i < BallCount ; i++) {   
    ClockTimeSinceLastBounce[i] = millis();
    Height[i] = StartHeight;
    Position[i] = 0; 
    ImpactVelocity[i] = ImpactVelocityStart;
    TimeSinceLastBounce[i] = 0;
    Dampening[i] = 0.90 - float(i)/pow(BallCount,2);
    ballBouncing[i]=true; 
  }

  while (ballsStillBouncing) {
    for (int i = 0 ; i < BallCount ; i++) {
      TimeSinceLastBounce[i] =  millis() - ClockTimeSinceLastBounce[i];
      Height[i] = 0.5 * Gravity * pow( TimeSinceLastBounce[i]/1000 , 2.0 ) + ImpactVelocity[i] * TimeSinceLastBounce[i]/1000;
  
      if ( Height[i] < 0 ) {                      
        Height[i] = 0;
        ImpactVelocity[i] = Dampening[i] * ImpactVelocity[i];
        ClockTimeSinceLastBounce[i] = millis();
  
        if ( ImpactVelocity[i] < 0.01 ) {
          if (continuous) {
            ImpactVelocity[i] = ImpactVelocityStart;
          } else {
            ballBouncing[i]=false;
          }
        }
      }
      Position[i] = round( Height[i] * (NUM_LEDS - 1) / StartHeight);
    }

    ballsStillBouncing = false; // assume no balls bouncing
    for (int i = 0 ; i < BallCount ; i++) {
      setPixel(Position[i],colors[i][0],colors[i][1],colors[i][2]);
      if ( ballBouncing[i] ) {
        ballsStillBouncing = true;
      }
    }
    
    showStrip();
    setAll(0,0,0);
  }
}

void meteorRain(byte red, byte green, byte blue, byte meteorSize, byte meteorTrailDecay, boolean meteorRandomDecay, int SpeedDelay) {  
  setAll(0,0,0);
  
  for(int i = 0; i < NUM_LEDS+NUM_LEDS; i++) {
    
    
    // fade brightness all LEDs one step
    for(int j=0; j<NUM_LEDS; j++) {
      if( (!meteorRandomDecay) || (random(10)>5) ) {
        fadeToBlack(j, meteorTrailDecay );        
      }
    }
    
    // draw meteor
    for(int j = 0; j < meteorSize; j++) {
      if( ( i-j <NUM_LEDS) && (i-j>=0) ) {
        setPixel(i-j, red, green, blue);
      } 
    }
   
    showStrip();
    delayWithUpdates(SpeedDelay);
  }
}

// used by meteorrain
void fadeToBlack(int ledNo, byte fadeValue) {
 #ifdef ADAFRUIT_NEOPIXEL_H 
    // NeoPixel
    uint32_t oldColor;
    uint8_t r, g, b;
    int value;
    
    oldColor = strip.getPixelColor(ledNo);
    r = (oldColor & 0x00ff0000UL) >> 16;
    g = (oldColor & 0x0000ff00UL) >> 8;
    b = (oldColor & 0x000000ffUL);

    r=(r<=10)? 0 : (int) r-(r*fadeValue/256);
    g=(g<=10)? 0 : (int) g-(g*fadeValue/256);
    b=(b<=10)? 0 : (int) b-(b*fadeValue/256);
    
    strip.setPixelColor(ledNo, r,g,b);
 #endif
 #ifndef ADAFRUIT_NEOPIXEL_H
   // FastLED
   leds[ledNo].fadeToBlackBy( fadeValue );
 #endif  
}

// *** REPLACE TO HERE ***
void showStrip() {
  strip.show();
}

void setPixel(int Pixel, byte red, byte green, byte blue) {
  float scale = globalBrightness;
  int r = (int)(red * scale);
  int g = (int)(green * scale);
  int b = (int)(blue * scale);
  strip.setPixelColor(Pixel, strip.Color(r, g, b));
}

void setAll(byte red, byte green, byte blue) {
  for (int i = 0; i < NUM_LEDS; i++) {
    setPixel(i, red, green, blue);
  }
  showStrip();
}
// ... Repeat for other effects ...
