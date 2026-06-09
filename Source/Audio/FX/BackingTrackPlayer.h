#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>

/** Simple backing-track player.
 *
 *  Loads WAV / AIFF / MP3 / FLAC (whatever juce_audio_formats supports) and
 *  mixes the decoded stereo audio into the master bus on the audio thread.
 *  Controls (play / pause / stop / loop / level) are safe to call from the
 *  message thread at any time.
 *
 *  Typical use:
 *      prepare() once when the device starts.
 *      process()  called every audio callback — mixes in-place into stereoOut.
 *      load()     called from UI thread; thread-safe via AudioTransportSource.
 */
class BackingTrackPlayer
{
public:
    BackingTrackPlayer()
    {
        formatManager.registerBasicFormats(); // WAV, AIFF, FLAC, OGG, MP3 (if available)
    }

    ~BackingTrackPlayer()
    {
        transport.setSource (nullptr);
    }

    // ---- called from audio thread ----------------------------------------

    void prepare (double sr, int blockSize)
    {
        sampleRate = sr;
        transport.prepareToPlay (blockSize, sr);
        mixBuffer.setSize (2, blockSize, false, false, true);
    }

    /** Mix the backing track into `bus` (stereo, any block size). */
    void process (juce::AudioBuffer<float>& bus) noexcept
    {
        if (! transport.isPlaying()) return;

        const int n  = bus.getNumSamples();
        const int ch = bus.getNumChannels();
        if (n <= 0 || ch < 1) return;

        // Resize scratch only if needed (defensive, should not normally trigger).
        if (mixBuffer.getNumSamples() < n)
            mixBuffer.setSize (2, n, false, false, true);

        mixBuffer.clear (0, 0, n);
        mixBuffer.clear (1, 0, n);

        juce::AudioSourceChannelInfo info (&mixBuffer, 0, n);
        transport.getNextAudioBlock (info);

        const float g = level.load();
        for (int c = 0; c < juce::jmin (ch, 2); ++c)
            bus.addFrom (c, 0, mixBuffer, c < mixBuffer.getNumChannels() ? c : 0, 0, n, g);
    }

    // ---- called from message thread ----------------------------------------

    /** Load a file. Returns true on success. Thread-safe. */
    bool load (const juce::File& file)
    {
        auto* reader = formatManager.createReaderFor (file);
        if (reader == nullptr) return false;

        auto newSrc = std::make_unique<juce::AudioFormatReaderSource> (reader, true);
        newSrc->setLooping (looping.load());

        transport.stop();
        transport.setSource (newSrc.get(), 0, nullptr, reader->sampleRate, 2);

        // Keep ownership — swap into member AFTER transport is set up.
        readerSource.reset (newSrc.release());

        currentFile = file;
        return true;
    }

    void play()    { if (readerSource) transport.start(); }
    void pause()   { transport.stop(); }
    void stop()    { transport.stop(); transport.setPosition (0.0); }

    void setLoop (bool shouldLoop)
    {
        looping.store (shouldLoop);
        if (readerSource) readerSource->setLooping (shouldLoop);
    }

    void setLevel (float v) noexcept { level.store (juce::jlimit (0.0f, 2.0f, v)); }
    float getLevel() const noexcept  { return level.load(); }

    bool  isLoaded()  const noexcept { return readerSource != nullptr; }
    bool  isPlaying() const noexcept { return transport.isPlaying(); }
    bool  isLooping() const noexcept { return looping.load(); }

    double getPositionSeconds() const noexcept { return transport.getCurrentPosition(); }
    double getLengthSeconds()   const noexcept { return transport.getLengthInSeconds(); }
    double getProgress()        const noexcept
    {
        const double len = getLengthSeconds();
        return len > 0.0 ? juce::jlimit (0.0, 1.0, getPositionSeconds() / len) : 0.0;
    }

    juce::String getFileName() const { return currentFile.getFileNameWithoutExtension(); }

private:
    juce::AudioFormatManager                      formatManager;
    juce::AudioTransportSource                    transport;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;

    juce::AudioBuffer<float> mixBuffer;
    juce::File               currentFile;

    double             sampleRate { 44100.0 };
    std::atomic<float> level      { 0.75f };
    std::atomic<bool>  looping    { false };
};
