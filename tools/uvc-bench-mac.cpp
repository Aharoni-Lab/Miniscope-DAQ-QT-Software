// Bench-validation CLI for the macOS UVC control transport (uvccontrolmac).
//
// Purpose: prove, with a real Miniscope on the bench, that pipe-0 control
// requests work at sustained per-frame rates while the device streams - the
// two open questions of the macOS port. Run it (a) with the device idle and
// (b) while another app streams from the Miniscope (QuickTime, Photo Booth,
// or this repo's app) and compare.
//
//   uvc-bench-mac --list                      # show connected USB devices
//   uvc-bench-mac                             # poll a Miniscope for 10 s @ 30 Hz
//   uvc-bench-mac --rate 60 --seconds 30      # harder polling
//   uvc-bench-mac --enable-bno                # SET_CUR SATURATION=1 first, then poll
//   uvc-bench-mac --vid 0x046d --pid 0x085e   # any UVC camera (e.g. a webcam)
//   uvc-bench-mac --location 0x14200000       # disambiguate identical devices
//
// What "good" looks like: 0 failed transfers, the frame counter (CONTRAST)
// strictly increasing while streaming, BNO words changing when the scope
// moves, and per-read latency well under the frame period.

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QThread>
#include <QSet>

#include <cstdio>

#include "miniscopeprotocol.h"
#include "uvccontrolmac.h"

using namespace MiniscopeProtocol;

