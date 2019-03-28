// Copyright (c) 2018 Thomas Zueblin
//
// Author: Thomas Zueblin (thomas.zueblin@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "Sequencer.h"

#define MUTE_DIM_FACTOR 20

Sequencer::Sequencer() {
    for (int i = 0; i < NUMBER_OF_FUNCTIONBUTTONS; i++) {
        functionButtons[i] = Bounce();
        functionButtons[i].attach(SHIFT_IN_DATA_PIN);
    }

    for (int i = 0; i < NUMBER_OF_TRACKBUTTONS; i++) {
        trackButtons[i] = Bounce();
        trackButtons[i].attach(SHIFT_IN_DATA_PIN);
    }

    for (int i = 0; i < NUMBER_OF_STEPBUTTONS; i++) {
        stepButtons[i] = Bounce();
        stepButtons[i].attach(SHIFT_IN_DATA_PIN);
    }

    for (int i = 0; i < NUM_LEDS; i++) {
        leds[i] = CRGB::Black;
    }

    for (int i = 0; i < NUMBER_OF_INSTRUMENTTRACKS; i++) {
        tracks[i].setTrackNum(i);
    }
}

void Sequencer::doTriggerSounds() {
    input1.update((uint16_t)analogRead(POTI_PIN_1));
    input2.update((uint16_t)analogRead(POTI_PIN_2)); 
    for (int i = 0; i < NUMBER_OF_INSTRUMENTTRACKS; i++) {
        SequencerStep &step = tracks[i].getCurrentStep();
        if (step.isParameterLockOn()) {
            switch (pLockParamSet) {
                case PLockParamSet::SET1:
                    if (input1.isActive()) {
                        step.parameter1 = input1.getValue();
                    }
                    if (input2.isActive()) {
                        step.parameter2 = input2.getValue();
                    }
                    break;
                case PLockParamSet::SET2:

                    if (input1.isActive()) {
                        step.parameter3 = input1.getValue();
                    }
                    if (input2.isActive()) {
                        step.parameter4 = input2.getValue();
                    }
                    break;

                case PLockParamSet::SET3:

                    if (input1.isActive()) {
                        step.parameter5 = input1.getValue();
                    }
                    if (input2.isActive()) {
                        step.parameter6 = input2.getValue();
                    }
                    break;
            }
        }
        if (!tracks[i].isMuted() && step.isTriggerOn()) {
            audioChannels[i]->setParam1(step.parameter1);
            audioChannels[i]->setParam2(step.parameter2);
            audioChannels[i]->setParam3(step.parameter3);
            audioChannels[i]->setParam4(step.parameter4);
            audioChannels[i]->setParam5(step.parameter5);
            audioChannels[i]->setParam6(step.parameter6);
            audioChannels[i]->trigger();
        }
        
    }
    triggerSounds = false;
}

/*
* Increments the current step by one on all tracks.
*/
void Sequencer::doStep() {
    for (int i = 0; i < NUMBER_OF_INSTRUMENTTRACKS; i++) {
        tracks[i].doStep();
    }
    triggerSounds = true;
    stepCount++;
}

void Sequencer::start() {
    if (!running) {
        doStartStop();
    }
}

void Sequencer::stop() {
    if (running) {
        doStartStop();
    }
}

