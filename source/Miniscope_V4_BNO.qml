// Miniscope window: the shared shell with the Miniscope-only controls
// (LUT / dFF display toggles, trace ROI, head-orientation widget). Kept as a
// wrapper so deviceConfigs/videoDevices.json qmlFile entries keep working.
VideoWindowShell {
    miniscope: true
}
