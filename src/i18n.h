// GameBasiX - Meldungen auf Deutsch und Englisch
//
// Die Meldungen im Quelltext stehen auf Deutsch. Beim Ausgeben ersetzt tr()
// bekannte Bausteine durch ihre englische Fassung, damit auch
// zusammengesetzte Texte („Unbekannte Methode: FOO (KISTE)“) übersetzt
// werden. Gewählt wird die Sprache über --lang oder GBX_LANG.
#pragma once

#include <string>

namespace i18n {

// "en" (auch "en-US", "english") schaltet auf Englisch, alles andere Deutsch.
void setLanguage(const std::string& code);
bool english();

// Übersetzt deutsche Bausteine. Unbekannter Text bleibt unverändert.
std::string tr(const std::string& text);

}  // namespace i18n