void Sequencer::updateState() {
    hasActivePLockReceivers = false;

    if (functionButtons[BUTTON_SET_PARAMSET_1].rose()) {
        pLockParamSet = PLockParamSet::SET1;
        deactivateSensors();
    } else if (functionButtons[BUTTON_SET_PARAMSET_2].rose()) {
        pLockParamSet = PLockParamSet::SET2;
        deactivateSensors();
    } else if (functionButtons[BUTTON_SET_PARAMSET_3].rose()) {
        pLockParamSet = PLockParamSet::SET3;
        deactivateSensors();
    }

    FunctionMode functionMode = calculateFunctionMode();
    switch (functionMode) {
        case FunctionMode::START_STOP:
            doStartStop();
            break;
        case FunctionMode::SET_TRACK_LENGTH:
            doSetTrackLength();
            break;
        case FunctionMode::TOGGLE_PLOCKS:
            doSetTrackPLock();
            break;
        case FunctionMode::LEAVE_TOGGLE_PLOCKS:
            doTurnOffPlockMode();
            break;
        case FunctionMode::TOGGLE_MUTES:
            doToggleTrackMuteArm();
            break;
        case FunctionMode::LEAVE_TOGGLE_MUTES:
            doUpdateMutes();
            break;
        case FunctionMode::PATTERN_OPS:
            doPatternOps();
            break;
        case FunctionMode::LEAVE_PATTERN_OPS:
            doLeavePatternOps();
            break;
        default:
            break;
    }

    if (functionMode != FunctionMode::TOGGLE_MUTES && functionMode != FunctionMode::PATTERN_OPS) {
        // if mute button is not pressed down, handle pressing track buttons as
        // normal track selection.
        doSetTrackSelection();
    }

    if (functionMode != FunctionMode::SET_TRACK_LENGTH && functionMode != FunctionMode::TOGGLE_PLOCKS && functionMode != FunctionMode::PATTERN_OPS) {
        // if not in set_length, plock or set pattern mode, handle step button
        // presses as normal trigger presses.
        doSetTriggers();
    }

    setFunctionButtonLights();

    if (running) {
        // check if we should step (internal clock / midi / triggers etc)
        if (shouldStep()){
            doStep();
        }
        // check if we should trigger the sounds. This is independent from doStep, since right after the sequencer is started
        // it does not step, but still trigger the sounds. The event sequence when starting looks like this:
        // 0              1              2              4         
        // Trigger....StepTrigger....StepTrigger....StepTrigger....
        if (triggerSounds){
            doTriggerSounds();
        }
    }

    // indicate current step
    if (running) {
        stepLED(tracks[selectedTrack].getCurrentPattern().currentStep) = CRGB::Red;
    }
}

FunctionMode Sequencer::calculateFunctionMode() {
    // BUTTON presses:
    // rose() -> button was pressed down
    // read() -> button is currently pressed down
    // fell() -> button was released

    // START STOP
    if (functionButtons[BUTTON_STARTSTOP].rose()) {
        return FunctionMode::START_STOP;
    }

    // PLOCKS
    if (functionButtons[BUTTON_TOGGLE_PLOCK].read()) {
        return FunctionMode::TOGGLE_PLOCKS;
    }
    if (functionButtons[BUTTON_TOGGLE_PLOCK].fell() && !trackOrStepButtonPressed) {
        // plock button was released without any steps or tracks
        // activated/deactivated -> leave plock mode
        return FunctionMode::LEAVE_TOGGLE_PLOCKS;
    }

    // MUTES
    if (functionButtons[BUTTON_TOGGLE_MUTE].read()) {
        return FunctionMode::TOGGLE_MUTES;
    }
    if (functionButtons[BUTTON_TOGGLE_MUTE].fell()) {
        // mute button was released -> active what was changed
        return FunctionMode::LEAVE_TOGGLE_MUTES;
    }

    // SET TRACK LENGTH
    if (functionButtons[BUTTON_SET_TRACKLENGTH].read()) {
        return FunctionMode::SET_TRACK_LENGTH;
    }

    // SWITCH PATTERN
    if (functionButtons[BUTTON_SET_PATTERN].read()) {
        return FunctionMode::PATTERN_OPS;
    }
    // SWITCH PATTERN
    if (functionButtons[BUTTON_SET_PATTERN].fell()) {
        return FunctionMode::LEAVE_PATTERN_OPS;
    }
    return FunctionMode::DEFAULT_MODE;
}

/*
 * Default mode (no mode button pressed). Checks for step button presses and
 * translates presses to triggers/untriggers. If more than one step button is
 * pressed down then this is a copy/paste operation. the buttons that was first
 * pressed is the source. Values from source are copied onto steps that are
 * pressed down in succession.
 */
