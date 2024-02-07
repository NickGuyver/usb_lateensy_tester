#include <array>
#include <chrono>
#include <cstdint>
#include <TeensyThreads.h>
#include <vector>

#include "USBHost_t36.h"

#define DEBUG_OUTPUT

//=============================================================================
// Pin Assignments
//=============================================================================
constexpr int led_pin = 13;
// Testing pins, connected together and to target
constexpr int test_pin = 6;
constexpr int interrupt_pin = 9;

//=============================================================================
// Skew Adjustments
//=============================================================================
constexpr int joystick_skew_us = 0;//997;
constexpr int mouse_skew_us = 0;//129;
constexpr int keyboard_skew_us = 0; // Testing data needed

using milliseconds = std::chrono::milliseconds;

// Randomize testing
milliseconds random_ms(0);
constexpr int random_floor_ms = 80;
constexpr int random_ceiling_ms = 1000;

// Handle stuck testing
uint8_t test_fail_count = 0;
constexpr int max_fail_time_ms = 5000;
constexpr int max_fail_count = 5;

// Timers used for measurements
elapsedMicros timer_us = 0;
elapsedMillis timer_ms = 0;

uint32_t buttons;
uint32_t prev_buttons = 0;

bool pin_state = 0;
bool trigger_state = 0;
uint32_t skip_count = 0;
milliseconds bad_result(100000);

struct DeviceInfo {
  const char* name = nullptr;
  const char* prev_name = "None";
  const uint8_t* manufacturer = nullptr;
  const uint8_t* product = nullptr;
  const uint8_t* serial = nullptr;
  uint16_t vendor_id = 0;
  uint16_t product_id = 0;
};

struct LatencyTest {
  uint32_t test_count = 0;
  uint32_t press_count = 0;
  uint32_t press_total = 0;
  uint32_t release_count = 0;
  uint32_t release_total = 0;
  std::vector<long unsigned int> presses = {};
  std::vector<long unsigned int>  releases = {};
  int current_progress = 0;
  int last_progress = 0;
};

DeviceInfo current_device;
LatencyTest current_test;

//=============================================================================
// USB Host Objects
//=============================================================================
USBHost usb_host;
JoystickController joystick(usb_host);
KeyboardController keyboard(usb_host);
MouseController mouse(usb_host);
RawHIDController rawhid(usb_host);
USBHIDParser hid1(usb_host);
USBHIDParser hid2(usb_host);
USBHIDParser hid3(usb_host);
USBHIDParser hid4(usb_host);
USBHIDParser hid5(usb_host);
USBHIDParser hid6(usb_host);
USBHIDParser hid7(usb_host);

// Include the most top level type devices to show information for
std::array<USBDriver*, 8> drivers = {&joystick, &hid1, &hid2, &hid3, &hid4, &hid5, &hid6, &hid7};
std::array<const char*, drivers.size()> driver_names = {"Joystick (Device)", "HID1", "HID2", "HID3", "HID4", "HID5", "HID6", "HID7"};
std::array<bool, drivers.size()> driver_active = {false, false, false, false, false, false, false, false};

// Include HID input devices
std::array<USBHIDInput*, 4> hid_drivers = {&joystick, &keyboard, &mouse, &rawhid};
std::array<const char*, hid_drivers.size()> hid_driver_names = {"Joystick", "Keyboard", "Mouse", "RawHID"};
std::array<bool, hid_drivers.size()> hid_driver_active = {false, false, false, false};


//=============================================================================
// Interrupt Functions
//=============================================================================
// Triggered from interrupt_pin
void StartTimer() {
  timer_us = 0;
}

// Triggered from keyboard press
void OnRawPress(uint8_t keycode) {
#ifdef DEBUG_OUTPUT
  Serial.println("KB PRESS");
#endif
  buttons = keycode;
  
  ProcessKeyboardData(timer_us);
}

// Triggered from keyboard release
void OnRawRelease(uint8_t keycode) {
#ifdef DEBUG_OUTPUT
  Serial.println("KB RELEASE");
#endif
  buttons = 0;
  
  ProcessKeyboardData(timer_us);
}


//=============================================================================
// Thread Functions
//=============================================================================
volatile uint32_t end_timer_us = 0;

void StopTimer() {
  while (1) {
    if (joystick.available() || mouse.available()) {
      end_timer_us = timer_us;
    }
    threads.yield();
  }
}


