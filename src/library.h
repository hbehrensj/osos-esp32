#pragma once
#include <Arduino.h>

// Online ZX81 program library, backed by the archive.org Sinclair_ZX81_TOSEC archive.
// A small prebuilt catalog (data/zx81-catalog.tsv, "<cat>\t<TITLE>\t<encoded-path>" per
// line) is downloaded once to LittleFS; searches are served from it. Downloading the
// actual game (fetch the per-title .zip from archive.org, inflate the .P) is a later step.
//
// Categories: 0=GAMES 1=APPS 2=DEMOS 3=EDUC 4=GAMES-MP 5=APPS-MP.

void   libraryBegin();
bool   libraryReady();                  // catalog present in LittleFS?
String libraryEnsureCatalog();          // download the catalog if missing; status line
String libraryStatus();

// Search the catalog: cat (-1 = all) + case-insensitive substring of the title. Appends up
// to `count` matches from `offset` as "<lineIndex>\t<TITLE>\n"; returns total match count.
int    librarySearch(int cat, const String& query, int offset, int count, String& out);

// Download catalog entry `index` from archive.org, inflate its .P, write it to the program
// slot, and set the 8.3 filename. Returns a status line. (Press S on the ZX81 to pull it.)
String libraryDownload(int index);
