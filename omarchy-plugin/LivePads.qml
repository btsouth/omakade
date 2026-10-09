import QtQuick
import Quickshell.Services.UPower

// Controller batteries that UPower reports, lowest first. Pads it cannot read
// are left out rather than guessed.
QtObject {
  id: root

  readonly property var levels: UPower.devices.values
    .filter(function(d) {
      return d && d.ready && !d.isLaptopBattery && d.isPresent !== false
        && /controller|gamepad|joystick|xbox|dualsense|dualshock|wireless controller|switch|joy-con|steam/i.test(String(d.model) + " " + String(d.nativePath))
    })
    .map(function(d) { return Math.round(d.percentage * 100) })
    .sort(function(a, b) { return a - b })
}