void Sequencer::doSetTriggers() {
    // value stores if at least one button is pressed down.
    bool aButtonIsPressed = false;

    for (int i = 0; i < NUMBER_OF_STEPBUTTONS; i++) {
        SequencerStep &step = tracks[selectedTrack].getCurrentPattern().getStep(i);
        if (stepButtons[i].read()) {
            aButtonIsPressed = true;
            if (sourceStepIndex == -1) {
                // this is the first button that is pressed down (after no steps
                // were pressed). Register this step as source for (a possible,
                // to follow) copy operation.
                sourceStepIndex = i;
            } else if (i != sourceStepIndex) {
                // this is not the first button that is pressed down, so this is
                // a target step for copy (from source step)
                step.copyValuesFrom(tracks[selectedTrack].getCurrentPattern().getStep(sourceStepIndex));
                stepCopy = true;
            }
        }

        if (stepButtons[i].fell() && !stepCopy) {
            // toggle the step on/off
            step.toggleTriggerState();
        }
        stepLED(i) = colorForStepState(step.state);
    }
    if (!aButtonIsPressed) {
        // reset values needed for the copy operation as soon as no step buttons
        // are pressed at all
        sourceStepIndex = -1;
        stepCopy = false;
    }
}

/*
 * Set track length mode. Step button presses set the track length. Also handles changing the internal clock tempo and rotating patterns.
 */
void Sequencer::doSetTrackLength() {
    functionLED(BUTTON_SET_TRACKLENGTH) = CRGB::CornflowerBlue;
    for (int i = 0; i < NUMBER_OF_STEPBUTTONS; i++) {
        if (stepButtons[i].fell()) {
            tracks[selectedTrack].getCurrentPattern().trackLength = i + 1;
        }
        stepLED(i) = colorForStepState(tracks[selectedTrack].getCurrentPattern().getStep(i).state);
    }
    stepLED(tracks[selectedTrack].getCurrentPattern().trackLength - 1) = CRGB::Red;
    if (input1.isActive()) {
        stepLength = map(input1.getValue(), 0, 1024, 512, 32);
        nextStepTime = lastStepTime + stepLength;
    }
    if (input2.isActive()) {
        tracks[selectedTrack].getCurrentPattern().offset = 16 - (input2.getValue() / 64);
    }
}

/*
 * Toggles plock mode of all steps in a track
 */
void Sequencer::doSetTrackPLock() {
    functionLED(BUTTON_TOGGLE_PLOCK) = CRGB::DarkOrange;
    for (int i = 0; i < NUMBER_OF_INSTRUMENTTRACKS; i++) {
        if (trackButtons[i].fell()) {
            tracks[i].getCurrentPattern().togglePLockMode();
            trackOrStepButtonPressed = true;
        }
    }
    for (int i = 0; i < NUMBER_OF_STEPBUTTONS; i++) {
        if (stepButtons[i].fell()) {
            tracks[selectedTrack].getCurrentPattern().getStep(i).toggleParameterLockRecord();
            trackOrStepButtonPressed = true;
        }
        stepLED(i) = colorForStepState(tracks[selectedTrack].getCurrentPattern().getStep(i).state);
    }
    if (functionButtons[BUTTON_TOGGLE_PLOCK].rose()) {
        trackOrStepButtonPressed = false;
    }
}

void Sequencer::doStartStop() {
    running = !running;
    if (!running) {
        for (int i = 0; i < NUMBER_OF_INSTRUMENTTRACKS; i++) {
            tracks[i].onStop();
        }
        pulseCount = 0;
        stepCount = 0;
    } else {
        triggerSounds = true;
        nextStepTime = millis() + stepLength;
    }
}

void Sequencer::doToggleTrackMuteArm() {
    functionLED(BUTTON_TOGGLE_MUTE) = CRGB::CornflowerBlue;
    for (int i = 0; i < NUMBER_OF_INSTRUMENTTRACKS; i++) {
        if (trackButtons[i].fell()) {
            tracks[i].toggleMuteArm();
        }
    }
    ledFader++;
    if (ledFader > 200) ledFader = 10;
    for (int i = 0; i < NUMBER_OF_INSTRUMENTTRACKS; i++) {
        if (!tracks[i].isMuted() && tracks[i].isArmed()) {
            trackLED(i) = CRGB::CornflowerBlue;
            trackLED(i).nscale8(255 - ledFader);
        } else if (tracks[i].isMuted() && tracks[i].isArmed()) {
            trackLED(i) = CRGB::CornflowerBlue;
            trackLED(i).nscale8(ledFader);
        } else {
            setDefaultTrackLight(i);
        }
    }
}

