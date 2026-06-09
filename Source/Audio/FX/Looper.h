#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>

namespace nl
{
    /** Master-bus audio looper.
     *
     *  Free-tempo, one-shot capture + overdub. Single REC pedal cycles:
     *      Idle  -> Recording     (clears buffer, captures from now)
     *      Rec   -> Playing       (locks length, restarts from 0)
     *      Play  -> Overdub       (sums live input into existing layers)
     *      Over  -> Playing       (commits overdub, keeps playing)
     *
     *  STOP halts playback (loop is preserved). PLAY restarts from 0.
     *  CLEAR wipes back to Idle.
     *
     *  Commands are posted from the message thread and consumed lock-free
     *  at the top of process(). Buffer storage is pre-allocated in
     *  prepare() so no audio-thread allocations occur.
     */
    class Looper
    {
    public:
        /** CountIn is a new optional state that precedes Recording when
         *  count-in is enabled. During CountIn, metronome clicks are mixed
         *  into the bus (regardless of the metronomeOn flag) so the player
         *  hears 4 beats before capture begins. */
        enum class State { Idle, CountIn, Recording, Playing, Overdub, Stopped };

        void prepare (double sampleRate, int /*blockSize*/, int numChannels = 2,
                      double maxSeconds = 60.0)
        {
            sr        = juce::jmax (1.0, sampleRate);
            numCh     = juce::jlimit (1, 2, numChannels);
            maxSamples= juce::jmax (1024, (int) std::ceil (maxSeconds * sr));
            buffer.setSize (numCh, maxSamples, false, true, true);
            buffer.clear();
            len = 0; pos = 0;
            countInSamplesLeft = 0;
            beatPhase = 0;
            clickPhase = 0;
            clickRemaining = 0;
            state.store ((int) State::Idle);
            cmd  .store ((int) Cmd::None);
        }

        void process (juce::AudioBuffer<float>& bus) noexcept
        {
            if (maxSamples <= 0) return;
            const int n  = bus.getNumSamples();
            const int ch = juce::jmin (numCh, bus.getNumChannels());

            // Drain pending command (single-consumer, message-thread producer).
            const int rawCmd = cmd.exchange ((int) Cmd::None);
            if (rawCmd != (int) Cmd::None)
                applyCommand ((Cmd) rawCmd);

            const State s = (State) state.load();
            const bool wantMet = metronomeOn.load();
            const double bpmNow = juce::jlimit (30.0, 300.0, bpm.load());
            const int samplesPerBeat = juce::jmax (1, (int) std::round (sr * 60.0 / bpmNow));
            const int beatsPerBar = juce::jmax (1, beats.load());

            // CountIn: count down samples while emitting metronome clicks.
            // When the counter expires, transition to Recording.
            if (s == State::CountIn)
            {
                for (int i = 0; i < n; ++i)
                {
                    advanceMetronome (bus, i, ch, samplesPerBeat, beatsPerBar);
                    if (--countInSamplesLeft <= 0)
                    {
                        // Transition to Recording at this exact sample.
                        len = 0; pos = 0; buffer.clear();
                        state.store ((int) State::Recording);
                        // Reset beat phase so recording starts on a downbeat.
                        beatPhase = 0;
                        // Capture the rest of this block as the first samples.
                        const int rem = n - i - 1;
                        if (rem > 0)
                            recordRange (bus, i + 1, rem, ch);
                        return;
                    }
                }
                return;
            }

            if (s == State::Idle || s == State::Stopped)
            {
                // Optional free-running metronome even when looper is idle/stopped.
                if (wantMet)
                    for (int i = 0; i < n; ++i)
                        advanceMetronome (bus, i, ch, samplesPerBeat, beatsPerBar);
                return;
            }

            const float playGain = mix.load();

            for (int i = 0; i < n; ++i)
            {
                // Snapshot the DRY input sample BEFORE the metronome adds
                // any click into the bus. We record / overdub from this
                // snapshot so the metronome is only ever heard, never
                // captured into the loop buffer.
                float dryIn[2] = { 0.0f, 0.0f };
                for (int c = 0; c < ch; ++c)
                    dryIn[c] = bus.getSample (c, i);

                if (s == State::Recording)
                {
                    if (len < maxSamples)
                    {
                        for (int c = 0; c < ch; ++c)
                            buffer.setSample (c, len, dryIn[c]);
                        ++len;
                    }
                    else
                    {
                        // Buffer full -- auto-commit to playing on next sample.
                        commitToPlaying();
                        // fall through; produce playback from this sample.
                    }
                }

                const State sNow = (State) state.load();
                if (sNow != State::Recording && len > 0)
                {
                    if (pos >= len) pos = 0;

                    for (int c = 0; c < ch; ++c)
                    {
                        const float loop = buffer.getSample (c, pos);

                        if (sNow == State::Overdub)
                        {
                            // Sum DRY live into the layer (no metronome).
                            const float layered = juce::jlimit (-2.0f, 2.0f, loop + dryIn[c]);
                            buffer.setSample (c, pos, layered);
                            bus  .setSample (c, i, dryIn[c] + layered * playGain);
                        }
                        else
                        {
                            bus.setSample (c, i, dryIn[c] + loop * playGain);
                        }
                    }
                    ++pos;
                    if (pos >= len) pos = 0;
                }

                // Metronome added to the OUTPUT bus after record / playback
                // so the click is audible to the player but never written
                // into the loop buffer.
                if (wantMet)
                    advanceMetronome (bus, i, ch, samplesPerBeat, beatsPerBar);
            }
        }