//=============================================================================
// Setup
//=============================================================================
void setup() {
  while (!Serial && (millis() < 3000))
    ;  // wait for Arduino Serial Monitor

  if (CrashReport) {
    Serial.print(CrashReport);
    Serial.println("\n *** Press any key to continue ***");
    while (Serial.read() == -1)
      ;
    while (Serial.read() != -1)
      ;
  }

  usb_host.begin();
  
  // Force next keyboard that attaches into boot protocol mode
  //keyboard.forceBootProtocol();

  pinMode(led_pin, OUTPUT);
  pinMode(test_pin, OUTPUT);
  pinMode(interrupt_pin, INPUT);
  
  // Used for randomizing the tests
  randomSeed(analogRead(A0)*analogRead(A1));
  random_ms = milliseconds(random(random_floor_ms, random_ceiling_ms));

  // Setup for threading
  //threads.setSliceMicros(1);
  threads.addThread(StopTimer);
  // loop() is id 0
  threads.setTimeSlice(0, 1);
  // StopTimer() is id 1
  threads.setTimeSlice(1, 1);

  // Setup the various interrupt handlers
  keyboard.attachRawPress(OnRawPress);
  keyboard.attachRawRelease(OnRawRelease);
  attachInterrupt(digitalPinToInterrupt(interrupt_pin), StartTimer, CHANGE);

  // Collect current device info and diplay the menu
  usb_host.Task();
  delay(500);
  UpdateActiveDeviceInfo();
}


//=============================================================================
// Main Loop
//=============================================================================
void loop() {
  usb_host.Task();
  
  current_test = {};
  
  UpdateActiveDeviceInfo();
  GetMenuChoice();
  
  if (current_test.test_count) {
    Serial.printf("\nRunning %lu tests...\n", current_test.test_count);
  }

  while (current_test.press_count < current_test.test_count) {
    if ((timer_ms >= random_ms.count()) && !trigger_state) {
      StartTest();
    }
    else {
      threads.yield();
      CheckUSB();
      TestFailureCheck();
      PrintProgress();
    }
  }

  if (current_test.test_count) {
    PrintResults();
  }
}


// Display the main serial menu
void MainMenu() {
  Serial.println("\n===================");
  Serial.println("USB LaTeensy Tester");
  Serial.println("===================");
  Serial.println("Testing Information");
  Serial.printf("|Testing Pin: %u\n", test_pin);
  Serial.printf("|Pin Status: %u\n", pin_state);
  Serial.println("Device Information");
  Serial.printf("|VID: %04u\n", current_device.vendor_id);
  Serial.printf("|PID: %04u\n", current_device.product_id);
  Serial.printf("|Type: %s\n", current_device.name);
  Serial.printf("|Manufacturer: %s\n", current_device.manufacturer);
  Serial.printf("|Product: %s\n", current_device.product);
  Serial.printf("|Serial: %s\n", current_device.serial);
  Serial.println("");
  Serial.println("\t1 - Run 10 tests");
  Serial.println("\t2 - Run 50 tests");
  Serial.println("\t3 - Run 100 tests");
  Serial.println("\t4 - Run 1000 tests");
  Serial.println("\t5 - Flip Testing Pin");

  digitalWriteFast(led_pin, pin_state);
  digitalWriteFast(test_pin, pin_state);
}


// Get the number of tests to run and allow flipping the test_pin
void GetMenuChoice() {
  switch (Serial.read()) {
    case '1':
      current_test.test_count = 10;
      break;
    case '2':
      current_test.test_count = 50;
      break;
    case '3':
      current_test.test_count = 100;
      break;
    case '4':
      current_test.test_count = 1000;
      break;
    case '5':
      pin_state = !pin_state;
      MainMenu();
      break;
  }
}


// Execute the test by triggering the test_pin
void StartTest() {
  pin_state = !pin_state;
#ifdef DEBUG_OUTPUT
  Serial.printf("\n\nrandom_ms: %lu\n", random_ms);
  Serial.printf("pin_state: %d\n", pin_state);
  Serial.printf("current_device: %s\n", current_device.name);
  Serial.printf("skip_count: %lu\n", skip_count);
  Serial.printf("test_fail_count: %u\n", test_fail_count);
  Serial.send_now();
#endif
  trigger_state = 1;
  digitalWriteFast(led_pin, pin_state);

  random_ms = milliseconds(random(random_floor_ms, random_ceiling_ms));
  timer_ms = 0;
  
  digitalWriteFast(test_pin, pin_state);
}


