// SPDX-License-Identifier: GPL-3.0-or-later
//
// Paddle and straight-key keyer for HaliKey (serial and MIDI). A port of the Keyer class of Dale
// Farnsworth's K4-Companion (GPL-3.0, https://github.com/DaleFarnsworth/K4-Companion).
//
// What goes on the air is carried in timed key commands: KZD#### (key down after a gap of ####
// ms) and KZU#### (key up after #### ms down). The K4 replays those intervals behind its KZL
// key-down delay, so thread wake-ups and network jitter cannot change an element's length. Paddle
// edges are timestamped at capture (MoMIDI's own clock, or the 1 ms serial poll), and every
// decision reads the paddles as they stood when it was due. Iambic memories are measured from when
// the previous element was heard.
//
// Threads: paddleEvent/midiMessage/serialSignalChanged on the device thread; processNextEvent/run on
// the keyer thread; configuration and releasePaddles from any thread.
//
// KPOD+ keying does not pass through here. See docs/cw-keyer-port-plan.md.

#ifndef PADDLEKEYER_H
#define PADDLEKEYER_H

#include "hardware/keyertiming.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class SidetoneScheduler;

class PaddleKeyer {
public:
    enum class Mode { PlainIambic, IambicA, IambicB, Ultimatic };
    enum class Event { DitDown, DitUp, DahDown, DahUp, Close };
    enum class Signal { CTS, DCD, DSR, RI };

    PaddleKeyer(CwKeying::Clock &clock, CwKeying::Link &link, SidetoneScheduler &sidetone,
                CwKeying::OneShotTimer &unmuteTimer);
    ~PaddleKeyer();

    PaddleKeyer(const PaddleKeyer &) = delete;
    PaddleKeyer &operator=(const PaddleKeyer &) = delete;

    // ---- configuration (any thread) ----
    void setMode(Mode mode);
    Mode mode() const;
    void setStraightKeying(bool straight);         // straight key or external keyer passthrough
    void setDitMemoryDelayPercent(double percent); // Iambic B only; 0 disables
    void setSpeed(int wpm, double weighting = 1.0);
    void setDitNotes(std::vector<int> notes);
    void setDahNotes(std::vector<int> notes);
    void setDitSignal(Signal signal);
    void setDahSignal(Signal signal);
    // Mute the K4's CW monitor for the length of each burst (ML0000, restored afterwards). Off by
    // default: whether the K4 already suppresses its monitor for KZD/KZU is a bench question.
    void setMuteRadioMonitor(bool mute);
    void setMonitorLevel(int level);       // the ML0 value restored after a burst
    void setKeyDownInitialDelayMs(int ms); // the K4's KZL value
    // While inhibited (KPOD+ owns CW, not in CW mode, link down) nothing is keyed: whatever is keyed
    // is released and paddle events are dropped at the door.
    void setInhibited(bool inhibited);

    double ditSeconds() const;
    double dahSeconds() const;
    double interElementSeconds() const;

    // ---- device threads ----
    void paddleEvent(Event event, double deltaTime, std::optional<double> timestamp = std::nullopt);
    void midiMessage(int status, int note, int velocity, double rtmidiDelta);
    void serialSignalChanged(Signal signal, bool asserted, double deltaTime);

    // Ends whatever is keyed: the running burst stops at its next element boundary with the
    // current element sent whole; a straight key held down gets its KZU now. closing = true also
    // refuses every later event.
    void releasePaddles(bool closing = false);

    // ---- keyer thread ----
    bool hasPendingEvents() const;
    Event processNextEvent(); // one pass of the keyer loop; blocks until an event arrives
    void run();               // the keyer loop, until Close
    // Releases, posts Close and refuses everything after. The owner joins the keyer thread.
    void close();

private:
    using Element = std::optional<char>; // '.', '-', or none
    using Chooser = std::function<Element(Element prev, std::optional<double> look, std::optional<double> heard,
                                          std::optional<double> asOf)>;

    struct PaddleEventItem {
        Event event;
        double timestamp;
    };
    struct Stamps {
        double ditUp, dahUp, ditDown, dahDown;
    };
    struct FirstElement {
        double keyedStart;
        char element;
        double closedAt;
    };

    PaddleEventItem popEvent();
    void pushEvent(PaddleEventItem item);

