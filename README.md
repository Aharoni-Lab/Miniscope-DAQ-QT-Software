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


## Building From Source

Version Compatibility

- Python 3.7
- Numpy <1.19
- Qt 5.*
- OpenCV (4.4.0 is specified in the project, compatibility unknown!)

### Steps

Edit the project file to specify the correct include directories for the above dependencies - Look for the area in the file that looks like this:

```
### UNTIL WE FIX THIS - you will need to substitute with your own include directories. These are just stubs to show what the include paths should look like
INCLUDEPATH += python3.7/site-packages/numpy/core/include/
INCLUDEPATH += include/python3.7m/
INCLUDEPATH += /usr/local/include/opencv4/opencv2/
```


Create the Makefile from the Miniscope-DAQ-QT-Software.pro file with qmake:

```shell
qmake -makefile -o Makefile Miniscope-DAQ-QT-Software.pro
```

Make the dang stuff!

```shell
make
```