/*
 * Pattern Ops: Operations related to patterns: arm / dearm switching patterns / copy paste.
 */
void Sequencer::doPatternOps() {
    functionLED(BUTTON_SET_PATTERN) = CRGB::CornflowerBlue;
    ledFader++;
    if (ledFader > 200) ledFader = 10;
    for (int i = 0; i < NUMBER_OF_INSTRUMENTTRACKS; i++) {
        if (trackButtons[i].fell()) {
            tracks[i].togglePatternOpsArm();
        }
        if (tracks[i].isPatternOpsArmed()) {
            trackLED(i) = CRGB::CornflowerBlue;
            trackLED(i).nscale8(255 - ledFader);
        } else {
            setDefaultTrackLight(i);
        }
    }

    uint8_t currentPatternIndex = tracks[selectedTrack].getCurrentPatternIndex();
    bool aButtonIsPressed = false;
    for (int i = 0; i < NUMBER_OF_STEPBUTTONS; i++) {
        if (stepButtons[i].read()) {
            aButtonIsPressed = true;
            if (sourcePatternIndex == -1) {
                // this is the first button that is pressed down (after no steps
                // were pressed). Register this step as source for (a possible,
                // to follow) copy operation.
                sourcePatternIndex = i;
            } else if (i != sourcePatternIndex) {
                // this is not the first button that is pressed down, so this is
                // a target step for copy (from source step)
                for (auto &track : tracks) {
                    if (!SequencerTrack::anyPatternOpsArmed() || track.isPatternOpsArmed()) {
                        track.patterns[i].copyValuesFrom(track.patterns[sourcePatternIndex]);
                        stepLED(i) = CRGB::Red;
                    }
                }
                patternCopy = true;
            }
        }
        if (stepButtons[i].fell() && !patternCopy) {
            nextPatternIndex = i;
        }
        stepLED(i) = i == currentPatternIndex ? CRGB::Red : CRGB::Black;
    }
    if (!aButtonIsPressed) {
        // reset values needed for the copy operation as soon as no step buttons
        // are pressed at all
        sourcePatternIndex = -1;
        patternCopy = false;
    }
    if (nextPatternIndex >= 0) {
        stepLED(nextPatternIndex) = CRGB::Red;
        stepLED(nextPatternIndex).nscale8(255 - ledFader);
    }
}

/*
 * Leave Pattern mode (activate queued change)
 */
void Sequencer::doLeavePatternOps() {
    for (auto &track : tracks) {
        if (nextPatternIndex >= 0) {
            if (!SequencerTrack::anyPatternOpsArmed()) {
                // the general, non-track specific pattern change, will also unmute all tracks
                track.unMute();
                track.switchToPattern(nextPatternIndex);
            } else if (track.isPatternOpsArmed()) {
                track.switchToPattern(nextPatternIndex);
            }
        }
    }
    SequencerTrack::deactivateAllPatternOpsArms();
    nextPatternIndex = -1;
}

void Sequencer::doSetTrackSelection() {
    for (int i = 0; i < NUMBER_OF_INSTRUMENTTRACKS; i++) {
        // while trackbuttons are pressed, input1 changes the volume of the track, input2 the panorama
        if (trackButtons[i].read()) {
            if (input1.isActive()) {
                audioChannels[i]->setVolume(input1.getValue());
                mixerL->gain(i, audioChannels[i]->getOutput1Gain());
                mixerR->gain(i, audioChannels[i]->getOutput2Gain());
            }
            if (input2.isActive()) {
                audioChannels[i]->setPan(input2.getValue());
                mixerL->gain(i, audioChannels[i]->getOutput1Gain());
                mixerR->gain(i, audioChannels[i]->getOutput2Gain());
            }
        }
        // on trackbutton release, change selected track
        if (trackButtons[i].fell()) {
            deactivateSensors();
            selectedTrack = i;
        }
        setDefaultTrackLight(i);
    }
}

