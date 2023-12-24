#include "USBHost_t36.h"

#define ledPin 13
#define testPin 6
#define interruptPin 7

//#define DEBUG_OUTPUT

#define JOYSTICK_SKEW 997
#define MOUSE_SKEW 129
#define KB_SKEW 0


//=============================================================================
// USB Host Objects
//=============================================================================
USBHost myusb;
KeyboardController keyboard1(myusb);
USBHIDParser hid1(myusb);
USBHIDParser hid2(myusb);
USBHIDParser hid3(myusb);
USBHIDParser hid4(myusb);
USBHIDParser hid5(myusb);
USBHIDParser hid6(myusb);
USBHIDParser hid7(myusb);
MouseController mouse(myusb);
JoystickController joystick(myusb);
RawHIDController rawhid2(myusb);

// Lets only include in the lists The most top level type devices we wish to show information for.
USBDriver *drivers[] = { &joystick, &hid1, &hid2, &hid3, &hid4, &hid5, &hid6, &hid7 };

#define CNT_DEVICES (sizeof(drivers) / sizeof(drivers[0]))
const char *driver_names[CNT_DEVICES] = { "Joystick(device)", "HID1", "HID2", "HID3", "HID4", "HID5", "HID6", "HID7" };
bool driver_active[CNT_DEVICES] = { false, false, false, false };

// Lets also look at HID Input devices
USBHIDInput *hiddrivers[] = { &keyboard1, &joystick, &mouse, &rawhid2 };
#define CNT_HIDDEVICES (sizeof(hiddrivers) / sizeof(hiddrivers[0]))
const char *hid_driver_names[CNT_HIDDEVICES] = { "KB1", "joystick", "mouse", "RawHid2" };

bool hid_driver_active[CNT_HIDDEVICES] = { false, false, false, false };

//=============================================================================
// Other state variables.
//=============================================================================

// Save away values for button

uint32_t buttons_cur = 0;
uint32_t buttons;

volatile elapsedMicros eu_timer;
elapsedMillis em_timer;
unsigned long random_ms = random(400, 1000);
uint8_t pin_flip = 0;
uint8_t trigger_set = 0;
const char *current_device = "None";
uint32_t press_count = 0;
uint32_t press_avg = 0;
uint32_t press_total = 0;
uint32_t release_count = 0;
uint32_t release_avg = 0;
uint32_t release_total = 0;
uint32_t skip_count = 0;
uint32_t test_count = 0;
unsigned long end_timer = 0;


void yield()
{
  digitalToggleFast(0);
}


//=============================================================================
// Setup
//=============================================================================
void setup() {
  while (!Serial && millis() < 3000)
    ;  // wait for Arduino Serial Monitor

  if (CrashReport) {
    Serial.print(CrashReport);
    Serial.println("\n *** Press any key to continue ***");
    while (Serial.read() == -1)
      ;
    while (Serial.read() != -1)
      ;
  }

  myusb.begin();
  
  // Force next keyboard that attaches into boot protocol mode
  //keyboard1.forceBootProtocol();

  pinMode(ledPin, OUTPUT);
  pinMode(testPin, OUTPUT);
  pinMode(interruptPin, INPUT);
  
  // Used for randomizing the tests
  randomSeed(analogRead(A0)*analogRead(A1));

  // Setup the various interrupt handlers
  keyboard1.attachRawPress(OnRawPress);
  keyboard1.attachRawRelease(OnRawRelease);
  attachInterrupt(digitalPinToInterrupt(interruptPin), StartTimer, CHANGE);

  // Collect current device info and diplay the menu
  UpdateActiveDeviceInfo();
  MainMenu();
}