    Stamps paddleTimestamps(std::optional<double> asOf) const;
    void sendStraightKey(const PaddleEventItem &item);
    void sendIambic();
    void sendUltimatic();
    void sendBurst(const Chooser &choose);
    Element missedFirstElement(std::optional<double> endedDue);
    Element placedFirstElement();
    std::optional<FirstElement> takeFirstElement();
    void startBurstSidetone(Event event, double timestamp, double pressed);
    bool startElement(bool firstElement, const std::function<bool(double)> &wanted);
    void planSidetoneCatchup(bool firstElement);
    bool confirmElement(double sidetoneStart, const std::function<bool(double)> &wanted, double gridShift);
    std::pair<double, double> elementSidetone(double elementSeconds) const;
    double upEdgeMargin(double ditSidetoneSeconds) const;
    void waitUntilSidetoneHeard();
    void endElement(char element);
    void measureInherentSidetoneLag();
    void endStraightKeyOnAir(double keyDownAt);
    long long straightKeyMs(double seconds) const;
    double midiEventTimestamp(double delta);
    std::optional<double> sleepUntil(double when);
    void send(const std::string &command);
    void muteSidetoneMonitor();
    void delayedUnmuteSidetoneMonitor(double delay);
    void unmuteSidetoneMonitor();
    static double wholeMs(double seconds);

    CwKeying::Clock &m_clock;
    CwKeying::Link &m_link;
    SidetoneScheduler &m_sidetone;
    CwKeying::OneShotTimer &m_unmuteTimer;

    // ---- configuration ----
    mutable std::mutex m_configMutex;
    Mode m_mode = Mode::IambicB;
    std::atomic<bool> m_straightKeying{false};
    double m_ditMemoryDelayPercent = 33.0;
    std::vector<int> m_ditNotes{20};
    std::vector<int> m_dahNotes{21};
    Signal m_ditSignal = Signal::DSR;
    Signal m_dahSignal = Signal::DCD;
    std::atomic<bool> m_muteRadioMonitor{false};
    std::atomic<int> m_monitorLevel{0};
    std::atomic<int> m_keyDownInitialDelayMs{0};
    std::atomic<bool> m_inhibited{false};

    // Element timing from on_speed_change; written under m_configMutex, read by the keyer thread.
    double m_ditSeconds = 0;
    double m_dahSeconds = 0;
    double m_interElementSeconds = 0;

    // ---- event queue (device -> keyer) ----
    mutable std::mutex m_queueMutex;
    std::condition_variable m_queueCv;
    std::deque<PaddleEventItem> m_queue;

    // ---- paddle state (device thread writes, keyer reads) ----
    mutable std::mutex m_paddleMutex;
    Stamps m_stamps{0, 0, 0, 0};
    std::deque<std::pair<double, Event>> m_history; // capped at kHistoryLength

    // ---- MIDI decoding (device thread only) ----
    double m_midiDelta = 0;
    bool m_ditWasDown = false;
    bool m_dahWasDown = false;
    double m_midiDeviceClock = 0.0;
    std::optional<double> m_midiClockOffset;

    // ---- first element / straight key: K4-Companion's first_element_lock ----
    std::mutex m_firstElementMutex;
    std::optional<FirstElement> m_firstElementScheduled;
    std::atomic<bool> m_inBurst{false};
    bool m_closed = false;
    bool m_straightKeyDown = false;
    double m_straightKeyDownAt = 0.0; // read only under m_firstElementMutex, like its writer

    // ---- straight-key on-air state (keyer thread, and releasePaddles from any thread) ----
    std::mutex m_straightMutex;
    double m_paddleTimestamp = 0;
    double m_previousKeyDownTimestamp = 0;
    double m_previousKeyUpTimestamp = 0;
    std::atomic<bool> m_k4Keyed{false};

    // ---- burst state (keyer thread only) ----
    std::optional<double> m_burstEndDue;
    std::optional<double> m_firstElementClosedAt;
    std::optional<double> m_elementTimestamp;
    std::optional<double> m_endElementTimestamp;
    std::optional<double> m_endSidetoneTimestamp;
    double m_sidetoneDelayUsed = 0.0;
    double m_sidetonePendingStep = 0.0;
    double m_sidetoneDelaySeen = 0.0;
    double m_sidetoneBehind = 0.0;
    double m_elementKeyedStart = 0.0;
    double m_elementSidetoneStart = 0.0;
    double m_elementKzdTimestamp = 0.0;
    double m_elementHeardStart = 0.0;
    std::optional<double> m_elementReadDue;
    bool m_elementSidetoneQueued = false;
    bool m_elementFirst = false;
    double m_inherentSidetoneLag = 0.0;
    double m_wakeOverrun = 0.0;
    std::optional<long long> m_lastKzdMs;

    // ---- monitor mute ----
    std::mutex m_monitorMutex;
    bool m_monitorMuted = false;

    static constexpr double kUnmuteExtraDelay = 0.5;
    static constexpr double kTolerableSidetoneLagMargin = .001;
    static constexpr double kMaxSidetoneCatchupFraction = .03;
    static constexpr double kSidetoneHeardPollSeconds = .001;
    static constexpr double kSidetoneEdgeMarginSeconds = .002;
    static constexpr double kWakeOverrunDecay = .98;
    static constexpr double kMaxWakeOverrun = .010;
    static constexpr double kMidiReanchorSeconds = 1.0;
    static constexpr size_t kHistoryLength = 64;
};

#endif // PADDLEKEYER_H
