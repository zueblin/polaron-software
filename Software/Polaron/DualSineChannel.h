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

#include <Audio.h>
#include "AudioChannel.h"
#include "effect_shaped_envelope.h"

#ifndef DualSineChannel_h
#define DualSineChannel_h

class DualSineChannel : public AudioChannel {
   public:
    DualSineChannel(int lowFreq, int highFreq) : oscToMult1(osc1, 0, mult, 0), oscToMult2(osc2, 0, mult, 1), multToEnv(mult, envelope) {
        low = lowFreq;
        high = highFreq;
        envelope.attack(20);
        envelope.hold(0);
        envelope.decay(40);
        envelope.retriggers(0);
    }
    AudioStream *getOutput1() { return &envelope; }
    AudioStream *getOutput2() { return &envelope; }

    void trigger() { envelope.noteOn(); }
    void setParam1(int value) { osc1.frequency(map(value, 0, 1024, low, high)); }
    void setParam2(int value) { osc2.frequency(map(value, 0, 1024, low, high)); }
    void setParam3(int value) { envelope.attack(map(value, 0, 1024, 0, 10240)); }
    void setParam4(int value) { envelope.decay(map(value, 0, 1024, 0, 10240)); }
    void setParam5(int value) { envelope.retriggers(map(value, 0, 1024, 0, 12)); }
    void setParam6(int value) {}

   private:
    int low = 35;
    int high = 880;
    AudioSynthWaveformSine osc1;
    AudioSynthWaveformSine osc2;
    AudioEffectMultiply mult;
    AudioEffectShapedEnvelope envelope;
    AudioConnection oscToMult1;
    AudioConnection oscToMult2;
    AudioConnection multToEnv;
};
#endif
