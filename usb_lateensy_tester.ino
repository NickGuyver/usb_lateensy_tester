#include "USBHost_t36.h"
#include <vector>

//#define DEBUG_OUTPUT

#define ledPin 13

// Testing pins, connected together and to target
#define testPin 6
#define interruptPin 9

// Adjust for skew as compared to protocol analyzer
#define JOYSTICK_SKEW 997
#define MOUSE_SKEW 129
#define KB_SKEW 0

// Range for random testing
#define RANDOM_FLOOR 80
#define RANDOM_CEILING 1000

// Handle stuck testing
#define MAX_TIME 5000
#define MAX_ATTEMPTS 5


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

// Only include the most top level type devices to show information for
USBDriver *drivers[] = { &joystick, &hid1, &hid2, &hid3, &hid4, &hid5, &hid6, &hid7 };

#define CNT_DEVICES (sizeof(drivers) / sizeof(drivers[0]))
const char *driver_names[CNT_DEVICES] = { "Joystick(device)", "HID1", "HID2", "HID3", "HID4", "HID5", "HID6", "HID7" };
bool driver_active[CNT_DEVICES] = { false, false, false, false };

// Include HID input devices
USBHIDInput *hiddrivers[] = { &keyboard1, &joystick, &mouse, &rawhid2 };
#define CNT_HIDDEVICES (sizeof(hiddrivers) / sizeof(hiddrivers[0]))
const char *hid_driver_names[CNT_HIDDEVICES] = { "KB1", "joystick", "mouse", "RawHid2" };

bool hid_driver_active[CNT_HIDDEVICES] = { false, false, false, false };

//=============================================================================
// Other variables
//=============================================================================
struct DeviceInfo {
  const char* name = nullptr;
  const char* prev_name = "None";
  const uint8_t* manuf = nullptr;
  const uint8_t* prod = nullptr;
  const uint8_t* serial = nullptr;
  uint16_t vid = 0;
  uint16_t pid = 0;
};

struct LatencyTest {
  uint32_t test_count = 0;
  uint32_t press_count = 0;
  uint32_t press_total = 0;
  uint32_t release_count = 0;
  uint32_t release_total = 0;
  std::vector<long unsigned int> presses = {};
  std::vector<long unsigned int>  releases = {};
};

DeviceInfo currentDevice;
LatencyTest currentTest;

elapsedMicros eu_timer = 0;
elapsedMillis em_timer = 0;
unsigned long end_timer = 0;
unsigned long random_ms = 0;

uint32_t buttons_cur = 0;
uint32_t buttons;

uint32_t skip_count = 0;

uint8_t pin_flip = 0;
uint8_t trigger_set = 0;
uint8_t test_failures = 0;


void yield()
{
  digitalToggleFast(0);
}


// Reset the microsecond timer, triggered from interrupt
void StartTimer() {
  eu_timer = 0;
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
  random_ms = random(RANDOM_FLOOR, RANDOM_CEILING);

  // Setup the various interrupt handlers
  keyboard1.attachRawPress(OnRawPress);
  keyboard1.attachRawRelease(OnRawRelease);
  attachInterrupt(digitalPinToInterrupt(interruptPin), StartTimer, CHANGE);

  // Collect current device info and diplay the menu
  myusb.Task();
  delay(500);
  UpdateActiveDeviceInfo();
}