        // --- Message-thread API -------------------------------------------
        void tapRecord() noexcept { cmd.store ((int) Cmd::Rec);   }
        void tapStop  () noexcept { cmd.store ((int) Cmd::Stop);  }
        void tapPlay  () noexcept { cmd.store ((int) Cmd::Play);  }
        void tapClear () noexcept { cmd.store ((int) Cmd::Clear); }

        void  setMix (float m) noexcept { mix.store (juce::jlimit (0.0f, 1.5f, m)); }
        float getMix() const noexcept   { return mix.load(); }

        // Count-in / metronome configuration.
        void setBpm           (double v) noexcept { bpm.store (juce::jlimit (30.0, 300.0, v)); }
        double getBpm         () const noexcept   { return bpm.load(); }
        void setBeatsPerBar   (int b)    noexcept { beats.store (juce::jlimit (1, 16, b)); }
        int  getBeatsPerBar   () const noexcept   { return beats.load(); }
        void setCountInEnabled(bool b)   noexcept { countInEnabled.store (b); }
        bool isCountInEnabled () const noexcept   { return countInEnabled.load(); }
        void setMetronomeOn   (bool b)   noexcept { metronomeOn.store (b); }
        bool isMetronomeOn    () const noexcept   { return metronomeOn.load(); }
        void setMetronomeLevel(float v)  noexcept { metLevel.store (juce::jlimit (0.0f, 1.0f, v)); }
        float getMetronomeLevel() const noexcept  { return metLevel.load(); }

        State getState()  const noexcept { return (State) state.load(); }
        int   getLength() const noexcept { return len; }
        int   getPosition() const noexcept { return pos; }

        double getLengthSeconds() const noexcept
        {
            return sr > 0.0 ? (double) len / sr : 0.0;
        }
        float getProgress() const noexcept
        {
            if (getState() == State::CountIn)
            {
                const int total = juce::jmax (1, countInTotalSamples);
                const int done  = total - countInSamplesLeft;
                return juce::jlimit (0.0f, 1.0f, (float) done / (float) total);
            }
            return len > 0 ? (float) pos / (float) len : 0.0f;
        }

        /** Beats remaining during count-in (4, 3, 2, 1...). 0 when not counting in. */
        int getCountInBeatsRemaining() const noexcept
        {
            if (getState() != State::CountIn) return 0;
            const double bpmNow = juce::jlimit (30.0, 300.0, bpm.load());
            const int samplesPerBeat = juce::jmax (1, (int) std::round (sr * 60.0 / bpmNow));
            return (countInSamplesLeft + samplesPerBeat - 1) / samplesPerBeat;
        }

    private:
        enum class Cmd { None = 0, Rec, Stop, Play, Clear };

