#include "MIDILearn.h"

void MIDILearnRegistry::registerParameter (const juce::String& paramId,
                                           const juce::String& displayName,
                                           Setter setter)
{
    std::lock_guard<std::mutex> g (lock);
    params[paramId.toStdString()] = { std::move (setter), displayName };
    if (auto it = mappings.find (paramId.toStdString()); it != mappings.end())
        it->second.displayName = displayName;
}

void MIDILearnRegistry::beginLearn (const juce::String& paramId)
{
    std::lock_guard<std::mutex> g (lock);
    pendingLearnId = paramId;
}

void MIDILearnRegistry::cancelLearn()
{
    std::lock_guard<std::mutex> g (lock);
    pendingLearnId.clear();
}

bool MIDILearnRegistry::isLearning() const noexcept
{
    std::lock_guard<std::mutex> g (lock);
    return pendingLearnId.isNotEmpty();
}

juce::String MIDILearnRegistry::currentLearnTarget() const
{
    std::lock_guard<std::mutex> g (lock);
    return pendingLearnId;
}

void MIDILearnRegistry::clearMapping (const juce::String& paramId)
{
    {
        std::lock_guard<std::mutex> g (lock);
        mappings.erase (paramId.toStdString());
    }
    if (onChanged) juce::MessageManager::callAsync ([cb = onChanged] { cb(); });
}

void MIDILearnRegistry::setMapping (const juce::String& paramId, int channel,
                                    int ccOrNote, MidiMsgType type)
{
    juce::String displayName;
    {
        std::lock_guard<std::mutex> g (lock);
        auto it = params.find (paramId.toStdString());
        if (it == params.end()) return;
        displayName = it->second.displayName;
        MIDILearnMapping m;
        m.channel     = juce::jlimit (0, 16, channel);
        m.ccOrNote    = juce::jlimit (0, 127, ccOrNote);
        m.type        = type;
        m.paramId     = paramId;
        m.displayName = displayName;
        mappings[paramId.toStdString()] = m;
    }
    if (onChanged) juce::MessageManager::callAsync ([cb = onChanged] { cb(); });
}

juce::StringArray MIDILearnRegistry::getRegisteredParamIds() const
{
    std::lock_guard<std::mutex> g (lock);
    juce::StringArray ids;
    for (auto& [k, v] : params) ids.add (k);
    ids.sort (true);
    return ids;
}

juce::String MIDILearnRegistry::getDisplayName (const juce::String& paramId) const
{
    std::lock_guard<std::mutex> g (lock);
    auto it = params.find (paramId.toStdString());
    return (it != params.end() && it->second.displayName.isNotEmpty())
           ? it->second.displayName : paramId;
}

void MIDILearnRegistry::clearAll()
{
    {
        std::lock_guard<std::mutex> g (lock);
        mappings.clear();
    }
    if (onChanged) juce::MessageManager::callAsync ([cb = onChanged] { cb(); });
}

void MIDILearnRegistry::handleMidi (const juce::MidiMessage& msg)
{
    int         cn   = -1;
    MidiMsgType ty   = MidiMsgType::CC;
    float       v01  = 0.0f;

    if (msg.isController())
    {
        cn  = msg.getControllerNumber();
        ty  = MidiMsgType::CC;
        v01 = juce::jlimit (0, 127, msg.getControllerValue()) / 127.0f;
    }
    else if (msg.isNoteOn())
    {
        cn  = msg.getNoteNumber();
        ty  = MidiMsgType::Note;
        v01 = juce::jlimit (0, 127, (int) msg.getVelocity()) / 127.0f;
    }
    else if (msg.isProgramChange())
    {
        cn  = msg.getProgramChangeNumber();
        ty  = MidiMsgType::PC;
        // PC is a trigger-style event: when the program number matches the
        // mapping's ccOrNote, fire the setter at full value (=> any "v01 >= 0.5"
        // footswitch target like Mute / Scene-Recall / Tap-Tempo will trigger).
        // Continuous params bound to PC will simply jump to max -- PC is not
        // a continuous controller, so this is the most useful interpretation.
        v01 = 1.0f;
    }
    else
    {
        return;
    }

    const int channel = msg.getChannel();   // 1..16

    juce::String learnId;
    juce::String displayName;
    Setter        setter;
    bool          notify = false;

    {
        std::lock_guard<std::mutex> g (lock);

        if (pendingLearnId.isNotEmpty())
        {
            learnId = pendingLearnId;
            auto it = params.find (learnId.toStdString());
            if (it != params.end())
            {
                MIDILearnMapping m;
                m.channel      = channel;
                m.ccOrNote     = cn;
                m.type         = ty;
                m.paramId      = learnId;
                m.displayName  = it->second.displayName;
                mappings[learnId.toStdString()] = m;
                pendingLearnId.clear();
                notify = true;
            }
        }

        // Dispatch to whichever mapping matches.
        for (auto& [k, m] : mappings)
        {
            if (m.type != ty || m.ccOrNote != cn) continue;
            if (m.channel != 0 && m.channel != channel) continue;
            if (auto it = params.find (k); it != params.end())
            {
                setter      = it->second.setter;
                displayName = m.displayName;
            }
            break;
        }
    }

    if (setter)
        juce::MessageManager::callAsync ([setter, v01] { setter (v01); });

    if (notify && onChanged)
        juce::MessageManager::callAsync ([cb = onChanged] { cb(); });
}

juce::Array<MIDILearnMapping> MIDILearnRegistry::getMappings() const
{
    std::lock_guard<std::mutex> g (lock);
    juce::Array<MIDILearnMapping> out;
    for (auto& [k, m] : mappings) out.add (m);
    return out;
}

juce::ValueTree MIDILearnRegistry::toValueTree() const
{
    juce::ValueTree v ("MIDIAssignments");
    std::lock_guard<std::mutex> g (lock);
    for (auto& [k, m] : mappings)
    {
        juce::ValueTree e ("Mapping");
        e.setProperty ("paramId",     m.paramId,     nullptr);
        e.setProperty ("displayName", m.displayName, nullptr);
        e.setProperty ("channel",     m.channel,     nullptr);
        e.setProperty ("ccOrNote",    m.ccOrNote,    nullptr);
        e.setProperty ("type",        (int) m.type,  nullptr);
        e.setProperty ("isCC",        m.isCC(),      nullptr); // legacy compat
        v.appendChild (e, nullptr);
    }
    return v;
}

void MIDILearnRegistry::fromValueTree (const juce::ValueTree& v)
{
    std::lock_guard<std::mutex> g (lock);
    mappings.clear();
    if (! v.hasType ("MIDIAssignments")) return;
    for (auto e : v)
    {
        MIDILearnMapping m;
        m.paramId     = e.getProperty ("paramId",     juce::String()).toString();
        m.displayName = e.getProperty ("displayName", juce::String()).toString();
        m.channel     = (int)  e.getProperty ("channel",  0);
        m.ccOrNote    = (int)  e.getProperty ("ccOrNote", -1);
        if (e.hasProperty ("type"))
        {
            const int t = juce::jlimit (0, 2, (int) e.getProperty ("type", 0));
            m.type = (MidiMsgType) t;
        }
        else
        {
            // Legacy file: only "isCC" present (true => CC, false => Note).
            const bool legacyIsCC = (bool) e.getProperty ("isCC", true);
            m.type = legacyIsCC ? MidiMsgType::CC : MidiMsgType::Note;
        }
        if (m.paramId.isNotEmpty() && m.ccOrNote >= 0)
            mappings[m.paramId.toStdString()] = m;
    }
}
