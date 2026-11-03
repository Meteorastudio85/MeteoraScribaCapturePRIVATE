#pragma once

#include <juce_core/juce_core.h>

// Lingua dell'interfaccia (italiano / inglese).
// I testi nel codice sono scritti in italiano dentro TR("..."): in inglese vengono tradotti con la tabella in Lang.cpp.
namespace ScribaLang
{
    enum class Language { Italian = 0, English = 1 };

    Language get();
    void set (Language l);              // salva la scelta nelle impostazioni dell'utente
    juce::String translate (const juce::String& italian);
}

inline juce::String TR (const char* italianUtf8)
{
    return ScribaLang::translate (juce::String (juce::CharPointer_UTF8 (italianUtf8)));
}
