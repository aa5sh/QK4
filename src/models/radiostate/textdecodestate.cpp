#include "textdecodestate.h"

#include "models/radiostate.h"

#include <QtGlobal>

void TextDecodeState::reset() {
    textDecodeMode = -1;
    textDecodeThreshold = -1;
    textDecodeLines = -1;
    textDecodeModeB = -1;
    textDecodeThresholdB = -1;
    textDecodeLinesB = -1;
}

namespace {

int tdDigit(const QString &cmd, int pos, int min, int max) {
    bool ok = false;
    const int value = cmd.mid(pos, 1).toInt(&ok);
    return (ok && value >= min && value <= max) ? value : -1;
}

bool applyTdField(int &field, int value) {
    if (value < 0 || value == field)
        return false;
    field = value;
    return true;
}

bool applyTd(const QString &cmd, int offset, int &mode, int &threshold, int &lines) {
    if (cmd.length() < offset + 3)
        return false;
    using L = TextDecodeState;
    bool changed = applyTdField(mode, tdDigit(cmd, offset, L::ModeMin, L::ModeMax));
    changed |= applyTdField(threshold, tdDigit(cmd, offset + 1, L::ThresholdMin, L::ThresholdMax));
    changed |= applyTdField(lines, tdDigit(cmd, offset + 2, L::LinesMin, L::LinesMax));
    return changed;
}

} // namespace

namespace TextDecodeHandlers {

void handleTD(TextDecodeState &state, RadioState &owner, const QString &cmd) {
    if (applyTd(cmd, 2, state.textDecodeMode, state.textDecodeThreshold, state.textDecodeLines))
        emit owner.textDecodeChanged();
}

void handleTDSub(TextDecodeState &state, RadioState &owner, const QString &cmd) {
    if (applyTd(cmd, 3, state.textDecodeModeB, state.textDecodeThresholdB, state.textDecodeLinesB))
        emit owner.textDecodeBChanged();
}

void handleTB(RadioState &owner, const QString &cmd) {
    if (cmd.length() < 5)
        return;
    QString text = cmd.mid(5);
    if (text.endsWith(';'))
        text.chop(1);
    if (!text.isEmpty())
        emit owner.textBufferReceived(text, false);
}

void handleTBSub(RadioState &owner, const QString &cmd) {
    if (cmd.length() < 6)
        return;
    QString text = cmd.mid(6);
    if (text.endsWith(';'))
        text.chop(1);
    if (!text.isEmpty())
        emit owner.textBufferReceived(text, true);
}

void setMode(TextDecodeState &state, RadioState &owner, int mode) {
    mode = qBound(TextDecodeState::ModeMin, mode, TextDecodeState::ModeMax);
    if (mode != state.textDecodeMode) {
        state.textDecodeMode = mode;
        emit owner.textDecodeChanged();
    }
}

void setThreshold(TextDecodeState &state, RadioState &owner, int threshold) {
    threshold = qBound(TextDecodeState::ThresholdMin, threshold, TextDecodeState::ThresholdMax);
    if (threshold != state.textDecodeThreshold) {
        state.textDecodeThreshold = threshold;
        emit owner.textDecodeChanged();
    }
}

void setLines(TextDecodeState &state, RadioState &owner, int lines) {
    lines = qBound(TextDecodeState::LinesMin, lines, TextDecodeState::LinesMax);
    if (lines != state.textDecodeLines) {
        state.textDecodeLines = lines;
        emit owner.textDecodeChanged();
    }
}

void setModeB(TextDecodeState &state, RadioState &owner, int mode) {
    mode = qBound(TextDecodeState::ModeMin, mode, TextDecodeState::ModeMax);
    if (mode != state.textDecodeModeB) {
        state.textDecodeModeB = mode;
        emit owner.textDecodeBChanged();
    }
}

void setThresholdB(TextDecodeState &state, RadioState &owner, int threshold) {
    threshold = qBound(TextDecodeState::ThresholdMin, threshold, TextDecodeState::ThresholdMax);
    if (threshold != state.textDecodeThresholdB) {
        state.textDecodeThresholdB = threshold;
        emit owner.textDecodeBChanged();
    }
}

void setLinesB(TextDecodeState &state, RadioState &owner, int lines) {
    lines = qBound(TextDecodeState::LinesMin, lines, TextDecodeState::LinesMax);
    if (lines != state.textDecodeLinesB) {
        state.textDecodeLinesB = lines;
        emit owner.textDecodeBChanged();
    }
}

} // namespace TextDecodeHandlers
