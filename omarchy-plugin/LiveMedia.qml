import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Services.Pipewire
import Quickshell.Services.Mpris
import Quickshell.Services.UPower

Item {
  id: root
  signal toast(var data)
  property var devices: []
  property var warned: ({})
  readonly property var sink: Pipewire.defaultAudioSink
  readonly property var source: Pipewire.defaultAudioSource
  readonly property var player: Mpris.players.values.length ? Mpris.players.values[0] : null
  readonly property var audio: ({
    volume: sink && sink.audio ? sink.audio.volume : undefined,
    muted: sink && sink.audio ? sink.audio.muted : undefined,
    micMuted: source && source.audio ? source.audio.muted : undefined,
    micName: source ? source.description : "",
    outputs: Pipewire.nodes.values.filter(n => n.audio && n.isSink && !n.isStream).map(n => ({name: n.description || n.name, id: n.id, current: n === root.sink})),
    inputs: Pipewire.nodes.values.filter(n => n.audio && !n.isSink && !n.isStream).map(n => ({name: n.description || n.name, id: n.id, current: n === root.source})),
    nowPlaying: player ? {player: player.identity, title: player.trackTitle, artist: player.trackArtist,
      art: player.trackArtUrl, playing: player.isPlaying, progress: player.length > 0 ? player.position / player.length : 0,
      position: Math.floor(player.position / 60) + ":" + String(Math.floor(player.position % 60)).padStart(2, "0"),
      length: player.length > 0 ? Math.floor(player.length / 60) + ":" + String(Math.floor(player.length % 60)).padStart(2, "0") : ""} : null
  })
  PwObjectTracker { objects: Pipewire.nodes.values.filter(n => n.audio && !n.isStream) }
  readonly property var poweredPads: UPower.devices.values.filter(d => d.ready && !d.isLaptopBattery && /controller|gamepad|joystick|xbox|dual|switch|steam/i.test(d.model + " " + d.nativePath)).map(d => ({name: d.model || "Controller", battery: Math.round(d.percentage * 100), identifiable: false, family: /dual|sony/i.test(d.model) ? "playstation" : /switch|nintendo/i.test(d.model) ? "nintendo" : /xbox/i.test(d.model) ? "xbox" : "generic"}))
  readonly property var pads: devices.map(function(pad) {
    var powered = root.poweredPads.find(d => d.name === pad.name)
    return Object.assign({}, pad, powered || {})
  }).concat(root.poweredPads.filter(d => !root.devices.some(p => p.name === d.name)))
  readonly property var battery: UPower.displayDevice && UPower.displayDevice.ready && UPower.displayDevice.isLaptopBattery ? {percent: Math.round(UPower.displayDevice.percentage * 100), charging: UPower.displayDevice.state === UPowerDeviceState.Charging} : null
  onPadsChanged: {
    pads.forEach(function(pad) {
      [20, 10].forEach(function(level) {
        var key = pad.name + ":" + level
        if (pad.battery !== undefined && pad.battery <= level && !root.warned[key]) {
          root.warned[key] = true
          root.toast({title: pad.name + " battery low", detail: pad.battery + "% remaining"})
        }
      })
    })
  }
  function act(name, value) {
    if (name === "volume" && sink && sink.audio) { sink.audio.volume = Math.max(0, Math.min(1, Number(value))); return true }
    if (name === "mic" && source && source.audio) { source.audio.muted = !value; return true }
    if (name === "output" || name === "input-device") {
      var node = Pipewire.nodes.values.find(n => n.audio && !n.isStream && n.isSink === (name === "output") && n.id === Number(value))
      if (!node) return false
      if (name === "output") Pipewire.preferredDefaultAudioSink = node
      else Pipewire.preferredDefaultAudioSource = node
      return true
    }
    if (name === "media" && player) {
      if (value === "play" && player.canTogglePlaying) player.togglePlaying()
      else if (value === "next" && player.canGoNext) player.next()
      else if (value === "previous" && player.canGoPrevious) player.previous()
      else return false
      return true
    }
    return false
  }
}