// Main loop
void loop() {
  myusb.Task();
  char choice = 0;
  int current_progress = 0;
  int last_progress = 0;
  press_count = 0;
  press_avg = 0;
  press_total = 0;
  release_count = 0;
  release_avg = 0;
  release_total = 0;

  UpdateActiveDeviceInfo();

  while (Serial.available()) {    
    choice = Serial.read();

    switch (choice) {
      case '1':
          test_count = 10;
          break;
      case '2':
          test_count = 50;
          break;
      case '3':
          test_count = 100;
          break;
      case '4':
          test_count = 1000;
          break;
    }

    if (test_count) {
      Serial.print("\nrunning ");
      Serial.print(test_count);
      Serial.println(" tests...\n");
    }

    while (press_count < test_count) {
      RunTest();

      current_progress = (press_count * 100) / test_count;
      if (current_progress % 10 == 0 && last_progress != current_progress) {
        Serial.print("\t");
        Serial.print((press_count * 100) / test_count);
        Serial.println("% complete");
        Serial.send_now();

        last_progress = current_progress;
      }
    }
    if (test_count) {
      test_count = 0;
      Serial.println("done\n");

      PrintResults();
      Serial.println();
      MainMenu();
    }
  }
}


// Reset the microsecond timer, triggered from interrupt
void StartTimer() {
  eu_timer = 0;
}


// Display the main serial menu
void MainMenu() {
  Serial.println("\n===================");
  Serial.println("USB LaTeensy Tester");
  Serial.println("===================");
  Serial.println("\t1 - Run 10 tests");
  Serial.println("\t2 - Run 50 tests");
  Serial.println("\t3 - Run 100 tests");
  Serial.println("\t4 - Run 1000 tests");
}


// Show the results of the last run test
void PrintResults() {
  Serial.print("press_count: ");
  Serial.print(press_count);
  Serial.println("");
  Serial.print("press_total: ");
  Serial.print(press_total);
  Serial.println("");
  Serial.print("press_avg: ");
  Serial.print(press_avg);
  Serial.println("");
  Serial.print("release_count: ");
  Serial.print(release_count);
  Serial.println("");
  Serial.print("release_total: ");
  Serial.print(release_total);
  Serial.println("");
  Serial.print("release_avg: ");
  Serial.print(release_avg);
  Serial.println("");
  Serial.print("trigger_count: ");
  Serial.print(press_count + release_count);
  Serial.println("");
  Serial.print("trigger_total: ");
  Serial.print(press_total + release_total);
  Serial.println("");
  Serial.print("trigger_avg: ");
  Serial.print((press_avg + release_avg) / 2);
  Serial.println("");
}


// Show debug output for each iteration of the test
#ifdef DEBUG_OUTPUT
void PrintDebug(unsigned long timer) {
  Serial.print("eu_timer: ");
  Serial.print(timer);
  Serial.println("");
  
  if (buttons) {
    Serial.println("PRESS");
    Serial.print("press_count: ");
    Serial.print(press_count);
    Serial.println("");
    Serial.print("press_total: ");
    Serial.print(press_total);
    Serial.println("");
    Serial.print("press_avg: ");
    Serial.print(press_avg);
    Serial.println("");
  }
  else {
    Serial.println("RELEASE");
    Serial.print("release_count: ");
    Serial.print(release_count);
    Serial.println("");
    Serial.print("release_total: ");
    Serial.print(release_total);
    Serial.println("");
    Serial.print("release_avg: ");
    Serial.print(release_avg);
    Serial.println("");
  }
}
#endif


// Collect and store the information from the current test
void DataCollector(unsigned long timer) {
  if (timer > 100000) {
#ifdef DEBUG_OUTPUT
    Serial.println("BAD RESULT");
    Serial.println(timer);
#endif
  }
  else {
#ifdef DEBUG_OUTPUT
      PrintDebug(timer);
#endif
    if (buttons) {
      press_count ++;
      press_total += timer;
      press_avg = press_total / press_count;
    }
    else {
      release_count ++;
      release_total += timer;
      release_avg = release_total / release_count;
    }
  }
}


// Execute the test by triggering the testPin, and then waiting for data on joystick or mouse
void RunTest() {
  if (em_timer >= random_ms && !trigger_set) {
    pin_flip = !pin_flip;
#ifdef DEBUG_OUTPUT
    Serial.println("");
    Serial.println("");
    Serial.print("em_timer: ");
    Serial.print(em_timer);
    Serial.println("");
    Serial.print("pin_flip: ");
    Serial.print(pin_flip);
    Serial.println("");
    Serial.print("current_device: ");
    Serial.print(current_device);
    Serial.println("");
    Serial.print("skip_count: ");
    Serial.print(skip_count);
    Serial.println("");
    Serial.send_now();
#endif
    trigger_set = 1;
    digitalWriteFast(ledPin, !pin_flip);

    random_ms = random(400, 1000);
    em_timer = 0;
    
    digitalWriteFast(testPin, pin_flip);
  }
  else if (joystick.available()) {
    end_timer = eu_timer;
    ProcessJoystickData(end_timer);
  }
  else if (mouse.available()) {
    end_timer = eu_timer;
    ProcessMouseData(end_timer);
  }
  skip_count ++;
}


