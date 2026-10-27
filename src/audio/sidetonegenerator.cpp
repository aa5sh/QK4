#include "sidetonegenerator.h"
#include "audio/audiologging.h"
#include "audio/sidetonescheduler.h"
#include <QAudioFormat>
#include <QMediaDevices>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>

namespace {
constexpr double kPi = 3.14159265358979323846; // not M_PI: MSVC hides it behind _USE_MATH_DEFINES
} // namespace

// The pull-mode source: every read is rendered on the spot by the generator.
class SidetoneGenerator::Source : public QIODevice {
public:
    explicit Source(SidetoneGenerator *owner) : QIODevice(owner), m_owner(owner) {}
    bool isSequential() const override { return true; }
    // WHY report plenty available: some pull-mode backends only read what bytesAvailable() admits
    // to, and a generator always has more to give.
    qint64 bytesAvailable() const override { return std::numeric_limits<int>::max() + QIODevice::bytesAvailable(); }

protected:
    qint64 readData(char *data, qint64 maxlen) override { return m_owner->render(data, maxlen); }
    qint64 writeData(const char *, qint64) override { return -1; }

private:
    SidetoneGenerator *m_owner;
};

SidetoneGenerator::SidetoneGenerator(SidetoneScheduler &scheduler, CwKeying::Clock &clock, QObject *parent)
    : QObject(parent), m_scheduler(scheduler), m_clock(clock) {
    // Audio init deferred to start() which runs on the sidetone thread.
    // WHY parented here, pre-moveToThread: the monitor rides moveToThread() with the
    // generator, so audioOutputsChanged is delivered on the sidetone thread — the same
    // thread that owns the sink. Same pattern as AudioEngine.
    m_mediaDevices = new QMediaDevices(this);
    connect(m_mediaDevices, &QMediaDevices::audioOutputsChanged, this,
            &SidetoneGenerator::onSystemDefaultOutputChanged);
}

SidetoneGenerator::~SidetoneGenerator() {
    // Audio sink should already be cleaned up by stop() on the correct thread.
    if (m_audioSink) {
        qCWarning(qk4Audio) << "SidetoneGenerator: audio sink not cleaned up by stop() — destroying from wrong thread";
        m_streamActive = false;
        delete m_audioSink;
        m_audioSink = nullptr;
    }
}

std::optional<double> SidetoneGenerator::streamTime() const {
    if (!m_streamActive.load(std::memory_order_acquire))
        return std::nullopt;
    return m_clock.now();
}

void SidetoneGenerator::setOutputDevice(const QString &deviceId) {
    if (m_selectedOutputDeviceId == deviceId)
        return;
    m_selectedOutputDeviceId = deviceId;
    if (m_audioSink) {
        closeAudio();
        initAudio();
    }
}