// Main loop
void loop() {
  myusb.Task();
  
  currentTest = {};
  char choice = 0;
  int current_progress = 0;
  int last_progress = 0;
  
  currentTest.presses.clear();
  currentTest.releases.clear();

  UpdateActiveDeviceInfo();

  while (Serial.available()) {
    choice = Serial.read();

    switch (choice) {
      case '1':
        currentTest.test_count = 10;
        break;
      case '2':
        currentTest.test_count = 50;
        break;
      case '3':
        currentTest.test_count = 100;
        break;
      case '4':
        currentTest.test_count = 1000;
        break;
      case '5':
        pin_flip = !pin_flip;
        MainMenu();
        break;
    }

    if (currentTest.test_count) {
      Serial.print("\nrunning ");
      Serial.print(currentTest.test_count);
      Serial.println(" tests...\n");
    }

    while (currentTest.press_count < currentTest.test_count) {
      RunTest();

      if (test_failures >= MAX_ATTEMPTS) {
        Serial.println("Too many failed attempts.");
        Serial.println();
        MainMenu();

        choice = 0;
        test_failures = 0;
        currentTest.test_count = 0;
      }

      current_progress = (currentTest.press_count * 100) / currentTest.test_count;
      if (current_progress % 10 == 0 && last_progress != current_progress) {
        Serial.print("\t");
        Serial.print((currentTest.press_count * 100) / currentTest.test_count);
        Serial.println("% complete");
        Serial.send_now();

        last_progress = current_progress;
      }
    }
    if (currentTest.test_count) {
      Serial.println("done\n");
      PrintResults();
      Serial.println();
      MainMenu();

      choice = 0;
      currentTest.test_count = 0;
    }
  }
}


// Display the main serial menu
void MainMenu() {
  Serial.println("\n===================");
  Serial.println("USB LaTeensy Tester");
  Serial.println("===================");
  Serial.println("Testing Information");
  Serial.printf("|Testing Pin: %u\n", testPin);
  Serial.printf("|Pin Status: %u\n", pin_flip);
  Serial.println("Device Information");
  Serial.printf("|VID: %04u\n", currentDevice.vid);
  Serial.printf("|PID: %04u\n", currentDevice.pid);
  Serial.printf("|Type: %s\n", currentDevice.name);
  Serial.printf("|Manufacturer: %s\n", currentDevice.manuf);
  Serial.printf("|Product: %s\n", currentDevice.prod);
  Serial.printf("|Serial: %s\n", currentDevice.serial);
  Serial.println("");
  Serial.println("\t1 - Run 10 tests");
  Serial.println("\t2 - Run 50 tests");
  Serial.println("\t3 - Run 100 tests");
  Serial.println("\t4 - Run 1000 tests");
  Serial.println("\t5 - Flip Testing Pin");

  digitalWriteFast(ledPin, pin_flip);
}


// Show the results of the last run test
void PrintResults() {
  Serial.print("press_count: ");
  Serial.print(currentTest.press_count);
  Serial.println("");
  Serial.print("press_total: ");
  Serial.print(currentTest.press_total);
  Serial.println("");
  Serial.print("press_avg: ");
  Serial.print(currentTest.press_total / currentTest.press_count);
  Serial.println("");
  Serial.print("release_count: ");
  Serial.print(currentTest.release_count);
  Serial.println("");
  Serial.print("release_total: ");
  Serial.print(currentTest.release_total);
  Serial.println("");
  Serial.print("release_avg: ");
  Serial.print(currentTest.release_total / currentTest.release_count);
  Serial.println("");
  Serial.print("trigger_count: ");
  Serial.print(currentTest.press_count + currentTest.release_count);
  Serial.println("");
  Serial.print("trigger_total: ");
  Serial.print(currentTest.press_total + currentTest.release_total);
  Serial.println("");
  Serial.print("trigger_avg: ");
  Serial.print(((currentTest.press_total / currentTest.press_count) + 
                (currentTest.release_total / currentTest.release_count)) / 2);
  Serial.println("");
#ifdef DEBUG_OUTPUT
  Serial.println("");
  Serial.println("Press Timings:");
  for (uint32_t i = 0; i < currentTest.test_count; i++) {
    Serial.print(currentTest.presses[i]);
    Serial.println("");
  }

  Serial.println("");

  Serial.println("Release Timings:");
  for (uint32_t i = 0; i < currentTest.test_count; i++) {
    Serial.print(currentTest.releases[i]);
    Serial.println("");
  }
#endif
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
    Serial.print(currentTest.press_count);
    Serial.println("");
    Serial.print("press_total: ");
    Serial.print(currentTest.press_total);
    Serial.println("");
    Serial.print("press_avg: ");
    Serial.print(currentTest.press_total / currentTest.press_count);
    Serial.println("");
  }
  else {
    Serial.println("RELEASE");
    Serial.print("release_count: ");
    Serial.print(currentTest.release_count);
    Serial.println("");
    Serial.print("release_total: ");
    Serial.print(currentTest.release_total);
    Serial.println("");
    Serial.print("release_avg: ");
    Serial.print(currentTest.release_total / currentTest.release_count);
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
      currentTest.presses.push_back(timer);
      currentTest.press_count ++;
      currentTest.press_total += timer;
    }
    else {
      currentTest.releases.push_back(timer);
      currentTest.release_count ++;
      currentTest.release_total += timer;
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
    Serial.print(currentDevice.name);
    Serial.println("");
    Serial.print("skip_count: ");
    Serial.print(skip_count);
    Serial.println("");
    Serial.print("test_failures: ");
    Serial.print(test_failures);
    Serial.println("");
    Serial.send_now();
#endif
    trigger_set = 1;
    digitalWriteFast(ledPin, !pin_flip);

    random_ms = random(RANDOM_FLOOR, RANDOM_CEILING);
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
  else if (em_timer > MAX_TIME) {
#ifdef DEBUG_OUTPUT
    Serial.println("TIMER FAIL");
#endif
    test_failures++;
    ClearTest();

    if (test_failures >= MAX_ATTEMPTS) {
      return;
    }
  }

  skip_count ++;
}


