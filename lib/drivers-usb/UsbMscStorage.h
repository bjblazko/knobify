#pragma once

#include "UsbStorage.h"

namespace knobify::drivers {

// The SD card as a TinyUSB mass storage LUN (ADR 0016). Sectors go
// straight between USB and the card (sdmmc_read/write_sectors), bypassing
// the firmware's FAT layer, which must be left alone while exported.
//
// Needs ARDUINO_USB_MODE=0 (TinyUSB instead of the S3's USB-Serial-JTAG,
// see platformio.ini). The MSC interface is registered by a global object
// in the .cpp, which runs before the core starts USB -- interfaces can't
// be added after that, so the drive is always enumerated and only reports
// "no medium" until startExport().
class UsbMscStorage : public usbdrive::UsbStorage {
 public:
  // Allocates the DMA bounce buffer; call once from setup().
  void begin();

  bool startExport() override;
  void stopExport() override;
  bool hostAttached() override;
  bool takeEjectRequest() override;

  // Prints what the host did (buffered, with times) once the USB serial
  // port is connected again -- it drops with every re-enumeration, and
  // lines printed meanwhile are lost. Call every loop().
  void printEvents();

  // Writes the buffered sectors when the host pauses (see the write
  // coalescing note in the .cpp). Call every loop().
  void tickWrites();

  // True while the card is exported. Nothing may print to Serial then:
  // Arduino-ESP32 2.0.x's USBCDC::write() spins without a timeout while
  // the CDC endpoint can't drain, and a busy drive starves it -- one log
  // line froze the whole loop until the watchdog reset it (2026-09-16).
  static bool exporting();
};

}  // namespace knobify::drivers
