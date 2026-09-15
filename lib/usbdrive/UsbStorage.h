#pragma once

namespace knobify::usbdrive {

// The SD card as a USB mass storage device. Implemented over TinyUSB on
// the device (lib/drivers-usb/UsbMscStorage.h), faked in host tests.
class UsbStorage {
 public:
  virtual ~UsbStorage() = default;
  // Hands the card to the USB host. The caller guarantees nothing else
  // reads or writes the card until stopExport().
  virtual bool startExport() = 0;
  // Takes the card back and remounts its filesystem, so the firmware sees
  // what the computer wrote.
  virtual void stopExport() = 0;
  // A computer has configured the device and isn't suspended.
  virtual bool hostAttached() = 0;
  // True once after the computer ejected the drive.
  virtual bool takeEjectRequest() = 0;
};

}  // namespace knobify::usbdrive
