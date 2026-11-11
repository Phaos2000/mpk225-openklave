// Stock-MPK225 host-side MIDI proof, not firmware. No SysEx/update/USB writes.
// Windows uses built-in WinMM. This program NEVER opens an MPK225 MIDI output.
#include "ChordEngine.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#endif

using mpk225::ChordEngine;
using mpk225::ChordType;
using mpk225::MidiBatch;

static uint32_t pack(uint8_t status, uint8_t key, uint8_t velocity) {
    return static_cast<uint32_t>(status) |
           (static_cast<uint32_t>(key) << 8) |
           (static_cast<uint32_t>(velocity) << 16);
}

struct ChordRouter {
    ChordEngine engine;
    uint8_t inputChannel = 0; // 0=channel 1, default factory keybed MIDI ch
    uint32_t emitted = 0;

    template <typename Send>
    void consume(uint32_t raw, Send send) {
        const uint8_t status = static_cast<uint8_t>(raw & 0xFF);
        const uint8_t key = static_cast<uint8_t>((raw >> 8) & 0x7F);
        const uint8_t velocity = static_cast<uint8_t>((raw >> 16) & 0x7F);
        const uint8_t family = static_cast<uint8_t>(status & 0xF0);
        const uint8_t channel = static_cast<uint8_t>(status & 0x0F);
        if (status < 0x80 || status >= 0xF0) return; // never forward SysEx/system data
        if (channel == inputChannel && (family == 0x90 || family == 0x80)) {
            const MidiBatch batch = (family == 0x90 && velocity != 0)
                ? engine.noteOn(key, velocity) : engine.noteOff(key);
            for (uint8_t i = 0; i < batch.count; ++i) {
                // Retain configured MIDI channel rather than channel 1 in the core.
                send(pack(static_cast<uint8_t>(batch.events[i].status | inputChannel),
                          batch.events[i].note, batch.events[i].velocity));
                ++emitted;
            }
        } else {
            send(raw & 0x00FFFFFFu);  // pass through pads on another channel, CC, wheel
            ++emitted;
        }
    }

    template <typename Send>
    void stop(Send send) {
        engine.allNotesOff([&](mpk225::MidiEvent e) {
            send(pack(static_cast<uint8_t>(e.status | inputChannel), e.note, 0));
            ++emitted;
        });
    }
};

[[maybe_unused]] static ChordType parseChord(const char* name) {
    const std::string wanted(name);
    for (unsigned n = 0; n < static_cast<unsigned>(ChordType::Count); ++n)
        if (wanted == mpk225::kChords[n].name) return static_cast<ChordType>(n);
    std::fprintf(stderr, "Unknown chord '%s'. Available:\n", name);
    for (unsigned n = 0; n < static_cast<unsigned>(ChordType::Count); ++n)
        std::fprintf(stderr, "%s%s", mpk225::kChords[n].name, n % 12 == 11 ? "\n" : " ");
    std::fprintf(stderr, "\n");
    std::exit(2);
}

static int selfTest() {
    ChordRouter router;
    router.engine.setChordType(ChordType::Major7);
    std::vector<uint32_t> messages;
    auto send = [&](uint32_t data) { messages.push_back(data); };
    router.consume(pack(0x90,60,90),send); // C E G B
    if (messages.size()!=4) return 1;
    if (((messages[3]>>8)&0x7F)!=71) return 2;
    router.consume(pack(0x90,64,90),send); // E G# B D#, B/E shared
    if (messages.size()!=6) return 3; // 2 new, 2 shared (E and B)
    router.consume(pack(0x80,60,0),send); // C and G release; E/B shared
    if (messages.size()!=8) return 4;
    router.consume(pack(0x90,64,0),send); // note-on velocity 0 = off
    if (messages.size()!=12) return 5;
    router.consume(pack(0xB0,1,72),send); // modwheel passthrough
    if (messages.back()!=pack(0xB0,1,72)) return 6;
    router.consume(pack(0xF0,0,0),send); // outgoing system/SysEx forbidden
    if (messages.size()!=13) return 7;
    router.consume(pack(0x90,60,70),send);
    router.stop(send);                    // all released safely
    if (messages.size()!=21) return 8;   // 4 more on + 4 off
    for (unsigned n=0;n<128;++n)
        if (router.engine.noteReferences(static_cast<uint8_t>(n))) return 9;
    std::printf("PASS: live host MIDI routing, 49-chord engine, overlapping notes, all-offs\n");
    return 0;
}

#ifdef _WIN32
static DWORD mainThread = 0;
static BOOL WINAPI ctrlHandler(DWORD event) {
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT ||
        event == CTRL_CLOSE_EVENT) {
        PostThreadMessage(mainThread, WM_QUIT, 0, 0);
        return TRUE;
    }
    return FALSE;
}
static void listPorts() {
    std::puts("MIDI input devices:");
    for (UINT i=0;i<midiInGetNumDevs();++i) {
        MIDIINCAPSA c{};
        if (midiInGetDevCapsA(i,&c,sizeof(c)) == MMSYSERR_NOERROR)
            std::printf("  Input %u: %s\n",i,c.szPname);
    }
    std::puts("MIDI output devices:");
    for (UINT i=0;i<midiOutGetNumDevs();++i) {
        MIDIOUTCAPSA c{};
        if (midiOutGetDevCapsA(i,&c,sizeof(c)) == MMSYSERR_NOERROR)
            std::printf("  Output %u: %s\n",i,c.szPname);
    }
}