// Gather the current device information
void UpdateActiveDeviceInfo() {
  // First see if any high level devices
  for (uint8_t i = 0; i < CNT_DEVICES; i++) {
    if (*drivers[i] != driver_active[i]) {
      if (driver_active[i]) {
#ifdef DEBUG_OUTPUT
        Serial.printf("*** Device %s - disconnected ***\n", driver_names[i]);
        current_device = "None";
#endif
        driver_active[i] = false;
      }
      else {
#ifdef DEBUG_OUTPUT
        Serial.printf("*** Device %s %x:%x - connected ***\n", driver_names[i], drivers[i]->idVendor(), drivers[i]->idProduct());
        current_device = driver_names[i];
#endif
        driver_active[i] = true;
      }
    }
  }
  // Then Hid Devices
  for (uint8_t i = 0; i < CNT_HIDDEVICES; i++) {
    if (*hiddrivers[i] != hid_driver_active[i]) {
      if (hid_driver_active[i]) {
#ifdef DEBUG_OUTPUT
        Serial.printf("*** HID Device %s - disconnected ***\n", hid_driver_names[i]);
#endif
        hid_driver_active[i] = false;
        current_device = "None";
      }
      else {       
#ifdef DEBUG_OUTPUT
        Serial.printf("*** HID Device %s %x:%x - connected ***\n", hid_driver_names[i], hiddrivers[i]->idVendor(), hiddrivers[i]->idProduct());
#endif
        hid_driver_active[i] = true;
        current_device = hid_driver_names[i];
      }
    }
  }
}


// Validate, adjust timing skew, send to collector, and then clear everything for the next test
void ProcessJoystickData(unsigned long timer) {
  buttons = joystick.getButtons();

  if (buttons != buttons_cur) {
    timer -= JOYSTICK_SKEW;

    DataCollector(timer);

#ifdef DEBUG_OUTPUT
    Serial.print("Joystick: buttons = ");
    Serial.print(buttons, HEX);
    Serial.println();
#endif

    buttons_cur = buttons;
    skip_count = 0;
    trigger_set = 0;

    joystick.joystickDataClear();
  }
}


// Validate, adjust timing skew, send to collector, and then clear everything for the next test
void ProcessMouseData(unsigned long timer) {
  buttons = mouse.getButtons();

  if (buttons != buttons_cur) {
    timer -= MOUSE_SKEW;

    DataCollector(timer);

#ifdef DEBUG_OUTPUT
    Serial.print("Mouse: buttons = ");
    Serial.print(buttons, HEX);
    Serial.println();
#endif

    buttons_cur = buttons;
    skip_count = 0;
    trigger_set = 0;

    mouse.mouseDataClear();
  }
}


// Validate, adjust timing skew, send to collector, and then clear everything for the next test
void ProcessKeyboardData(unsigned long timer) {
  if (buttons != buttons_cur) {
    timer -= KB_SKEW;

    DataCollector(timer);

#ifdef DEBUG_OUTPUT
    Serial.print("Keyboard: buttons = ");
    Serial.print(buttons, HEX);
    Serial.println();
#endif
    buttons_cur = buttons;
    skip_count = 0;
    trigger_set = 0;
  }
}


// Called whenever a new KB key is pressed
void OnRawPress(uint8_t keycode) {
#ifdef DEBUG_OUTPUT
  Serial.println("KB PRESS");
#endif
  end_timer = eu_timer;
  buttons = keycode;
  
  ProcessKeyboardData(end_timer);
}


// Called whenever a new KB key is released
void OnRawRelease(uint8_t keycode) {
#ifdef DEBUG_OUTPUT
  Serial.println("KB RELEASE");
#endif
  end_timer = eu_timer;
  buttons = 0;
  
  ProcessKeyboardData(end_timer);
}