// Check for state changes on USB, then send the time
void CheckUSB() {
  if (joystick.available()) {
    ProcessJoystickData(timer_us);
  }
  if (mouse.available()) {
    ProcessMouseData(timer_us);
  }
  
  skip_count ++;
}


// Check for and keep track of testing failures due to timeouts
void TestFailureCheck() {
  if (timer_ms > max_fail_time_ms) {
#ifdef DEBUG_OUTPUT
    Serial.println("TIMER FAIL");
#endif
    test_fail_count++;
    ClearTest();

    if (test_fail_count >= max_fail_count) {
      Serial.printf("\nToo many failed attempts.\n");
      MainMenu();

      test_fail_count = 0;
      current_test.test_count = 0;
    }
  }
}


// Show the current testing progress in 10% increments
void PrintProgress() {
  current_test.current_progress = (current_test.press_count * 100) / current_test.test_count;
  
  if (current_test.current_progress % 10 == 0 && current_test.last_progress != current_test.current_progress) {
    Serial.printf("\t%d%% complete\n", current_test.current_progress);
    Serial.send_now();

    current_test.last_progress = current_test.current_progress;
  }
}


// Validate, adjust timing skew, send to collector, and then clear everything for the next test
void ProcessJoystickData(unsigned long timer) {
  buttons = joystick.getButtons();
#ifdef DEBUG_OUTPUT
  Serial.printf("end_timer_us = %lu\n", end_timer_us);
#endif
  if (buttons != prev_buttons) {
    if (timer >= joystick_skew_us) {
      timer -= joystick_skew_us;
    }

    DataCollector(timer);

#ifdef DEBUG_OUTPUT
    Serial.printf("Joystick: buttons = %lu\n", buttons);
#endif

    ClearTest();

    joystick.joystickDataClear();
  }
}


// Validate, adjust timing skew, send to collector, and then clear everything for the next test
void ProcessMouseData(unsigned long timer) {
  buttons = mouse.getButtons();
#ifdef DEBUG_OUTPUT
  Serial.printf("end_timer_us = %lu\n", end_timer_us);
#endif
  if (buttons != prev_buttons) {
    if (timer >= mouse_skew_us) {
      timer -= mouse_skew_us;
    }

    DataCollector(timer);

#ifdef DEBUG_OUTPUT
    Serial.printf("Mouse: buttons = %lu\n", buttons);
#endif

    ClearTest();

    mouse.mouseDataClear();
  }
}


// Collect and store the information from the current test
void DataCollector(unsigned long timer) {
  if (timer > bad_result.count()) {
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
      current_test.presses.push_back(timer);
      current_test.press_count ++;
      current_test.press_total += timer;
    }
    else {
      current_test.releases.push_back(timer);
      current_test.release_count ++;
      current_test.release_total += timer;
    }
  }
}


// Clear testing variables
void ClearTest() {
  prev_buttons = buttons;
  skip_count = 0;
  trigger_state = 0;
}


// Show the results of the last run test
void PrintResults() {
  Serial.println("done\n");

  Serial.printf("press_count: %lu\n", current_test.press_count);
  Serial.printf("press_total: %lu\n", current_test.press_total);
  Serial.printf("press_avg: %lu\n", (current_test.press_total / current_test.press_count));
  Serial.printf("release_count: %lu\n", current_test.release_count);
  Serial.printf("release_total: %lu\n", current_test.release_total);
  Serial.printf("release_avg: %lu\n", (current_test.release_total / current_test.release_count));
  Serial.printf("trigger_count: %lu\n", (current_test.press_count + current_test.release_count));
  Serial.printf("trigger_total: %lu\n", (current_test.press_total + current_test.release_total));
  Serial.print("trigger_avg: ");
  Serial.print(((current_test.press_total / current_test.press_count) + 
                (current_test.release_total / current_test.release_count)) / 2);
  Serial.println("");
#ifdef DEBUG_OUTPUT
  Serial.println("");
  Serial.println("Press Timings:");
  for (uint32_t i = 0; i < current_test.test_count; i++) {
    Serial.print(current_test.presses[i]);
    Serial.println("");
  }

  Serial.println("");

  Serial.println("Release Timings:");
  for (uint32_t i = 0; i < current_test.test_count; i++) {
    Serial.print(current_test.releases[i]);
    Serial.println("");
  }
#endif
  Serial.println();
  MainMenu();

  current_test.test_count = 0;
}


