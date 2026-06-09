#include "OfflineRenderer.h"
#include "../Audio/AudioEngine.h"

OfflineRenderer::OfflineRenderer (AudioEngine& e,
                                  const juce::File& in,
                                  const juce::File& outDir,
                                  const juce::String& base,
                                  int mask,
                                  int bits)
    : juce::ThreadWithProgressWindow ("Offline Render", true, true),
      engine (e),
      inputWav (in),
      outputDir (outDir),
      outputBaseName (base),
      captureMask (mask),
      bitsPerSample (bits)
{
    setStatusMessage ("Preparing...");
}

void OfflineRenderer::run()
{
    success = false;
    resultMessage.clear();

    if (! inputWav.existsAsFile())
    {
        resultMessage = "Input WAV does not exist: " + inputWav.getFullPathName();
        return;
    }
    if (! outputDir.isDirectory())
    {
        const auto r = outputDir.createDirectory();
        if (r.failed())
        {
            resultMessage = "Could not create output directory: " + r.getErrorMessage();
            return;
        }
    }

    // ---- Open input ----
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (inputWav));
    if (reader == nullptr)
    {
        resultMessage = "Could not read input file (unsupported format?).";
        return;
    }

    const double sampleRate     = reader->sampleRate;
    const int    totalSamples   = reader->lengthInSamples > (juce::int64) std::numeric_limits<int>::max()
                                    ? std::numeric_limits<int>::max()
                                    : static_cast<int> (reader->lengthInSamples);
    const int    blockSize      = 1024;

    setStatusMessage ("Stopping live audio device...");
    engine.stop();
    engine.prepareOffline (sampleRate, blockSize);

    // ---- Allocate stem buffers ----
    juce::AudioBuffer<float> diBuf, postNamBuf, postIrBuf, masterBuf;
    if (captureMask & StemDI)      diBuf     .setSize (1, totalSamples, false, true, true);
    if (captureMask & StemPostNAM) postNamBuf.setSize (1, totalSamples, false, true, true);
    if (captureMask & StemPostIR)  postIrBuf .setSize (2, totalSamples, false, true, true);
    if (captureMask & StemMaster)  masterBuf .setSize (2, totalSamples, false, true, true);

    AudioEngine::StemTaps taps;
    taps.di      = (captureMask & StemDI)      ? &diBuf      : nullptr;
    taps.postNam = (captureMask & StemPostNAM) ? &postNamBuf : nullptr;
    taps.postIr  = (captureMask & StemPostIR)  ? &postIrBuf  : nullptr;
    taps.master  = (captureMask & StemMaster)  ? &masterBuf  : nullptr;

    // ---- Pre-warm: send a few blocks of silence so IIR/IR/NAM tails settle ----
    setStatusMessage ("Pre-warming DSP...");
    juce::AudioBuffer<float> silence (1, blockSize); silence.clear();
    juce::AudioBuffer<float> warmOut (2, blockSize);
    for (int i = 0; i < 8; ++i)
        engine.processOfflineBlock (silence.getReadPointer (0),
                                    warmOut.getWritePointer (0),
                                    warmOut.getWritePointer (1),
                                    blockSize, nullptr);

    // ---- Render ----
    setStatusMessage ("Rendering...");
    juce::AudioBuffer<float> readBuf (juce::jmax (1, (int) reader->numChannels), blockSize);
    juce::AudioBuffer<float> monoIn  (1, blockSize);
    juce::AudioBuffer<float> outSink (2, blockSize);

    int pos = 0;
    while (pos < totalSamples)
    {
        if (threadShouldExit())
        {
            resultMessage = "Cancelled by user.";
            engine.start();
            return;
        }

        const int n = juce::jmin (blockSize, totalSamples - pos);

        readBuf.clear();
        reader->read (&readBuf, 0, n, pos, true, reader->numChannels > 1);

        // Sum to mono if needed.
        monoIn.clear();
        for (int ch = 0; ch < readBuf.getNumChannels(); ++ch)
            monoIn.addFrom (0, 0, readBuf, ch, 0, n,
                            readBuf.getNumChannels() > 1 ? 1.0f / (float) readBuf.getNumChannels() : 1.0f);

        taps.offset = pos;
        engine.processOfflineBlock (monoIn.getReadPointer (0),
                                    outSink.getWritePointer (0),
                                    outSink.getWritePointer (1),
                                    n, &taps);

        pos += n;
        setProgress ((double) pos / (double) totalSamples);
    }

    // ---- Write stems ----
    setStatusMessage ("Writing stems...");
    juce::StringArray written;
    auto tryWrite = [&] (int flag, const juce::String& suffix, juce::AudioBuffer<float>& b)
    {
        if ((captureMask & flag) == 0) return;
        auto dest = outputDir.getChildFile (outputBaseName + "_" + suffix + ".wav");
        if (writeStem (dest, b, sampleRate))
            written.add (dest.getFileName());
        else
            resultMessage += "  - Failed: " + dest.getFileName() + "\n";
    };
    tryWrite (StemDI,      "DI",      diBuf);
    tryWrite (StemPostNAM, "PostNAM", postNamBuf);
    tryWrite (StemPostIR,  "PostIR",  postIrBuf);
    tryWrite (StemMaster,  "Master",  masterBuf);

    // ---- Restart live device ----
    setStatusMessage ("Restarting live device...");
    engine.start();

    success = ! written.isEmpty();
    if (success)
        resultMessage = "Rendered " + juce::String (written.size())
                        + " stem(s) to:\n" + outputDir.getFullPathName()
                        + "\n\n" + written.joinIntoString ("\n")
                        + (resultMessage.isEmpty() ? juce::String() : ("\n\n" + resultMessage));
    else if (resultMessage.isEmpty())
        resultMessage = "No stems were written.";
}

bool OfflineRenderer::writeStem (const juce::File& dest,
                                 const juce::AudioBuffer<float>& src,
                                 double sampleRate)
{
    dest.deleteFile();
    auto stream = std::make_unique<juce::FileOutputStream> (dest);
    if (! stream->openedOk()) return false;

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (stream.get(), sampleRate,
                             (unsigned int) src.getNumChannels(),
                             bitsPerSample, {}, 0));
    if (writer == nullptr) return false;

    stream.release();   // writer owns it now
    return writer->writeFromAudioSampleBuffer (src, 0, src.getNumSamples());
}