void SidetoneGenerator::initAudio() {
    QAudioDevice device;
    if (!m_selectedOutputDeviceId.isEmpty()) {
        for (const QAudioDevice &d : QMediaDevices::audioOutputs()) {
            if (d.id() == m_selectedOutputDeviceId) {
                device = d;
                break;
            }
        }
    }
    if (device.isNull())
        device = QMediaDevices::defaultAudioOutput();

    // Record the actually-opened device so onSystemDefaultOutputChanged can detect
    // whether the effective default really moved. Set before sink creation so even
    // a failed start() records what was attempted (same ordering as AudioEngine).
    m_activeOutputDeviceId = device.id();

    QAudioFormat format;
    format.setSampleRate(48000);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Float);
    if (!device.isFormatSupported(format)) {
        format.setSampleFormat(QAudioFormat::Int16);
        if (!device.isFormatSupported(format)) {
            // WHY the device's own format, rendered as it asks: the old generator switched to
            // preferredFormat() but kept writing 48 kHz mono Int16 into it. Every rate, channel
            // count and sample format below is rendered to what was actually opened.
            format = device.preferredFormat();
        }
    }
    m_format = format;
    m_rate = format.sampleRate();
    if (m_rate <= 0 || format.bytesPerFrame() <= 0) {
        qCWarning(qk4Audio) << "SidetoneGenerator: unusable output format" << format;
        return;
    }
    m_blockFrames = std::max(1, static_cast<int>(std::lround(m_rate * kBlockSeconds)));
    m_riseStep = 1.0 / std::max(1L, std::lround(m_rate * kRiseSeconds));
    m_fallStep = 1.0 / std::max(1L, std::lround(m_rate * kFallSeconds));
    m_scheduler.setMinimumSidetoneMs(static_cast<int>(std::lround((kRiseSeconds * .5 + kFallSeconds * .5) * 1000)));
    m_keyed = false;
    m_envelope = 0.0;
    m_framesWritten = 0;
    m_toneFrequency = 0;
    rebuildTone();

    m_audioSink = new QAudioSink(device, format, this);
    m_audioSink->setBufferSize(
        static_cast<qsizetype>(format.bytesForDuration(static_cast<qint64>(kBufferSeconds * 1e6))));
    m_source = new Source(this);
    m_source->open(QIODevice::ReadOnly);
    // A new stream: every timing estimate from the old one is void.
    m_scheduler.clear();
    m_streamActive.store(true, std::memory_order_release);
    m_audioSink->start(m_source);
    if (m_audioSink->error() != QAudio::NoError) {
        qCWarning(qk4Audio) << "SidetoneGenerator: Failed to start audio sink:" << m_audioSink->error();
        m_streamActive.store(false, std::memory_order_release);
    } else {
        qCInfo(qk4Audio) << "SidetoneGenerator: sidetone on" << device.description() << format;
    }
}

void SidetoneGenerator::closeAudio() {
    m_streamActive.store(false, std::memory_order_release);
    if (m_audioSink) {
        m_audioSink->stop();
        delete m_audioSink;
        m_audioSink = nullptr;
    }
    delete m_source;
    m_source = nullptr;
    m_scheduler.clear();
}

void SidetoneGenerator::start() {
    initAudio();
}

void SidetoneGenerator::stop() {
    closeAudio();
    m_activeOutputDeviceId.clear();
}

void SidetoneGenerator::onSystemDefaultOutputChanged() {
    // Only follow the OS default when the user hasn't pinned a specific device.
    if (!m_selectedOutputDeviceId.isEmpty())
        return;
    // Nothing built yet (or stop() already ran) — the next initAudio() resolves fresh.
    if (!m_audioSink)
        return;
    const QString newDefault = QMediaDevices::defaultAudioOutput().id();
    if (newDefault.isEmpty() || newDefault == m_activeOutputDeviceId)
        return; // effective default unchanged
    closeAudio();
    initAudio();
}

void SidetoneGenerator::setFrequency(int hz) {
    m_frequency.store(hz, std::memory_order_relaxed);
}

void SidetoneGenerator::setVolume(float volume) {
    m_volume.store(volume, std::memory_order_relaxed);
}

void SidetoneGenerator::rebuildTone() {
    const int pitch = std::max(1, m_frequency.load(std::memory_order_relaxed));
    if (pitch == m_toneFrequency && !m_cycle.empty())
        return;
    m_toneFrequency = pitch;
    // One exact period: rate / gcd(rate, pitch) samples, so the table wraps with no phase error.
    const int period = m_rate / std::gcd(m_rate, pitch);
    m_cycle.resize(static_cast<size_t>(period));
    for (int i = 0; i < period; ++i)
        m_cycle[static_cast<size_t>(i)] =
            static_cast<float>(std::sin(2 * kPi * pitch * static_cast<double>(i) / m_rate));
    m_cycleIndex %= m_cycle.size();
}

