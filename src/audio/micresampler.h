#ifndef MICRESAMPLER_H
#define MICRESAMPLER_H

#include <cmath>
#include <numeric>
#include <vector>

// Causal polyphase windowed-sinc FIR. Integer phase accounting gives exactly
// floor(inputFrames * 12000 / inputRate) samples, independent of buffer boundaries.
// About 2 ms group delay; coefficients are calculated only when opening a device.
class MicResampler {
public:
    bool configure(int rate) {
        if (rate < 12000 || rate > 192000)
            return false;
        m_rate = rate;
        m_step = std::gcd(rate, 12000);
        m_half = static_cast<int>(std::ceil(24.0 * rate / 12000));
        m_taps = 2 * m_half + 1;
        m_history.assign(m_taps, 0.0f);
        m_coeff.assign(12000 / m_step, std::vector<float>(m_taps));
        constexpr double pi = 3.14159265358979323846;
        const double cutoff = 5000.0 / rate;
        for (int p = 0; p < static_cast<int>(m_coeff.size()); ++p) {
            double sum = 0;
            for (int k = 0; k < m_taps; ++k) {
                const double x = k - p * m_step / 12000.0 - m_half;
                const double window = std::abs(x) <= m_half
                    ? 0.42 + 0.5 * std::cos(pi * x / m_half) + 0.08 * std::cos(2 * pi * x / m_half) : 0;
                const double c = (std::abs(x) < 1e-12 ? 2 * cutoff
                    : std::sin(2 * pi * cutoff * x) / (pi * x)) * window;
                m_coeff[p][k] = static_cast<float>(c);
                sum += c;
            }
            for (auto &c : m_coeff[p])
                c = static_cast<float>(c / sum);
        }
        m_phase = 0;
        m_head = 0;
        return true;
    }

    // Append mono float output; history and phase survive every call, including PTT edges.
    void process(const float *mono, int count, std::vector<float> &out) {
        if (!m_rate)
            return;
        for (int i = 0; i < count; ++i) {
            m_history[m_head] = mono[i];
            m_phase += 12000;
            if (m_phase >= m_rate) {
                m_phase -= m_rate;
                if (m_rate == 12000) {
                    out.push_back(mono[i]);
                } else {
                    const auto &coeff = m_coeff[m_phase / m_step];
                    double sum = 0;
                    int pos = m_head;
                    for (int k = 0; k < m_taps; ++k) {
                        sum += m_history[pos] * coeff[k];
                        if (--pos < 0)
                            pos = m_taps - 1;
                    }
                    out.push_back(static_cast<float>(sum));
                }
            }
            if (++m_head == m_taps)
                m_head = 0;
        }
    }

private:
    int m_rate = 0, m_step = 1, m_half = 0, m_taps = 0, m_phase = 0, m_head = 0;
    std::vector<float> m_history;
    std::vector<std::vector<float>> m_coeff;
};
#endif
