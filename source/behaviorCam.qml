// Behavior-camera window: the shared shell without the Miniscope-only bits.
// Kept as a wrapper so deviceConfigs/videoDevices.json qmlFile entries (and
// any user-customized catalog entries) keep pointing at a valid file.
VideoWindowShell {
    miniscope: false
}