void Sequencer::doUpdateMutes() {
    for (int i = 0; i < NUMBER_OF_INSTRUMENTTRACKS; i++) {
        tracks[i].activateMuteArms();
    }
}

void Sequencer::doTurnOffPlockMode() {
    for (int i = 0; i < NUMBER_OF_INSTRUMENTTRACKS; i++) {
        tracks[i].getCurrentPattern().turnOffPLockMode();
    }
    deactivateSensors();
}

void Sequencer::setDefaultTrackLight(uint8_t trackNum) {
    if (tracks[trackNum].getCurrentPattern().isInPLockMode()) {
        hasActivePLockReceivers = true;
        // if track is recording plocks
        trackLED(trackNum) = (trackNum == selectedTrack) ? CRGB::DarkOrange : CRGB::Yellow;
    } else {
        trackLED(trackNum) = (trackNum == selectedTrack) ? CRGB::Green : CRGB::CornflowerBlue;
    }
    if (tracks[trackNum].isMuted()) {
        trackLED(trackNum).nscale8(MUTE_DIM_FACTOR);
    }
}

void Sequencer::setFunctionButtonLights() {
    functionLED(BUTTON_STARTSTOP) = running ? CRGB::Green : CRGB::Black;
    if ((hasActivePLockReceivers && (stepCount % 2) == 0) || (hasActivePLockReceivers && (input1.isActive() || input2.isActive()))) {
        functionLED(BUTTON_TOGGLE_PLOCK) = CRGB::DarkOrange;
    }
    functionLED(BUTTON_SET_PARAMSET_1) = pLockParamSet == PLockParamSet::SET1 ? CRGB::Green : CRGB::CornflowerBlue;
    functionLED(BUTTON_SET_PARAMSET_2) = pLockParamSet == PLockParamSet::SET2 ? CRGB::Green : CRGB::CornflowerBlue;
    functionLED(BUTTON_SET_PARAMSET_3) = pLockParamSet == PLockParamSet::SET3 ? CRGB::Green : CRGB::CornflowerBlue;
}

void Sequencer::onMidiInput(uint8_t rtb) {
    switch (rtb) {
        case 0xF8:  // Clock
            midiClockReceived = true;
            break;
        case 0xFA:  // Start
            isSyncingToMidiClock = true;
            start();
            break;
        case 0xFC:  // Stop
            isSyncingToMidiClock = false;
            stop();
            break;
        //case 0xFB:  // Continue
        //case 0xFE:  // ActiveSensing
        //case 0xFF:  // SystemReset
        //    break;
        default:  // Invalid Real Time marker
            break;
    }
}

/*
* Midiclock sends 24 pulses per quarter -> 6 pulses for a 16th 
*/
bool Sequencer::shouldStepMidiClock() {
    if (pulseCount++ >= 5) {
        pulseCount = 0;
        return true;
    } else {
        return false;
    }
}

bool Sequencer::shouldStepInternalClock() {
    if (millis() >= nextStepTime) {
        lastStepTime = nextStepTime;
        nextStepTime += stepLength;
        return true;
    } else {
        return false;
    }
}

/*
* Checks if the conditions are met to advance one step, 
* considering internal clock / midiclock / trigger input
*/
bool Sequencer::shouldStep(){
    if (isSyncingToMidiClock && midiClockReceived) {
        midiClockReceived = false;
        return shouldStepMidiClock();
    } else {
        return !isSyncingToMidiClock && shouldStepInternalClock();
    }
}

CRGB Sequencer::colorForStepState(uint8_t state) {
    switch (state) {
        case 1:
            // trigger on / plock rec off
            return CRGB::CornflowerBlue;
        case 2:
            // trigger off / plock rec on
            return CRGB::Green;
        case 3:
            // trigger on / plock rec on
            return CRGB::DarkOrange;
        default:
            // trigger off / plock rec off
            return CRGB::Black;
    }
}