#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>
#include <functional>
#include <unordered_map>
#include <mutex>

/** Type of MIDI message a mapping listens for. */
enum class MidiMsgType
{
    CC   = 0,  // Controller change       (value = controller value)
    Note = 1,  // Note On                  (value = velocity)
    PC   = 2   // Program Change           (value = trigger on match; "ccOrNote" = program #)
};

/** Mapping of an incoming MIDI message to a target parameter. */
struct MIDILearnMapping
{
    int          channel  { 0 };       // 0 = omni
    int          ccOrNote { -1 };      // CC#, note#, or program#
    MidiMsgType  type     { MidiMsgType::CC };
    juce::String paramId;
    juce::String displayName;

    // Legacy compatibility accessor (true for CC, false otherwise).
    bool isCC() const noexcept { return type == MidiMsgType::CC; }
};

/** Registry of MIDI -> parameter assignments.
 *
 *  Parameters are addressed by string ID. The owner of each parameter
 *  registers a setter callback (value in 0..1) on the registry; when a
 *  matching MIDI message arrives the setter is invoked on the message
 *  thread.
 */
class MIDILearnRegistry
{
public:
    using Setter = std::function<void (float /*0..1*/)>;

    /** Register a target setter. paramId is unique. */
    void registerParameter (const juce::String& paramId,
                            const juce::String& displayName,
                            Setter setter);

    /** Begin learning: the next CC/note received is bound to this paramId. */
    void beginLearn (const juce::String& paramId);
    void cancelLearn();
    bool isLearning() const noexcept;
    juce::String currentLearnTarget() const;

    /** Set/replace a mapping manually (no learn cycle). */
    void setMapping (const juce::String& paramId, int channel, int ccOrNote, MidiMsgType type);
    /** Legacy overload (kept for backwards source compatibility). */
    void setMapping (const juce::String& paramId, int channel, int ccOrNote, bool isCC)
    {
        setMapping (paramId, channel, ccOrNote, isCC ? MidiMsgType::CC : MidiMsgType::Note);
    }

    /** Removes any binding for paramId. */
    void clearMapping (const juce::String& paramId);
    void clearAll();

    /** Inject an incoming MIDI message (called by MIDIManager). */
    void handleMidi (const juce::MidiMessage& msg);

    juce::Array<MIDILearnMapping> getMappings() const;

    /** Returns all currently-registered parameter IDs (sorted). */
    juce::StringArray getRegisteredParamIds() const;
    /** Returns the human-readable name registered for paramId, or paramId itself. */
    juce::String       getDisplayName (const juce::String& paramId) const;

    juce::ValueTree toValueTree() const;
    void            fromValueTree (const juce::ValueTree& v);

    std::function<void()> onChanged;   // UI refresh

private:
    struct Entry { Setter setter; juce::String displayName; };

    mutable std::mutex                    lock;
    std::unordered_map<std::string, Entry>            params;     // paramId -> entry
    std::unordered_map<std::string, MIDILearnMapping> mappings;   // paramId -> mapping
    juce::String pendingLearnId;
};