        void applyCommand (Cmd c) noexcept
        {
            const State s = (State) state.load();
            switch (c)
            {
                case Cmd::Rec:
                    if (s == State::Idle)
                    {
                        if (countInEnabled.load())
                        {
                            // Arm count-in: N beats of metronome clicks before
                            // actual recording starts. Buffer is cleared on
                            // the transition (see process()).
                            const double bpmNow = juce::jlimit (30.0, 300.0, bpm.load());
                            const int samplesPerBeat = juce::jmax (1, (int) std::round (sr * 60.0 / bpmNow));
                            const int b = juce::jmax (1, beats.load());
                            countInTotalSamples = samplesPerBeat * b;
                            countInSamplesLeft  = countInTotalSamples;
                            beatPhase = 0;
                            clickRemaining = 0;
                            state.store ((int) State::CountIn);
                        }
                        else
                        {
                            len = 0; pos = 0; buffer.clear();
                            beatPhase = 0;
                            state.store ((int) State::Recording);
                        }
                    }
                    else if (s == State::CountIn)   { /* ignore extra taps during count-in */ }
                    else if (s == State::Recording) commitToPlaying();
                    else if (s == State::Playing)   state.store ((int) State::Overdub);
                    else if (s == State::Overdub)   state.store ((int) State::Playing);
                    else if (s == State::Stopped)   { pos = 0; state.store ((int) State::Playing); }
                    break;

                case Cmd::Stop:
                    if (s == State::Recording) commitToPlaying();
                    if (s == State::CountIn)   { countInSamplesLeft = 0; state.store ((int) State::Idle); break; }
                    state.store ((int) State::Stopped);
                    break;

                case Cmd::Play:
                    if (s == State::Stopped || s == State::Playing || s == State::Overdub)
                    {
                        pos = 0;
                        state.store ((int) State::Playing);
                    }
                    break;

                case Cmd::Clear:
                    len = 0; pos = 0; buffer.clear();
                    countInSamplesLeft = 0;
                    state.store ((int) State::Idle);
                    break;

                default: break;
            }
        }

        void commitToPlaying() noexcept
        {
            pos = 0;
            state.store ((int) State::Playing);
        }

        void recordRange (juce::AudioBuffer<float>& bus, int startIdx, int count, int ch) noexcept
        {
            for (int j = 0; j < count && len < maxSamples; ++j)
            {
                for (int c = 0; c < ch; ++c)
                    buffer.setSample (c, len, bus.getSample (c, startIdx + j));
                ++len;
            }
        }

        /** Advance metronome by one sample, mixing a click envelope into all
         *  bus channels when one is active. Beat 1 of a bar uses a higher
         *  pitch (downbeat). */
        void advanceMetronome (juce::AudioBuffer<float>& bus, int i, int ch,
                               int samplesPerBeat, int beatsPerBar) noexcept
        {
            // Spawn a click on each beat boundary.
            if (beatPhase == 0)
            {
                clickRemaining = juce::jmin (samplesPerBeat, (int) (sr * 0.040));
                // Downbeat: 1500 Hz; other beats: 900 Hz.
                const double freq = (currentBeatInBar == 0) ? 1500.0 : 900.0;
                clickIncrement = juce::MathConstants<double>::twoPi * freq / sr;
                clickPhase = 0.0;
            }

            if (clickRemaining > 0)
            {
                const float env = (float) clickRemaining
                                 / (float) juce::jmax (1, (int) (sr * 0.040));
                const float lvl = metLevel.load();
                const float s   = std::sin ((float) clickPhase) * env * env * lvl;
                for (int c = 0; c < ch; ++c)
                    bus.setSample (c, i, bus.getSample (c, i) + s);
                clickPhase += clickIncrement;
                --clickRemaining;
            }

            if (++beatPhase >= samplesPerBeat)
            {
                beatPhase = 0;
                if (++currentBeatInBar >= beatsPerBar)
                    currentBeatInBar = 0;
            }
        }

        juce::AudioBuffer<float> buffer;
        std::atomic<int>   state { (int) State::Idle };
        std::atomic<int>   cmd   { (int) Cmd::None };
        std::atomic<float> mix   { 1.0f };

        // Metronome / count-in.
        std::atomic<double> bpm           { 120.0 };
        std::atomic<int>    beats         { 4 };
        std::atomic<bool>   countInEnabled{ true };
        std::atomic<bool>   metronomeOn   { false };
        std::atomic<float>  metLevel      { 0.5f };

        double sr        { 44100.0 };
        int    numCh     { 2 };
        int    maxSamples{ 0 };
        int    len       { 0 };
        int    pos       { 0 };

        // Count-in / metronome internal state (audio thread only).
        int    countInTotalSamples { 0 };
        int    countInSamplesLeft  { 0 };
        int    beatPhase           { 0 };
        int    currentBeatInBar    { 0 };
        int    clickRemaining      { 0 };
        double clickPhase          { 0.0 };
        double clickIncrement      { 0.0 };
    };
}
