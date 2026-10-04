import QtQuick

// A plain focus target for things that are not one of the control types.
Item {
  property bool cursor: false
  signal triggered()
  function activate() { triggered() }
  function step(d) { return false }
}