void SidetoneGenerator::renderBlock(float *out, int frames, double dacTime, double now) {
    m_scheduler.noteBlockTiming(dacTime, now, static_cast<double>(frames) / m_rate);
    const bool idle = !m_keyed && m_envelope <= 0 && !m_scheduler.pendingEdgeHeld();
    if (idle)
        m_scheduler.easeLag(static_cast<double>(frames) / m_rate);
    if (idle && m_scheduler.queueEmpty()) {
        std::fill(out, out + frames, 0.0f);
        return;
    }
    m_positions.resize(static_cast<size_t>(frames));
    int start = 0;
    while (start < frames) {
        const auto edge = m_scheduler.nextKeyEdge(start, frames, dacTime, m_rate);
        const int end = edge ? edge->offset : frames;
        const double step = m_keyed ? m_riseStep : -m_fallStep;
        for (int i = start; i < end; ++i) {
            m_envelope = std::clamp(m_envelope + step, 0.0, 1.0);
            m_positions[static_cast<size_t>(i)] = static_cast<float>(m_envelope);
        }
        if (!edge)
            break;
        m_keyed = edge->down;
        start = end;
    }
    rebuildTone();
    const float volume = m_volume.load(std::memory_order_relaxed);
    for (int i = 0; i < frames; ++i) {
        const double env = .5 * (1 - std::cos(kPi * m_positions[static_cast<size_t>(i)]));
        out[i] = m_cycle[m_cycleIndex] * static_cast<float>(env) * volume;
        m_cycleIndex = (m_cycleIndex + 1) % m_cycle.size();
    }
}

qint64 SidetoneGenerator::render(char *data, qint64 maxlen) {
    const int bytesPerFrame = m_format.bytesPerFrame();
    if (!m_audioSink || bytesPerFrame <= 0 || m_rate <= 0)
        return 0;
    const qint64 frames = maxlen / bytesPerFrame;
    if (frames <= 0)
        return 0;

    // Play-out time of the first frame rendered now: everything written so far that the device
    // hasn't played yet is queued ahead of it. The device's own fixed latency beyond that is the
    // same for every block, which is all the scheduler needs.
    const double now = m_clock.now();
    const double played = static_cast<double>(m_audioSink->processedUSecs()) / 1e6;
    const double written = static_cast<double>(m_framesWritten) / m_rate;
    const double queued = std::max(0.0, written - played);

    const int channels = m_format.channelCount();
    const auto sampleFormat = m_format.sampleFormat();
    const int bytesPerSample = m_format.bytesPerSample();
    m_scratch.resize(static_cast<size_t>(m_blockFrames));
    qint64 done = 0;
    while (done < frames) {
        const int n = static_cast<int>(std::min<qint64>(frames - done, m_blockFrames));
        const double dac = now + queued + static_cast<double>(done) / m_rate;
        renderBlock(m_scratch.data(), n, dac, now);
        char *dst = data + done * bytesPerFrame;
        for (int i = 0; i < n; ++i) {
            const float v = m_scratch[static_cast<size_t>(i)];
            for (int c = 0; c < channels; ++c) {
                char *p = dst + (static_cast<qint64>(i) * channels + c) * bytesPerSample;
                switch (sampleFormat) {
                case QAudioFormat::Float:
                    std::memcpy(p, &v, sizeof v);
                    break;
                case QAudioFormat::Int16: {
                    const auto s = static_cast<qint16>(std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f));
                    std::memcpy(p, &s, sizeof s);
                    break;
                }
                case QAudioFormat::Int32: {
                    const auto s = static_cast<qint32>(std::llround(std::clamp(v, -1.0f, 1.0f) * 2147483647.0));
                    std::memcpy(p, &s, sizeof s);
                    break;
                }
                case QAudioFormat::UInt8: {
                    const auto s = static_cast<quint8>(std::lround(std::clamp(v, -1.0f, 1.0f) * 127.0f + 128.0f));
                    std::memcpy(p, &s, sizeof s);
                    break;
                }
                default:
                    std::memset(p, 0, static_cast<size_t>(bytesPerSample));
                    break;
                }
            }
        }
        done += n;
    }
    m_framesWritten += frames;
    return frames * bytesPerFrame;
}