// Clear testing variables
void ClearTest() {
  buttons_cur = buttons;
  skip_count = 0;
  trigger_set = 0;
}


// Gather the current device information
void UpdateActiveDeviceInfo() {
  // First see if any high level devices
  for (uint8_t i = 0; i < CNT_DEVICES; i++) {
    if (*drivers[i] != driver_active[i]) {
      if (driver_active[i]) {
#ifdef DEBUG_OUTPUT
        Serial.printf("*** Device %s - disconnected ***\n", driver_names[i]);
#endif
        currentDevice = {};
        driver_active[i] = false;
      }
      else {
#ifdef DEBUG_OUTPUT
        Serial.printf("*** Device %s %x:%x - connected ***\n", driver_names[i], drivers[i]->idVendor(), drivers[i]->idProduct());
        Serial.printf("Manufacturer: %s\n", drivers[i]->manufacturer());
        Serial.printf("Product: %s\n", drivers[i]->product());
        Serial.printf("Serial: %s\n", drivers[i]->serialNumber());
#endif
        currentDevice.name = driver_names[i];
        currentDevice.manuf = drivers[i]->manufacturer();
        currentDevice.prod = drivers[i]->product();
        currentDevice.serial = drivers[i]->serialNumber();
        currentDevice.vid = drivers[i]->idVendor();
        currentDevice.pid = drivers[i]->idProduct();
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
        currentDevice = {};
        hid_driver_active[i] = false;
      }
      else {       
#ifdef DEBUG_OUTPUT
        Serial.printf("*** HID Device %s %x:%x - connected ***\n", hid_driver_names[i], hiddrivers[i]->idVendor(), hiddrivers[i]->idProduct());
        Serial.printf("Manufacturer: %s\n", hiddrivers[i]->manufacturer());
        Serial.printf("Product: %s\n", hiddrivers[i]->product());
        Serial.printf("Serial: %s\n", hiddrivers[i]->serialNumber());
#endif
        currentDevice.name = hid_driver_names[i];
        currentDevice.manuf = hiddrivers[i]->manufacturer();
        currentDevice.prod = hiddrivers[i]->product();
        currentDevice.serial = hiddrivers[i]->serialNumber();
        currentDevice.vid = hiddrivers[i]->idVendor();
        currentDevice.pid = hiddrivers[i]->idProduct();
        hid_driver_active[i] = true;
      }
    }
  }

  if (currentDevice.prev_name != currentDevice.name) {
    MainMenu();
    currentDevice.prev_name = currentDevice.name;
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

    ClearTest();

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

    ClearTest();

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
    
    ClearTest();
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