static quint16 parseU16(const QString &s) { return quint16(s.toUInt(nullptr, 0)); }

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("uvc-bench-mac");

    QCommandLineParser parser;
    parser.setApplicationDescription("Miniscope UVC pipe-0 control bench (macOS)");
    parser.addHelpOption();
    parser.addOption({"list", "List connected USB devices and exit."});
    parser.addOption({"vid", "USB vendor ID (default: Miniscope DAQ).", "hex", "0x" + QString::number(kUsbVendorId, 16)});
    parser.addOption({"pid", "USB product ID (default: Miniscope DAQ).", "hex", "0x" + QString::number(kUsbProductId, 16)});
    parser.addOption({"location", "USB locationID to disambiguate identical devices.", "hex", "0"});
    parser.addOption({"rate", "GET_CUR poll rate in Hz.", "hz", "30"});
    parser.addOption({"seconds", "How long to poll.", "s", "10"});
    parser.addOption({"enable-bno", "Send SET_CUR SATURATION=1 first (starts the DAQ's BNO/frame register refresh)."});
    parser.process(app);

    if (parser.isSet("list")) {
        const auto devices = UVCControlMac::enumerate();
        printf("%-6s %-6s %-12s %s\n", "VID", "PID", "locationID", "name");
        for (const auto &d : devices)
            printf("0x%04x 0x%04x 0x%08x   %s\n", d.vid, d.pid, d.locationID,
                   d.name.isEmpty() ? "(unnamed)" : qPrintable(d.name));
        return 0;
    }

    const quint16 vid = parseU16(parser.value("vid"));
    const quint16 pid = parseU16(parser.value("pid"));
    const quint32 location = parser.value("location").toUInt(nullptr, 0);
    const int rateHz = qMax(1, parser.value("rate").toInt());
    const int seconds = qMax(1, parser.value("seconds").toInt());

    UVCControlMac ctrl;
    // The DAQ's write-settle pause; reads (the polling below) never pay it.
    ctrl.setWriteSettleUs(kCtrlSettleUs);
    if (!ctrl.open(vid, pid, location)) {
        fprintf(stderr, "open failed: %s\n", qPrintable(ctrl.lastError()));
        fprintf(stderr, "hint: run with --list to see connected devices\n");
        return 1;
    }
    printf("opened %04x:%04x at locationID 0x%08x, VideoControl interface #%u\n",
           vid, pid, ctrl.locationID(), ctrl.vcInterfaceNumber());

    if (parser.isSet("enable-bno")) {
        if (ctrl.setCur(kProcessingUnitId, SEL_SATURATION, 0x0001))
            printf("SET_CUR SATURATION=1 ok (BNO/frame register refresh enabled)\n");
        else
            fprintf(stderr, "SET_CUR SATURATION=1 FAILED: %s\n", qPrintable(ctrl.lastError()));
    }

    // Poll loop: the reads the capture backend will issue every frame.
    const struct { const char *name; quint8 sel; } regs[] = {
        {"frameNum(CONTRAST) ", SEL_CONTRAST},
        {"bnoW(SATURATION)   ", SEL_SATURATION},
        {"bnoX(HUE)          ", SEL_HUE},
        {"bnoY(GAIN)         ", SEL_GAIN},
        {"bnoZ(BRIGHTNESS)   ", SEL_BRIGHTNESS},
    };
    constexpr size_t kNumRegs = sizeof(regs) / sizeof(regs[0]);
    const int totalPolls = rateHz * seconds;
    const qint64 periodUs = 1000000 / rateHz;

    int failures = 0;
    int regFailures[kNumRegs] = {};
    QString regLastError[kNumRegs];
    int frameIncrements = 0, frameStalls = 0, frameBackwards = 0;
    qint16 lastFrame = -1;
    QSet<quint16> bnoValuesSeen;
    qint64 worstUs = 0, totalUs = 0;
    QElapsedTimer wall, one;

    printf("polling %d registers at %d Hz for %d s (%d cycles)...\n",
           int(kNumRegs), rateHz, seconds, totalPolls);
    wall.start();
    for (int i = 0; i < totalPolls; i++) {
        one.start();
        for (size_t ri = 0; ri < kNumRegs; ri++) {
            const auto &r = regs[ri];
            quint16 v = 0;
            if (!ctrl.getCur(kProcessingUnitId, r.sel, &v)) {
                failures++;
                regFailures[ri]++;
                regLastError[ri] = ctrl.lastError();
                continue;
            }
            if (r.sel == SEL_CONTRAST) {
                const qint16 f = qint16(v);
                if (lastFrame >= 0) {
                    if (f > lastFrame) frameIncrements++;
                    else if (f == lastFrame) frameStalls++;
                    else frameBackwards++;   // wrap or reset
                }
                lastFrame = f;
            } else {
                bnoValuesSeen.insert(v);
            }
        }
        const qint64 tookUs = one.nsecsElapsed() / 1000;
        totalUs += tookUs;
        worstUs = qMax(worstUs, tookUs);
        if (tookUs < periodUs)
            QThread::usleep(ulong(periodUs - tookUs));
    }
    const double wallS = wall.elapsed() / 1000.0;

    printf("\n--- report -------------------------------------------------\n");
    printf("wall time            : %.1f s (target %d s)\n", wallS, seconds);
    printf("transfers            : %d (%d failed)\n",
           totalPolls * int(kNumRegs), failures);
    for (size_t ri = 0; ri < kNumRegs; ri++)
        if (regFailures[ri])
            printf("  %s: %d/%d failed (last: %s)\n", regs[ri].name, regFailures[ri],
                   totalPolls, qPrintable(regLastError[ri]));
    printf("poll cycle (5 reads) : avg %lld us, worst %lld us (budget %lld us)\n",
           totalUs / totalPolls, worstUs, periodUs);
    printf("frame counter        : %d increments, %d stalls, %d backwards\n",
           frameIncrements, frameStalls, frameBackwards);
    printf("distinct BNO words   : %d %s\n", int(bnoValuesSeen.size()),
           bnoValuesSeen.size() <= 1 ? "(static - is streaming on? try --enable-bno / move the scope)" : "");
    printf("--------------------------------------------------------------\n");
    printf("PASS criteria: 0 failed, frame counter increments while streaming,\n"
           "BNO words vary when the scope moves, worst cycle << frame period.\n");
    return failures == 0 ? 0 : 2;
}