static std::string lower(std::string s) {
    std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return std::tolower(c);});
    return s;
}
static bool numberArg(const char* arg, unsigned& result) {
    char* end=nullptr;
    const unsigned long value=std::strtoul(arg,&end,10);
    if (!*arg || !end || *end || value>65535) return false;
    result=static_cast<unsigned>(value);
    return true;
}
int main(int argc, char** argv) {
    unsigned in=0, out=0, channel=1;
    ChordType chord=ChordType::Major7;
    bool inputChosen=false, outputChosen=false, listed=false;
    for (int a=1;a<argc;++a) {
        const std::string opt(argv[a]);
        if (opt=="--selftest") return selfTest();
        if (opt=="--list") { listed=true; continue; }
        if (opt=="--help") {
            std::puts("Usage: MPK225-Live-Chords.exe --list");
            std::puts("       MPK225-Live-Chords.exe --input N --output M --chord maj7 [--channel 1]");
            std::puts("Input = MPK225 MIDI; Output = separate virtual MIDI or synth port (NOT MPK225).");
            std::puts("Chord names: maj m7 maj7 7 m9 13 7#9 etc. Run --chord invalid to list all.");
            std::puts("Press Ctrl+C to stop and release generated notes. No SysEx, no flashing.");
            return 0;
        }
        if (a+1>=argc) { std::fprintf(stderr,"Missing value for %s\n",argv[a]); return 2; }
        const char* value=argv[++a];
        if (opt=="--input") { if (!numberArg(value,in)) return 2; inputChosen=true; }
        else if (opt=="--output") { if(!numberArg(value,out))return 2; outputChosen=true; }
        else if (opt=="--channel") { if(!numberArg(value,channel)||channel<1||channel>16)return 2; }
        else if (opt=="--chord") chord=parseChord(value);
        else { std::fprintf(stderr,"Unknown option: %s\n",opt.c_str()); return 2; }
    }
    if (listed) { listPorts(); return 0; }
    if (!inputChosen || !outputChosen) { std::puts("Choose ports with --list and --input N --output M"); return 2; }
    if (in>=midiInGetNumDevs() || out>=midiOutGetNumDevs()) {
        std::puts("Invalid port number. Run --list."); return 2;
    }
    MIDIINCAPSA cin{}; MIDIOUTCAPSA cout{};
    if (midiInGetDevCapsA(in,&cin,sizeof(cin)) != MMSYSERR_NOERROR ||
        midiOutGetDevCapsA(out,&cout,sizeof(cout)) != MMSYSERR_NOERROR) return 2;
    if (lower(cout.szPname).find("mpk225") != std::string::npos) {
        std::puts("REFUSED: output port belongs to MPK225. Select a virtual MIDI or synth destination.");
        return 2;
    }
    if (lower(cin.szPname).find("mpk225") == std::string::npos)
        std::printf("Warning: input '%s' does not mention MPK225.\n",cin.szPname);
    HMIDIOUT mout=nullptr;
    MMRESULT result=midiOutOpen(&mout,out,0,0,CALLBACK_NULL);
    if (result!=MMSYSERR_NOERROR) { std::printf("Cannot open output (%u)\n",result); return 2; }
    mainThread=GetCurrentThreadId();
    MSG msg{}; PeekMessage(&msg,nullptr,WM_USER,WM_USER,PM_NOREMOVE);
    HMIDIIN min=nullptr;
    result=midiInOpen(&min,in,mainThread,0,CALLBACK_THREAD);
    if (result!=MMSYSERR_NOERROR) {
        std::printf("Cannot open MPK225 input (%u)\n",result);
        midiOutClose(mout); return 2;
    }
    ChordRouter router;
    router.inputChannel=static_cast<uint8_t>(channel-1);
    router.engine.setChordType(chord);
    if (midiInStart(min)!=MMSYSERR_NOERROR) {
        midiInClose(min); midiOutClose(mout); std::puts("Could not start MIDI input"); return 2;
    }
    SetConsoleCtrlHandler(ctrlHandler,TRUE);
    std::printf("ACTIVE: %s -> %s; chord=%s; key channel=%u.\n",
                cin.szPname,cout.szPname,router.engine.chordName(),channel);
    std::puts("Read MIDI from keyboard; send channel voice to selected output. Press Ctrl+C to stop.");
    auto send=[&](uint32_t data) {
        MMRESULT e=midiOutShortMsg(mout,data);
        if (e!=MMSYSERR_NOERROR) std::printf("MIDI OUT error: %u\n",e);
    };
    while (GetMessage(&msg,nullptr,0,0)>0) {
        if (msg.message == MM_MIM_DATA) router.consume(static_cast<uint32_t>(msg.lParam),send);
    }
    midiInStop(min); midiInReset(min); midiInClose(min);
    router.stop(send);
    midiOutClose(mout);
    SetConsoleCtrlHandler(ctrlHandler,FALSE);
    std::printf("Stopped; sent %u MIDI messages, all generated notes released.\n",router.emitted);
    return 0;
}
#else
int main(int argc, char** argv) {
    if (argc==2 && std::string(argv[1])=="--selftest") return selfTest();
    std::puts("Windows WinMM app. Run this source's --selftest on non-Windows.");
    return 2;
}
#endif
