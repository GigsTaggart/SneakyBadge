# SneakyBadge

Programmed in Arduino C++

Preferences-> board manager URL https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json

Lib prereqs:  ss_oled, BitBang_I2C

**You must patch BitBang_I2C.cpp:**

After line 586 which starts out:
```
#if defined(TEENSYDUINO) || defined(ARDUINO_ARCH_MBED) || defined( __AVR__ )
```

Append "|| defined(ARDUINO_ARCH_RP2040)" on the end of the line 586

then Insert
```
#if defined(ARDUINO_ARCH_RP2040)
       pWire->setSDA((int)pI2C->iSDA);
       pWire->setSCL((int)pI2C->iSCL);
#endif
```
After line 586

Philhower's RP2040 board library doesn't
provide a two parameter initialization for the I2C Wire

It should look like this when you are done:
```
#if !defined( _LINUX_ ) && !defined( __AVR_ATtiny85__ )
#if defined(TEENSYDUINO) || defined(ARDUINO_ARCH_MBED) || defined( __AVR__ ) || defined( NRF52 ) || defined ( ARDUINO_ARCH_NRF52840 ) || defined(ARDUINO_ARCH_NRF52) || defined(ARDUINO_ARCH_SAM) || defined(ARDUINO_ARCH_RP2040)
#if defined(ARDUINO_ARCH_RP2040)
       pWire->setSDA((int)pI2C->iSDA);
       pWire->setSCL((int)pI2C->iSCL);
#endif
#ifdef ARDUINO_ARCH_MBED
```
You need the LittleFS upload tool from Philhower as well.  It only works in Arduino 1.X. It will appear in the menu as LittleFS data upload.  I used 1.5mb for code and 512k for FS mode.

In the data/ directory you need a dictionary, one word per line, all caps, sorted alphabetically, named dict.txt, and sndict.txt for the acceptable snurdle words

