#pragma once
#include <Arduino.h>

// zxsvr server (charlierobson serial-server protocol) on the OpenSpand UART.
// The ZX81 (OSOS) is the client and drives every exchange; we just answer verbs:
//   'I'            -> reply 2 bytes: armed file length (lo,hi)
//   'T',num,len    -> reply len bytes from offset num*256 (len 0 = 256) + 2-byte checksum
//   'X'            -> end of transfer; revert to the program slot
//   'U',verLo,verHi-> OSOS auto-update query; reply 1 byte (0=up-to-date,
//                     1=newer menu.p available -> arm the update slot for the next 'I')
//   'N'            -> reply 1 len byte + len ZX81-code bytes: the program slot's original
//                     filename. zxsvr.exe ignores 'N' (the ZX81 times out -> INBOX.P), so
//                     this stays compatible while giving real filenames over the bridge.
//
// Two served-file slots in LittleFS: the program slot (PROGRAM_FS_PATH, last web
// upload) is the default; the update slot (MENU_FS_PATH, mirrored from GitHub) is
// armed by 'U' and reverts after the next 'X'.

void serialServerBegin();
void serialServerLoop();
void serialSetProgramName(const String& fn);   // remember a web-uploaded .p's filename
String serialProgramName();                     // current program-slot filename
