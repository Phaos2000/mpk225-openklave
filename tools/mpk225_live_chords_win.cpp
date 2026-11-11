// Stock-MPK225 host-side MIDI proof, not firmware. No SysEx/update/USB writes.
// Windows uses built-in WinMM. This program NEVER opens an MPK225 MIDI output.
#include "ChordEngine.h"
#include "SmartHarmony.h"
#include "VoicingPlanner.h"
#include <algorithm>
#include <array>
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
using mpk225::SmartHarmony;
using mpk225::HarmonyContext;
using mpk225::HarmonySuggestion;
using mpk225::VoicedChord;
using mpk225::VoicingPlanner;
using mpk225::ScaleMode;

static uint32_t pack(uint8_t status, uint8_t key, uint8_t velocity) {
    return static_cast<uint32_t>(status) |
           (static_cast<uint32_t>(key) << 8) |
           (static_cast<uint32_t>(velocity) << 16);
}

struct ChordRouter {
    ChordEngine engine;
    uint8_t inputChannel = 0; // 0=channel 1, default factory keybed MIDI ch
    uint32_t emitted = 0;
    bool smart = false;        // opt-in: keep fixed-chord mode as default
    HarmonyContext harmony{}; // tonic C, major scale, strict in-key
    VoicedChord previous{};    // real note-level previous voicing
    unsigned smartFallbacks = 0;
    const char* lastChosenName = "";

    // Only quality is suggested: the root is always the actual played key's
    // pitch class. No allocations, external calls, or background analysis.
    MidiBatch makeSmartChord(uint8_t key, uint8_t velocity) {
        const uint8_t pc = static_cast<uint8_t>(key % 12);
        bool found = false;
        int bestRank = -32768;
        HarmonySuggestion best{};
        for (unsigned q = 0; q < static_cast<unsigned>(ChordType::Count); ++q) {
            HarmonySuggestion suggestion{};
            if (!SmartHarmony::evaluate(harmony, pc, static_cast<ChordType>(q), suggestion))
                continue;
            // Prefer richer diatonic structures on musical ties. This is a
            // simple transparent heuristic, NOT functional-harmony prediction.
            const int rank = static_cast<int>(suggestion.score) +
                             3 * mpk225::kChords[q].count;
            if (!found || rank > bestRank) {
                found = true;
                bestRank = rank;
                best = suggestion;
            }
        }

        // In strict key filtering an out-of-scale root can have zero matches.
        // Play the unchanged note instead of silently eating a key press.
        if (!found) {
            ++smartFallbacks;
            lastChosenName = "single note (out-of-scale)";
            const uint8_t singleton[1] = {key};
            return engine.noteOnVoicing(key, velocity, singleton, 1);
        }
        // Keep voicings near the played key, then minimize movement from the
        // previous chord. Allows real inversions, without octave wrap errors.
        const uint8_t low = key > 12 ? static_cast<uint8_t>(key-12) : 0;
        const uint8_t high = key < 99 ? static_cast<uint8_t>(key+28) : 127;
        VoicedChord chosen{};
        if (!VoicingPlanner::plan(pc, best.type, previous, low, high, chosen)) {
            ++smartFallbacks;
            lastChosenName = "single note (voicing unavailable)";
            const uint8_t singleton[1] = {key};
            return engine.noteOnVoicing(key, velocity, singleton, 1);
        }
        previous = chosen;
        harmony.hasPrevious = true;
        harmony.previousRoot = pc;
        harmony.previousType = best.type;
        lastChosenName = mpk225::kChords[static_cast<unsigned>(best.type)].name;
        return engine.noteOnVoicing(key, velocity, chosen.notes, chosen.count);
    }

