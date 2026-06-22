# DEPRECATION NOTICE

This software is reaching its end of life, and we are in the process of consolidating and rearchitecting miniscope IO software as an SDK with minimal I/O functionality for all miniscopes - [`miniscope-io`](https://github.com/Aharoni-Lab/miniscope-io) and a yet-to-be-started GUI built on top of that. 

This software will receive one final major update to bring it to Qt6 and allow it to be reproducibly built so that current users can maintain it and we can handle minimal patches in the short term. We do not have a firm deprecation timeline yet, but will update this notice with at least 6 months notice before formally dropping support. 

These issues are tracking the update:
- [ ] [Reproducible Builds](https://github.com/Aharoni-Lab/Miniscope-DAQ-QT-Software/issues/61)
- [ ] [Update to Qt6](https://github.com/Aharoni-Lab/Miniscope-DAQ-QT-Software/issues/62)

# Miniscope-DAQ-QT-Software

**[[Miniscope V4 Wiki](https://github.com/Aharoni-Lab/Miniscope-v4/wiki)] [[Miniscope DAQ Software Wiki](https://github.com/Aharoni-Lab/Miniscope-DAQ-QT-Software/wiki)] [[Miniscope DAQ Firmware Wiki](https://github.com/Aharoni-Lab/Miniscope-DAQ-Cypress-firmware/wiki)] [[Miniscope Wire-Free DAQ Wiki](https://github.com/Aharoni-Lab/Miniscope-Wire-Free-DAQ/wiki)] [[Miniscope-LFOV Wiki](https://github.com/Aharoni-Lab/Miniscope-LFOV/wiki)][[2021 Virtual Miniscope Workshop Recording](https://sites.google.com/metacell.us/miniscope-workshop-2021)]**

Neural and behavior control and recording software for the UCLA Miniscope Project

**Make sure to click Watch and Star in the upper right corner of this page to get updates on new features and releases.**

<p align="center">
  <img width="600" src="https://github.com/Aharoni-Lab/Miniscope-DAQ-QT-Software/blob/master/wikiImg/miniscope_bright.png ">
</p>


This backwards compatible Miniscope DAQ software is an upgrade and replacement to our previous DAQ software. It is written completely in QT, using C++ for the backend and QT Quick (QML and Java) for the front end. This software supports all current and previous Miniscopes but requires the most up-to-date version of the [Miniscope DAQ Firmware](https://github.com/Aharoni-Lab/Miniscope-DAQ-Cypress-firmware).

**All information can be found on the [Miniscope DAQ Software Wiki page](https://github.com/Aharoni-Lab/Miniscope-DAQ-QT-Software/wiki).**

Along with this repository holding the software's source code, you can get built release versions of the software on the [Releases Page](https://github.com/Aharoni-Lab/Miniscope-DAQ-QT-Software/releases).
