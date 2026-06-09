#pragma once
#include <juce_core/juce_core.h>
#include "FileUtils.h"

namespace ns::RecentFiles
{
    inline juce::File listFile (const juce::String& key)
    {
        return ns::FileUtils::userDataDir().getChildFile ("Recent_" + key + ".txt");
    }

    /** Loads recents (newest first). Stale (missing) entries are dropped. */
    inline juce::Array<juce::File> load (const juce::String& key, int maxEntries = 10)
    {
        juce::Array<juce::File> out;
        auto f = listFile (key);
        if (! f.existsAsFile()) return out;

        juce::StringArray lines;
        f.readLines (lines);
        for (auto& l : lines)
        {
            l = l.trim();
            if (l.isEmpty()) continue;
            juce::File jf (l);
            if (jf.existsAsFile() && ! out.contains (jf))
                out.add (jf);
            if (out.size() >= maxEntries) break;
        }
        return out;
    }

    /** Promote `file` to the head of the list and persist (max 10). */
    inline void add (const juce::String& key, const juce::File& file, int maxEntries = 10)
    {
        if (file == juce::File{}) return;
        auto list = load (key, maxEntries * 2);
        list.removeAllInstancesOf (file);
        list.insert (0, file);
        while (list.size() > maxEntries) list.removeLast();

        juce::StringArray out;
        for (auto& f : list) out.add (f.getFullPathName());
        listFile (key).replaceWithText (out.joinIntoString ("\n"));
    }

    inline void clear (const juce::String& key)
    {
        listFile (key).deleteFile();
    }
}