    template <typename Send>
    void consume(uint32_t raw, Send send) {
        const uint8_t status = static_cast<uint8_t>(raw & 0xFF);
        const uint8_t key = static_cast<uint8_t>((raw >> 8) & 0x7F);
        const uint8_t velocity = static_cast<uint8_t>((raw >> 16) & 0x7F);
        const uint8_t family = static_cast<uint8_t>(status & 0xF0);
        const uint8_t channel = static_cast<uint8_t>(status & 0x0F);
        if (status < 0x80 || status >= 0xF0) return; // never forward SysEx/system data
        if (channel == inputChannel && (family == 0x90 || family == 0x80)) {
            const bool on = family == 0x90 && velocity != 0;
            const MidiBatch batch = on
                ? (smart ? makeSmartChord(key, velocity) : engine.noteOn(key, velocity))
                : engine.noteOff(key);
            if (smart && on) {
                std::printf("SMART key=%u root=%u quality=%s voices=%u\n",
                            static_cast<unsigned>(key),
                            static_cast<unsigned>(key % 12), lastChosenName,
                            static_cast<unsigned>(batch.count));
            }
            for (uint8_t i = 0; i < batch.count; ++i) {
                send(pack(static_cast<uint8_t>(batch.events[i].status | inputChannel),
                          batch.events[i].note, batch.events[i].velocity));
                ++emitted;
            }
        } else {
            send(raw & 0x00FFFFFFu); // pass through CC, pitch wheel, other channels
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

[[maybe_unused]] static uint8_t parseTonic(const char* name) {
    const std::string notes[] = {"C","C#","Db","D","D#","Eb","E","F",
                                  "F#","Gb","G","G#","Ab","A","A#","Bb","B"};
    const uint8_t values[] = {0,1,1,2,3,3,4,5,6,6,7,8,8,9,10,10,11};
    for (size_t i=0;i<sizeof(values)/sizeof(values[0]);++i)
        if (notes[i] == name) return values[i];
    std::fprintf(stderr, "Unknown tonic '%s' (examples: C, F#, Bb)\n",name);
    std::exit(2);
}

[[maybe_unused]] static ScaleMode parseScale(const char* name) {
    struct Choice { const char* label; ScaleMode value; };
    const Choice modes[] = {
        {"major",ScaleMode::Major}, {"minor",ScaleMode::NaturalMinor},
        {"dorian",ScaleMode::Dorian}, {"phrygian",ScaleMode::Phrygian},
        {"lydian",ScaleMode::Lydian}, {"mixolydian",ScaleMode::Mixolydian},
        {"locrian",ScaleMode::Locrian}, {"harmonic-minor",ScaleMode::HarmonicMinor},
        {"melodic-minor",ScaleMode::MelodicMinor},
        {"major-pentatonic",ScaleMode::MajorPentatonic},
        {"minor-pentatonic",ScaleMode::MinorPentatonic},
        {"chromatic",ScaleMode::Chromatic}
    };
    for (const auto& mode : modes)
        if (name == std::string(mode.label)) return mode.value;
    std::fprintf(stderr,"Unknown scale '%s'. Try major, minor, dorian, harmonic-minor, chromatic.\n",name);
    std::exit(2);
}

[[maybe_unused]] static mpk225::HarmonyFilter parseFilter(const char* name) {
    const std::string filter(name);
    if (filter=="strict") return mpk225::HarmonyFilter::Strict;
    if (filter=="borrowed") return mpk225::HarmonyFilter::Borrowed;
    if (filter=="any") return mpk225::HarmonyFilter::Any;
    std::fprintf(stderr,"Unknown filter '%s' (strict / borrowed / any)\n",name);
    std::exit(2);
}


// List every compatible type (all roots or one chosen root). Source of truth
// is the same no-allocation SmartHarmony evaluation used for live playing.
[[maybe_unused]] static size_t showSuggestions(const HarmonyContext& context,
                                                const char* rootArg) {
    const bool allRoots = std::string(rootArg) == "all";
    const uint8_t filterRoot = allRoots ? 0 : parseTonic(rootArg);
    std::array<HarmonySuggestion, SmartHarmony::kPossibilities> found{};
    size_t n = 0;
    SmartHarmony::forEach(context, [&](HarmonySuggestion item) {
        if (allRoots || item.root == filterRoot) found[n++] = item;
    });
    // Stable ties retain enumeration order; score is a heuristic.
    std::stable_sort(found.begin(), found.begin() + n,
        [](const HarmonySuggestion& a, const HarmonySuggestion& b) {
            return a.score > b.score;
        });
    const char* pitches[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    std::printf("%zu compatible voiced chord types; rank is heuristic only:\n", n);
    for (size_t i=0;i<n;++i) {
        const auto& choice=found[i];
        std::printf("%3zu  %-2s %-11s score=%d  non-scale=%u  shared=%u  motion=%u\n",
                    i+1,pitches[choice.root],
                    mpk225::kChords[static_cast<unsigned>(choice.type)].name,
                    static_cast<int>(choice.score),
                    static_cast<unsigned>(choice.nonScaleTones),
                    static_cast<unsigned>(choice.sharedTones),
                    static_cast<unsigned>(choice.motion));
    }
    if (n==0) std::puts("No matches. Try --filter borrowed or --filter any.");
    return n;
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
    // Enumeration covers all 49 types at each of 12 roots.
    HarmonyContext listContext{};
    if (SmartHarmony::forEach(listContext, [](HarmonySuggestion){}) != 73) return 20;
    listContext.filter=mpk225::HarmonyFilter::Any;
    if (SmartHarmony::forEach(listContext, [](HarmonySuggestion){}) !=
        SmartHarmony::kPossibilities) return 21;
    // Smart mode: C-key generates multiple notes all in C major.
    ChordRouter smart;
    smart.smart=true;
    smart.harmony.tonic=0;
    smart.harmony.scale=ScaleMode::Major;
    std::vector<uint32_t> result;
    auto track=[&](uint32_t msg){result.push_back(msg);};
    smart.consume(pack(0x90,60,91),track);
    if (result.size()<3 || result.size()>5 || !smart.harmony.hasPrevious) return 10;
    const uint16_t scale=SmartHarmony::scaleMask(0,ScaleMode::Major);
    for (auto msg: result) {
        if ((msg&0xF0u)!=0x90u) return 11;
        const unsigned pitch=static_cast<unsigned>((msg>>8)&0x7Fu);
        if (!(scale & (1u<<(pitch%12)))) return 12;
    }
    const size_t generated = result.size();
    smart.consume(pack(0x90,60,0),track); // MIDI velocity-zero note off
    if (result.size()!=generated*2) return 13;
    for (unsigned n=0;n<128;++n)
        if (smart.engine.noteReferences(static_cast<uint8_t>(n))) return 14;
    result.clear();
    smart.consume(pack(0x90,61,88),track); // C# is outside C major: raw-note fallback
    if (smart.smartFallbacks!=1 || result.size()!=1 ||
        ((result[0]>>8)&0x7Fu)!=61) return 15;
    smart.consume(pack(0x80,61,0),track);
    if (result.size()!=2 || ((result[1]>>8)&0x7Fu)!=61) return 16;
    result.clear();
    smart.consume(pack(0x90,67,95),track); // G chord follows the previous C chord
    if (result.empty() || result.size()>5) return 17;
    for (auto msg: result) {
        if (!(scale & (1u<<(((msg>>8)&0x7Fu)%12)))) return 18;
    }
    smart.stop(track);
    for (unsigned n=0;n<128;++n)
        if (smart.engine.noteReferences(static_cast<uint8_t>(n))) return 19;
    std::printf("PASS: live fixed/smart MIDI, in-key suggestions, fallback, overlap, all-offs\n");
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
    std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    return s;
}
static bool numberArg(const char* arg, unsigned& result) {
    char* end=nullptr;
    const unsigned long value=std::strtoul(arg,&end,10);
    if (!*arg || !end || *end || value>65535) return false;
    result=static_cast<unsigned>(value);
    return true;
}
// Read-only keybed/pad/encoder observation with the STOCK MPK225 firmware.
// This function does not open any MIDI output or transmit a single byte.
static int monitorOnly(unsigned index) {
    if (index >= midiInGetNumDevs()) {
        std::puts("Invalid MIDI input number; use --list."); return 2;
    }
    MIDIINCAPSA info{};
    if (midiInGetDevCapsA(index,&info,sizeof(info)) != MMSYSERR_NOERROR) return 2;
    HMIDIIN input=nullptr;
    mainThread=GetCurrentThreadId();
    MSG msg{};
    PeekMessage(&msg,nullptr,WM_USER,WM_USER,PM_NOREMOVE);
    const MMRESULT opened=midiInOpen(&input,index,mainThread,0,CALLBACK_THREAD);
    if (opened!=MMSYSERR_NOERROR) {
        std::printf("Cannot open input %u (error=%u)\n",index,opened);
        return 2;
    }
    if (midiInStart(input)!=MMSYSERR_NOERROR) {
        midiInClose(input); std::puts("Cannot start input monitoring"); return 2;
    }
    SetConsoleCtrlHandler(ctrlHandler,TRUE);
    std::printf("READ-ONLY MONITOR: %s. Press keys/pads/controls; Ctrl+C stops.\n",
                info.szPname);
    std::puts("No MIDI output is opened. No SysEx, update commands or firmware changes.");
    const ULONGLONG began=GetTickCount64();
    unsigned seen=0,presses=0,releases=0,errors=0;
    while (GetMessage(&msg,nullptr,0,0)>0) {
        if (msg.message==MM_MIM_ERROR || msg.message==MM_MIM_LONGERROR) {
            ++errors;
            std::puts("MIDI input driver reported malformed data");
            continue;
        }
        if (msg.message!=MM_MIM_DATA) continue;
        const uint32_t d=static_cast<uint32_t>(msg.lParam);
        const unsigned st=d&0xFFu;
        const unsigned a=(d>>8)&0x7Fu;
        const unsigned b=(d>>16)&0x7Fu;
        if(st<0x80u || st>=0xF0u) continue; // MIDI channel voice only
        const unsigned ch=(st&0xFu)+1u;
        const unsigned type=st&0xF0u;
        const bool noteOff=(type==0x80u || (type==0x90u && b==0));
        const char* kind = noteOff ? "NOTE OFF" :
                           type==0x90u ? "NOTE ON" :
                           type==0xA0u ? "POLY PRESSURE" :
                           type==0xB0u ? "CC" :
                           type==0xC0u ? "PROGRAM" :
                           type==0xD0u ? "CHANNEL PRESSURE" :
                           type==0xE0u ? "PITCH BEND" : "OTHER";
        if (type==0x90u && !noteOff) ++presses;
        if (noteOff) ++releases;
        ++seen;
        std::printf("%7llu ms   ch=%2u   %-16s  data1=%3u data2=%3u\n",
                    static_cast<unsigned long long>(GetTickCount64()-began),
                    ch,kind,a,b);
        std::fflush(stdout);
    }
    midiInStop(input);
    midiInReset(input);
    midiInClose(input);
    SetConsoleCtrlHandler(ctrlHandler,FALSE);
    std::printf("MONITOR STOPPED: messages=%u note-ons=%u note-offs=%u errors=%u\n",
                seen,presses,releases,errors);
    return errors ? 1 : 0;
}

int main(int argc, char** argv) {
    unsigned in=0, out=0, channel=1;
    ChordType chord=ChordType::Major7;
    bool useSmart=false;
    HarmonyContext harmony{};
    bool inputChosen=false, outputChosen=false, listed=false, monitor=false;
    std::string suggestRoot;
    for (int a=1;a<argc;++a) {
        const std::string opt(argv[a]);
        if (opt=="--selftest") return selfTest();
        if (opt=="--list") { listed=true; continue; }
        if (opt=="--monitor") { monitor=true; continue; }
        if (opt=="--smart") { useSmart=true; continue; }
        if (opt=="--help") {
            std::puts("Usage: MPK225-Live-Chords.exe --list");
            std::puts("       MPK225-Live-Chords.exe --monitor --input N  (read-only MIDI traffic)");
            std::puts("       MPK225-Live-Chords.exe --input N --output M --chord maj7 [--channel 1]");
            std::puts("       MPK225-Live-Chords.exe --input N --output M --smart --tonic C --scale major [--filter strict]");
            std::puts("       MPK225-Live-Chords.exe --suggest G --tonic C --scale major [--filter strict]");
            std::puts("       MPK225-Live-Chords.exe --suggest all --tonic C --scale major");
            std::puts("Suggestions are read-only: no MIDI device or output is opened.");
            std::puts("Smart filters: strict / borrowed / any. Modes include minor, dorian, lydian, harmonic-minor.");
            std::puts("Smart chooses a compatible quality per pressed root, then voices it near the key.");
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
        else if (opt=="--tonic") harmony.tonic=parseTonic(value);
        else if (opt=="--scale") harmony.scale=parseScale(value);
        else if (opt=="--filter") harmony.filter=parseFilter(value);
        else if (opt=="--suggest") suggestRoot=value;
        else { std::fprintf(stderr,"Unknown option: %s\n",opt.c_str()); return 2; }
    }
    if (!suggestRoot.empty()) {
        showSuggestions(harmony,suggestRoot.c_str());
        return 0;
    }
    if (listed) { listPorts(); return 0; }
    if (monitor) {
        if (!inputChosen) { std::puts("Specify --input N; use --list to find MPK225."); return 2; }
        return monitorOnly(in);
    }
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
    router.smart=useSmart;
    router.harmony=harmony;
    if (midiInStart(min)!=MMSYSERR_NOERROR) {
        midiInClose(min); midiOutClose(mout); std::puts("Could not start MIDI input"); return 2;
    }
    SetConsoleCtrlHandler(ctrlHandler,TRUE);
    std::printf("ACTIVE: %s -> %s; mode=%s; fixed chord=%s; key channel=%u.\n",
                cin.szPname,cout.szPname,useSmart?"SMART":"FIXED",
                router.engine.chordName(),channel);
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