// Show debug output for each iteration of the test
#ifdef DEBUG_OUTPUT
void PrintDebug(unsigned long timer) {
  Serial.printf("timer_us: %lu\n", timer);
  
  if (buttons) {
    Serial.println("PRESS");
    Serial.printf("press_count: %lu\n", current_test.press_count);
    Serial.printf("press_total: %lu\n", current_test.press_total);
    Serial.printf("press_avg: %lu\n", (current_test.press_total / current_test.press_count));
  }
  else {
    Serial.println("RELEASE");
    Serial.printf("release_count: %lu\n", current_test.release_count);
    Serial.printf("release_total: %lu\n", current_test.release_total);
    Serial.printf("release_avg: %lu\n", (current_test.release_total / current_test.release_count));
  }
}
#endif


// Gather the current device information
void UpdateActiveDeviceInfo() {
  // First see if any high level devices
  for (uint8_t i = 0; i < drivers.size(); i++) {
    if (*drivers[i] != driver_active[i]) {
      if (driver_active[i]) {
#ifdef DEBUG_OUTPUT
        Serial.printf("*** Device %s - disconnected ***\n", driver_names[i]);
#endif
        current_device = {};
        driver_active[i] = false;
      }
      else {
#ifdef DEBUG_OUTPUT
        Serial.printf("*** Device %s %x:%x - connected ***\n", driver_names[i], drivers[i]->idVendor(), drivers[i]->idProduct());
        Serial.printf("Manufacturer: %s\n", drivers[i]->manufacturer());
        Serial.printf("Product: %s\n", drivers[i]->product());
        Serial.printf("Serial: %s\n", drivers[i]->serialNumber());
#endif
        current_device.name = driver_names[i];
        current_device.manufacturer = drivers[i]->manufacturer();
        current_device.product = drivers[i]->product();
        current_device.serial = drivers[i]->serialNumber();
        current_device.vendor_id = drivers[i]->idVendor();
        current_device.product_id = drivers[i]->idProduct();
        driver_active[i] = true;
      }
    }
  }
  // Then Hid Devices
  for (uint8_t i = 0; i < hid_drivers.size(); i++) {
    if (*hid_drivers[i] != hid_driver_active[i]) {
      if (hid_driver_active[i]) {
#ifdef DEBUG_OUTPUT
        Serial.printf("*** HID Device %s - disconnected ***\n", hid_driver_names[i]);
#endif
        current_device = {};
        hid_driver_active[i] = false;
      }
      else {       
#ifdef DEBUG_OUTPUT
        Serial.printf("*** HID Device %s %x:%x - connected ***\n", hid_driver_names[i], hid_drivers[i]->idVendor(), hid_drivers[i]->idProduct());
        Serial.printf("Manufacturer: %s\n", hid_drivers[i]->manufacturer());
        Serial.printf("Product: %s\n", hid_drivers[i]->product());
        Serial.printf("Serial: %s\n", hid_drivers[i]->serialNumber());
#endif
        current_device.name = hid_driver_names[i];
        current_device.manufacturer = hid_drivers[i]->manufacturer();
        current_device.product = hid_drivers[i]->product();
        current_device.serial = hid_drivers[i]->serialNumber();
        current_device.vendor_id = hid_drivers[i]->idVendor();
        current_device.product_id = hid_drivers[i]->idProduct();
        hid_driver_active[i] = true;
      }
    }
  }

  if (current_device.prev_name != current_device.name) {
    MainMenu();
    current_device.prev_name = current_device.name;
  }
}


// Validate, adjust timing skew, send to collector, and then clear everything for the next test
void ProcessKeyboardData(unsigned long timer) {
  if (buttons != prev_buttons) {
    timer -= keyboard_skew_us;

    DataCollector(timer);

#ifdef DEBUG_OUTPUT
    Serial.printf("Keyboard: buttons = %lu\n", buttons);
#endif
    
    ClearTest();
  }
}
