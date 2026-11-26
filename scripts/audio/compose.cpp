// Offline score renderer. Build and run through render.sh. No audio-device API.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
constexpr int rate = 44100;
constexpr double pi = 3.14159265358979323846;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

struct Entry { std::string id, name, purpose, asset; };
struct Catalog {
    std::vector<Entry> sounds, tracks;
    std::map<std::string, std::string> legacy;
};

// Deliberately limited to the catalog's object/array/string/integer schema.
// It never writes metadata, including aliases maintained by the application.
class CatalogReader {
    std::string text;
    std::size_t at = 0;
public:
    explicit CatalogReader(const fs::path& path) {
        require(fs::file_size(path) <= 65536, "Catalog exceeds 64 KiB");
        std::ifstream in(path, std::ios::binary);
        require(bool(in), "Cannot open catalog");
        text.assign(std::istreambuf_iterator<char>(in), {});
    }
    bool take(char c) {
        while (at < text.size() && (text[at] == ' ' || text[at] == '\n'
               || text[at] == '\r' || text[at] == '\t')) ++at;
        if (at < text.size() && text[at] == c) { ++at; return true; }
        return false;
    }
    void expect(char c) { require(take(c), std::string("Expected catalog token: ") + c); }
    unsigned hex4() {
        unsigned n = 0;
        for (int i = 0; i < 4; ++i) {
            require(at < text.size(), "Truncated Unicode escape");
            const char c = text[at++];
            const int v = c >= '0' && c <= '9' ? c - '0' :
                c >= 'a' && c <= 'f' ? c - 'a' + 10 :
                c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            require(v >= 0, "Invalid Unicode escape");
            n = n * 16 + unsigned(v);
        }
        return n;
    }
    std::string string() {
        expect('"');
        std::string out;
        while (at < text.size()) {
            const unsigned char c = static_cast<unsigned char>(text[at++]);
            if (c == '"') return out;
            require(c >= 32, "Control character in catalog string");
            if (c != '\\') { out += char(c); continue; }
            require(at < text.size(), "Truncated string escape");
            switch (text[at++]) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                unsigned cp = hex4();
                if (cp >= 0xd800 && cp <= 0xdbff) {
                    require(at + 2 <= text.size() && text[at] == '\\' && text[at+1] == 'u', "Missing low surrogate");
                    at += 2;
                    const unsigned low = hex4();
                    require(low >= 0xdc00 && low <= 0xdfff, "Invalid low surrogate");
                    cp = 0x10000 + ((cp - 0xd800) << 10) + low - 0xdc00;
                } else require(cp < 0xdc00 || cp > 0xdfff, "Unexpected low surrogate");
                if (cp < 0x80) out += char(cp);
                else if (cp < 0x800) { out += char(0xc0 | (cp >> 6)); out += char(0x80 | (cp & 63)); }
                else if (cp < 0x10000) {
                    out += char(0xe0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 63)); out += char(0x80 | (cp & 63));
                } else {
                    out += char(0xf0 | (cp >> 18)); out += char(0x80 | ((cp >> 12) & 63));
                    out += char(0x80 | ((cp >> 6) & 63)); out += char(0x80 | (cp & 63));
                }
                break;
            }
            default: throw std::runtime_error("Unsupported JSON escape");
            }
        }
        throw std::runtime_error("Unterminated catalog string");
    }
    std::vector<Entry> entries(bool sound) {
        std::vector<Entry> out;
        expect('[');
        if (take(']')) return out;
        do {
            expect('{');
            std::map<std::string, std::string> fields;
            if (!take('}')) {
                do {
                    const auto key = string(); expect(':');
                    require(key == "id" || key == "name" || key == "asset" || (sound && key == "purpose"), "Unexpected entry field: " + key);
                    require(fields.emplace(key, string()).second, "Duplicate entry field: " + key);
                } while (take(','));
                expect('}');
            }
            require(fields.size() == (sound ? 4u : 3u), "Incomplete catalog entry");
            for (const auto& [key, value] : fields) require(!value.empty(), "Empty catalog field: " + key);
            out.push_back({fields.at("id"), fields.at("name"), sound ? fields.at("purpose") : "", fields.at("asset")});
        } while (take(','));
        expect(']');
        return out;
    }
    Catalog read() {
        Catalog out;
        std::set<std::string> fields;
        expect('{');
        do {
            const auto key = string(); expect(':');
            require(fields.insert(key).second, "Duplicate catalog field: " + key);
            if (key == "version") expect('1');
            else if (key == "sounds") out.sounds = entries(true);
            else if (key == "tracks") out.tracks = entries(false);
            else if (key == "legacySounds") {
                expect('{');
                if (!take('}')) {
                    do {
                        const auto old = string(); expect(':');
                        const auto target = string();
                        require(!old.empty() && out.legacy.emplace(old, target).second, "Duplicate or empty legacy alias");
                    } while (take(','));
                    expect('}');
                }
            } else throw std::runtime_error("Unexpected catalog field: " + key);
        } while (take(','));
        expect('}'); take(' ');
        require(at == text.size() && fields.size() == 4, "Incomplete catalog or trailing content");
        require(out.sounds.size() == 20 && out.tracks.size() == 10, "Catalog must contain 20 sounds and 10 tracks");
        return out;
    }
};

enum class Tone { Felt, Pluck, Reed, Glass, Hollow, Warm, Sub, Veil, Halo, Metal, Kick, Rim, Hush, Drive, Brass, Snare, Tom };
struct Note { double at; int pitch; double hold; double level = 1; };
struct BassNote { double at, hold; int interval; double level = 1; };
struct Drum { double at; Tone tone; double level; };

// Each song has its own harmony, groove and returning phrase. The form values
// are the first chord, first theme, bridge, return and coda bars respectively.
struct Song {
    std::string id;
    int bpm, bars, chordBars;
    Tone chord, lead;
    std::array<int,4> roots;
    std::array<int,5> form;
    std::vector<Note> theme;
    std::vector<BassNote> bass;
    std::vector<Drum> drums;
    double hatStep, swing, chordLevel, leadLevel;
    double seconds() const { return bars*4*60.0/bpm; }
};

std::vector<Song> songs() {
    using T=Tone;
    return {
        // Syncopated bass and dry backbeat.
        {"switchyard",118,72,4,T::Hollow,T::Reed,{38,41,36,43},{4,12,32,40,64},
            {{0,62,7.9},{8,65,7.9},{16,60,7.9},{24,62,7.9}},
            {{0,1.3,0},{1.75,.6,7},{3,1.0,0},{4,1.4,0},{6.25,.6,7},{7.25,.7,0}},
            {{0,T::Kick,1},{1.75,T::Kick,.65},{1,T::Snare,.72},{3,T::Snare,.85},
             {4,T::Kick,1},{6.5,T::Kick,.6},{5,T::Snare,.7},{7,T::Snare,.9}},
            .5,.045,.095,.105},
        // Fast electronic march, with a held minor-key answer.
        {"iron-circuit",124,76,2,T::Drive,T::Brass,{35,38,42,40},{4,16,36,44,68},
            {{0,59,7.9},{8,62,3.9},{12,66,3.9},{16,64,7.9},{24,59,7.9}},
            {{0,1.8,0},{2.5,.8,7},{4,1.8,0},{6.5,.8,7}},
            {{0,T::Kick,1},{1.5,T::Kick,.65},{2,T::Snare,.8},{3.5,T::Kick,.55},
             {4,T::Kick,1},{5.5,T::Kick,.65},{6,T::Snare,.9},{7.5,T::Tom,.45}},
            .5,0,.13,.085},
        // Heavy, spacious trip-hop with a descending phrase.
        {"night-transit",96,68,4,T::Hollow,T::Warm,{36,32,39,31},{8,16,36,44,60},
            {{0,63,5.9},{6,67,5.9},{12,65,6.9},{19,63,6.9},{26,60,5.9}},
            {{0,2.5,0},{3.25,.7,7},{4.5,2.4,0},{7.25,.5,7}},
            {{0,T::Kick,1},{3.5,T::Kick,.48},{2,T::Snare,.78},
             {4.5,T::Kick,.8},{6,T::Snare,.85},{7.5,T::Tom,.45}},
            1,0,.085,.115},
        // Bright Mixolydian funk, led by the bass rather than upper notes.
        {"copper-wire",110,72,2,T::Reed,T::Hollow,{45,43,38,40},{4,16,36,44,68},
            {{0,57,7.9},{8,61,7.9},{16,64,7.9},{24,67,7.9}},
            {{0,.95,0},{1.25,.6,7},{2.75,.9,0},{4,1.1,0},{5.5,.6,7},{6.75,.9,0}},
            {{0,T::Kick,1},{2.75,T::Kick,.7},{1,T::Snare,.65},{3.5,T::Snare,.9},
             {4,T::Kick,1},{5.25,T::Kick,.55},{5,T::Snare,.65},{7,T::Snare,.9}},
            .5,.065,.075,.09},
        // Slow, wide chords and a tom-led half-time pulse.
        {"cold-front",92,64,4,T::Veil,T::Brass,{38,34,41,36},{8,20,36,44,56},
            {{0,57,7.9},{8,62,7.9},{16,65,7.9},{24,64,7.9}},
            {{0,3.3,0},{3.5,.4,7},{4,3.3,0},{7.5,.4,7}},
            {{0,T::Kick,1},{2,T::Snare,.7},{3.5,T::Tom,.45},
             {4,T::Kick,.85},{6,T::Snare,.85},{7.25,T::Tom,.5}},
            1,0,.075,.085},
        // Rolling toms and a low, upward mechanical motif.
        {"dust-route",106,72,2,T::Drive,T::Warm,{42,38,33,40},{8,16,36,44,64},
            {{0,54,7.9},{8,57,7.9},{16,61,7.9},{24,59,7.9}},
            {{0,1.7,0},{2.25,1.0,7},{4,1.7,0},{6.25,1.0,7}},
            {{0,T::Kick,1},{1.5,T::Tom,.5},{3,T::Snare,.76},
             {4,T::Kick,1},{5,T::Tom,.43},{6.5,T::Kick,.55},{7,T::Snare,.9}},
            .5,0,.11,.10},
        // Quick breakbeat with a long, open response.
        {"pulse-engine",132,80,2,T::Drive,T::Reed,{43,34,41,38},{2,10,30,38,70},
            {{0,67,3.9},{4,70,3.9},{8,74,7.9},{16,72,7.9},{24,67,7.9}},
            {{0,1.0,0},{1.5,.55,7},{3,.8,0},{4,1.0,0},{5.5,.55,7},{7,.8,0}},
            {{0,T::Kick,1},{1.5,T::Kick,.68},{2,T::Snare,.9},{3,T::Kick,.6},
             {4,T::Kick,1},{5.5,T::Kick,.68},{6,T::Snare,.92},{7,T::Kick,.6}},
            .25,.025,.095,.075},
        // Weighty half-time rhythm with sparse power chords.
        {"concrete-sky",122,76,4,T::Drive,T::Hollow,{37,33,40,35},{4,12,34,42,68},
            {{0,61,7.9},{8,64,7.9},{16,68,7.9},{24,59,7.9}},
            {{0,2.0,0},{2.75,.7,7},{4,2.0,0},{6.75,.7,7}},
            {{0,T::Kick,1},{1,T::Kick,.55},{2.5,T::Snare,.9},{3.75,T::Tom,.43},
             {4,T::Kick,1},{5,T::Kick,.55},{6.5,T::Snare,.92},{7.5,T::Tom,.45}},
            .5,0,.12,.09},
        // Open Mixolydian rock pulse with a rising refrain.
        {"waypoint",116,72,4,T::Drive,T::Warm,{38,36,43,45},{4,12,32,40,64},
            {{0,62,5.9},{6,66,5.9},{12,69,5.9},{18,72,5.9},{24,69,7.9}},
            {{0,2.0,0},{2.5,1.0,7},{4,2.0,0},{6.5,1.0,7}},
            {{0,T::Kick,1},{1,T::Snare,.75},{2,T::Kick,.65},{3,T::Snare,.85},
             {4,T::Kick,1},{5,T::Snare,.75},{6.5,T::Kick,.65},{7,T::Snare,.9}},
            .5,0,.10,.11}
    };
}

struct CueNote { double at, hold; int pitch; Tone tone; double level, pan = 0; };
struct Cue { std::string id; double seconds; int channels; std::vector<CueNote> notes; };

std::vector<Cue> cues() {
    using T = Tone;
    return {
        {"soft-confirm",.38,1,{{0,.23,67,T::Felt,.7},{.105,.25,74,T::Warm,.48}}},
        {"gentle-change",.46,1,{{0,.24,64,T::Hollow,.65},{.17,.27,66,T::Felt,.55}}},
        {"attention",.87,1,{{0,.22,67,T::Warm,.58},{.27,.22,67,T::Warm,.48},{.55,.28,72,T::Felt,.43}}},
        {"complete",1.02,2,{{0,.46,55,T::Warm,.3,0},{.07,.35,67,T::Felt,.55,-.22},{.29,.35,71,T::Felt,.5,.1},{.52,.45,74,T::Glass,.42,.24}}},
        {"soft-warning",.78,1,{{0,.32,60,T::Hollow,.6},{.12,.3,61,T::Warm,.23},{.43,.3,59,T::Felt,.47}}},
        {"new-activity",.69,2,{{0,.26,71,T::Pluck,.48,-.3},{.21,.24,67,T::Felt,.4,.3},{.39,.27,69,T::Hollow,.48,0}}},
        {"panel-tick",.17,1,{{0,.15,62,T::Metal,.52}}},
        {"radar-ready",1.15,2,{{0,.7,48,T::Veil,.27,-.18},{.09,.35,60,T::Hollow,.5,-.45},{.36,.35,67,T::Felt,.46,.45},{.68,.42,72,T::Glass,.4,0}}},
        {"critical",1.04,2,{{0,.3,65,T::Warm,.55,-.18},{.08,.3,66,T::Hollow,.22,.18},{.44,.33,65,T::Warm,.58,.18},{.55,.38,60,T::Felt,.36,-.18}}},
        {"disconnect",.77,1,{{0,.3,64,T::Hollow,.53},{.23,.28,60,T::Warm,.48},{.44,.29,57,T::Felt,.4}}},
        {"connect",.74,1,{{0,.26,57,T::Felt,.45},{.19,.29,64,T::Warm,.5},{.42,.28,71,T::Hollow,.44}}},
        {"save",.66,1,{{0,.32,66,T::Felt,.4},{.03,.32,69,T::Warm,.34},{.29,.32,74,T::Felt,.54}}},
        {"open",.48,1,{{0,.3,60,T::Hollow,.45},{.13,.31,67,T::Pluck,.5}}},
        {"close",.4,1,{{0,.24,67,T::Felt,.46},{.11,.25,64,T::Hollow,.4}}},
        {"notice",.85,1,{{0,.65,58,T::Warm,.23},{.09,.68,75,T::Felt,.48}}},
        {"recover",1.12,2,{{0,.35,60,T::Warm,.4,-.18},{.21,.35,62,T::Felt,.43,-.08},{.46,.37,67,T::Hollow,.46,.1},{.72,.35,71,T::Felt,.42,.2}}},
        {"resource-added",.61,2,{{0,.29,67,T::Pluck,.46,-.2},{.24,.33,71,T::Warm,.5,.2}}},
        {"resource-removed",.64,2,{{0,.24,74,T::Hollow,.44,.2},{.18,.24,69,T::Felt,.42,0},{.36,.24,66,T::Pluck,.36,-.2}}},
        {"capacity",1.18,1,{{0,.38,59,T::Warm,.5},{.28,.35,66,T::Hollow,.35},{.74,.39,59,T::Felt,.45}}},
        {"heartbeat",.77,1,{{0,.26,45,T::Sub,.63},{.19,.24,52,T::Sub,.42},{.43,.29,57,T::Warm,.22}}}
    };
}

std::string contour(const std::vector<Note>& notes) {
    std::ostringstream out;
    for (const auto& note:notes)
        out << note.at << ':' << note.pitch-notes.front().pitch << ':' << note.hold << ';';
    return out.str();
}

void validate(const Catalog& catalog,const std::vector<Song>& music,const std::vector<Cue>& sounds) {
    require(music.size()==9 && sounds.size()==20, "Expected nine scored songs, Forge and twenty cues");
    std::set<std::string> songIds{"forge"}, cueIds, themes;
    for (const auto& song:music) {
        require(songIds.insert(song.id).second, "Duplicate song ID: "+song.id);
        require(song.bpm>=80 && song.bpm<=140 && song.bars>=64 && song.bars<=80
            && (song.chordBars==2 || song.chordBars==4) && song.seconds()>=120 && song.seconds()<=190,
            "Invalid song timing: "+song.id);
        require(song.form[0]<song.form[1] && song.form[1]<song.form[2]
            && song.form[2]<song.form[3] && song.form[3]<song.form[4]
            && song.form[4]<song.bars && song.form[0]>=0, "Invalid song form: "+song.id);
        require(song.theme.size()>=4 && song.theme.size()<=8 && song.bass.size()>=4
            && song.drums.size()>=5 && themes.insert(contour(song.theme)).second,
            "Missing or repeated song material: "+song.id);
        for (const auto& note:song.theme)
            require(note.at>=0 && note.hold>=2 && note.at+note.hold<=32.1
                && note.pitch>=48 && note.pitch<=76 && note.level>0 && note.level<=1,
                "Invalid song phrase: "+song.id);
        for (const auto& note:song.bass)
            require(note.at>=0 && note.at+note.hold<=8 && note.hold>0
                && (note.interval==0 || note.interval==7 || note.interval==12)
                && note.level>0 && note.level<=1, "Invalid bass part: "+song.id);
        for (const auto& note:song.drums)
            require(note.at>=0 && note.at<8 && note.level>0 && note.level<=1
                && (note.tone==Tone::Kick || note.tone==Tone::Snare || note.tone==Tone::Rim
                    || note.tone==Tone::Tom || note.tone==Tone::Metal), "Invalid drum part: "+song.id);
        for (const int root:song.roots) require(root>=28 && root<=48, "Invalid harmony: "+song.id);
        require((song.hatStep==.25 || song.hatStep==.5 || song.hatStep==1)
            && song.swing>=0 && song.swing<=.1 && song.chordLevel>0 && song.chordLevel<=.2
            && song.leadLevel>0 && song.leadLevel<=.2, "Invalid song mix: "+song.id);
    }
    for (const auto& cue:sounds) {
        require(cueIds.insert(cue.id).second, "Duplicate cue ID");
        require(cue.seconds>=.1 && cue.seconds<=2 && (cue.channels==1 || cue.channels==2) && !cue.notes.empty(), "Invalid cue: "+cue.id);
        for (const auto& note:cue.notes)
            require(note.at>=0 && note.hold>0 && note.at+note.hold<=cue.seconds
                && note.pitch>=36 && note.pitch<=88 && std::abs(note.pan)<=1
                && note.level>0 && note.level<=1, "Invalid cue note: "+cue.id);
    }
    const auto entries=[](const std::vector<Entry>& items,const std::set<std::string>& ids,bool sound) {
        std::set<std::string> found;
        for (const auto& entry:items) {
            require(ids.count(entry.id)==1 && found.insert(entry.id).second, "Catalog/score ID mismatch: "+entry.id);
            require(!entry.id.empty() && entry.id.front()!='-' && entry.id.back()!='-', "Invalid asset ID");
            char previous=0;
            for (const char c:entry.id) {
                require((c>='a'&&c<='z') || (c>='0'&&c<='9') || (c=='-'&&previous!='-'), "Unsafe asset ID");
                previous=c;
            }
            require(entry.asset=="qrc:/podlord/audio/"+std::string(sound?"sfx/":"music/")
                +entry.id+(sound?".wav":".m4a"), "Unexpected asset path: "+entry.id);
        }
        require(found==ids, "Catalog omits a composition");
    };
    entries(catalog.sounds,cueIds,true);
    entries(catalog.tracks,songIds,false);
    for (const auto& [old,target]:catalog.legacy)
        require(!old.empty() && cueIds.count(target)==1, "Invalid legacy sound alias: "+old);
}

const std::array<double,16384>& sineTable() {
    static const auto table=[] {
        std::array<double,16384> data{};
        for (std::size_t i=0;i<data.size();++i) data[i]=std::sin(2*pi*double(i)/double(data.size()));
        return data;
    }();
    return table;
}
double wave(double cycles) {
    const double position=cycles*16384;
    const auto whole=static_cast<std::uint64_t>(position);
    const auto& table=sineTable();
    const auto index=std::size_t(whole&16383);
    return table[index]+(table[(index+1)&16383]-table[index])*(position-double(whole));
}
double smooth(double x) { x=std::clamp(x,0.0,1.0); return x*x*(3-2*x); }
std::uint32_t seedFor(std::string_view id) {
    std::uint32_t seed=2166136261u;
    for (const unsigned char c:id) seed=(seed^c)*16777619u;
    return seed ? seed : 1;
}
struct Audio {
    int channels;
    bool loop;
    std::vector<float> pcm;
    Audio(double seconds,int count,bool circular):channels(count),loop(circular),
        pcm(std::size_t(std::llround(seconds*rate))*std::size_t(count),0) {}
    std::size_t frames() const { return pcm.size()/std::size_t(channels); }
};

// Additive, band-limited musical voices and generated, filtered percussion.
// Each note starts and ends with a smooth envelope. No sampled instruments.
void voice(Audio& audio,double start,double duration,int midi,Tone tone,double level,double pan,std::uint32_t seed) {
    const double hz=440*std::exp2((midi-69)/12.0);
    const bool pad=tone==Tone::Veil || tone==Tone::Halo;
    const bool drum=tone==Tone::Kick || tone==Tone::Rim || tone==Tone::Hush || tone==Tone::Snare || tone==Tone::Tom;
    const double attack=std::min(duration*.3,pad?.55:drum?.009:tone==Tone::Sub?.018:.02);
    const double release=std::min(duration*.4,pad?.9:tone==Tone::Sub?.13:.16);
    const double cutoff=tone==Tone::Sub?650:pad?1050:tone==Tone::Metal?1650:tone==Tone::Hush?3200:tone==Tone::Snare?4600:tone==Tone::Drive||tone==Tone::Brass?3800:2100;
    const double filter=1-std::exp(-2*pi*cutoff/rate);
    const double fall=std::exp(-(tone==Tone::Metal?13:tone==Tone::Pluck?5:tone==Tone::Glass?3.5:drum?16:1.6)/rate);
    const double bendFall=std::exp(-35.0/rate);
    const double left=std::sqrt((1-pan)*.5), right=std::sqrt((1+pan)*.5);
    double phase=0, decay=1, bend=1, filtered=0, previousNoise=0;
    const auto first=std::size_t(std::llround(start*rate));
    const auto length=std::size_t(std::llround(duration*rate));
    for (std::size_t i=0;i<length;++i) {
        const std::size_t absolute=first+i;
        if (!audio.loop && absolute>=audio.frames()) break;
        const double t=double(i)/rate;
        double sample=0;
        switch (tone) {
        case Tone::Felt: sample=(wave(phase)+.13*wave(phase*2)+.035*wave(phase*3))*(.48+.52*decay); break;
        case Tone::Pluck: sample=(.84*wave(phase)+.16*wave(phase*2.002))*(.15+.85*decay); break;
        case Tone::Reed: sample=.79*wave(phase)+.15*wave(phase*3)+.06*wave(phase*5); break;
        case Tone::Glass: sample=(.83*wave(phase)+.11*wave(phase*2.76)+.06*wave(phase*4.11))*(.12+.88*decay); break;
        case Tone::Hollow: sample=(.84*wave(phase)+.16*wave(phase*2))*(.84+.16*wave(.17+t*.67)); break;
        case Tone::Warm: sample=.85*wave(phase)+.1*wave(phase*2.005)+.05*wave(phase*3); break;
        case Tone::Sub: sample=.96*wave(phase)+.04*wave(phase*2); break;
        case Tone::Veil: sample=.53*wave(phase)+.32*wave(phase*1.0018)+.15*wave(phase*2); break;
        case Tone::Halo: sample=.64*wave(phase)+.24*wave(phase*1.0031)+.12*wave(phase*3); break;
        case Tone::Metal: sample=(.72*wave(phase)+.18*wave(phase*2.37)+.1*wave(phase*3.61))*(.025+.975*decay); break;
        case Tone::Drive: {
            const double body=wave(phase)+.52*wave(phase*2)+.27*wave(phase*3)+.15*wave(phase*4)+.11*wave(phase*5);
            const double drive=body/(1+.55*std::abs(body));
            sample=(drive*.85+.15*wave(phase*1.003))*(.66+.34*decay);
            break;
        }
        case Tone::Brass:
            sample=(.68*wave(phase)+(.18+.08*wave(t*4))*wave(phase*2)+.12*wave(phase*3)+.06*wave(phase*5))*(.76+.24*decay);
            break;
        case Tone::Tom: sample=(.84*wave(phase)+.16*wave(phase*1.55))*decay; break;
        case Tone::Kick: sample=wave(phase)*decay; break;
        case Tone::Snare:
        case Tone::Rim:
        case Tone::Hush: {
            seed^=seed<<13; seed^=seed>>17; seed^=seed<<5;
            const double noise=double(seed)/2147483647.5-1;
            const double detail=(noise-previousNoise)*.5;
            previousNoise=noise;
            sample=(tone==Tone::Rim?.77*wave(phase)+.23*detail:tone==Tone::Snare?.26*wave(phase)+.48*noise+.26*detail:detail*.65)*decay;
            break;
        }
        }
        filtered+=filter*(sample-filtered);
        const double envelope=smooth(t/attack)*smooth((duration-t)/release);
        const double value=filtered*envelope*level;
        const std::size_t frame=absolute%audio.frames();
        if (audio.channels==1) audio.pcm[frame]+=float(value);
        else {
            audio.pcm[frame*2]+=float(value*left);
            audio.pcm[frame*2+1]+=float(value*right);
        }
        phase+=(hz+(tone==Tone::Kick?85*bend:tone==Tone::Tom?38*bend:0))/rate;
        decay*=fall; bend*=bendFall;
    }
}

void finish(Audio& audio,bool music) {
    if (music) {
        const auto dry=audio.pcm;
        // Two finite, cross-channel room reflections, circular at the loop seam.
        const std::size_t d1=std::size_t(rate*.113), d2=std::size_t(rate*.271);
        const auto n=audio.frames();
        for (std::size_t i=0;i<n;++i) for (std::size_t c=0;c<2;++c)
            audio.pcm[i*2+c]+=dry[((i+n-d1)%n)*2+(1-c)]*.075f+dry[((i+n-d2)%n)*2+c]*.035f;
    }
    for (int c=0;c<audio.channels;++c) {
        double sum=0;
        for (std::size_t i=std::size_t(c);i<audio.pcm.size();i+=std::size_t(audio.channels)) sum+=audio.pcm[i];
        const float mean=float(sum/double(audio.frames()));
        for (std::size_t i=std::size_t(c);i<audio.pcm.size();i+=std::size_t(audio.channels)) audio.pcm[i]-=mean;
    }
    if (!music) {
        const auto n=audio.frames();
        for (std::size_t i=0;i<n;++i) {
            const double gate=smooth(double(i)/(rate*.006))*smooth(double(n-1-i)/(rate*.009));
            for (int c=0;c<audio.channels;++c) audio.pcm[i*std::size_t(audio.channels)+std::size_t(c)]*=float(gate);
        }
    }
    double peak=0, energy=0;
    for (const float x:audio.pcm) {
        require(std::isfinite(x), "Non-finite synthesis output");
        peak=std::max(peak,std::abs(double(x))); energy+=double(x)*x;
    }
    require(peak>1e-6 && energy>1e-8, "Silent composition");
    const double rms=std::sqrt(energy/double(audio.pcm.size()));
    const double gain=std::min((music?.42:.18)/peak,(music?.075:.035)/rms);
    for (float& x:audio.pcm) x*=float(gain);
}

void finishTrack(Audio& audio) {
    finish(audio,true);
    const auto frames=audio.frames();
    for (std::size_t frame=0;frame<frames;++frame) {
        const double gain=smooth(double(frame)/(rate*1.0))*smooth(double(frames-1-frame)/(rate*5.0));
        audio.pcm[frame*2]*=float(gain);
        audio.pcm[frame*2+1]*=float(gain);
    }
}

Audio renderForge() {
    constexpr int bars=72;
    constexpr double secondsPerBeat=60.0/112.0;
    Audio audio(bars*4*secondsPerBeat,2,false);
    const auto seed=seedFor("forge");
    std::uint32_t event=0;
    const auto play=[&](double beat,double hold,int pitch,Tone tone,double level,double pan=0) {
        voice(audio,beat*secondsPerBeat,hold*secondsPerBeat,pitch,tone,level,pan,seed+(++event));
    };
    const std::array<int,4> roots{40,43,38,45};
    const std::array<Note,8> hook{{{0,64,4.1},{4,64,4.1},{8,67,4.1},{12,64,4.1},
        {16,62,4.1},{20,64,4.1},{24,59,4.1},{28,64,4.1}}};
    for (int bar=0;bar<bars;++bar) {
        const double beat=bar*4.0;
        const int root=roots[std::size_t(bar/4)%roots.size()];
        const bool bridge=bar>=36 && bar<44;
        const double energy=bar<4?.68:bridge?.72:bar>=64?.76:bar>=44?1.0:.9;
        play(beat,3.9,root,Tone::Sub,.25*energy);
        play(beat,.34,35,Tone::Kick,.38*energy);
        play(beat+2,.3,35,Tone::Kick,.29*energy);
        if (!bridge) play(beat+3.5,.25,35,Tone::Kick,.16*energy);
        play(beat+(bridge?2:1),.24,49,Tone::Snare,.31*energy,.08);
        if (!bridge) play(beat+3,.23,49,Tone::Snare,.29*energy,.08);
        for (int tick=0;tick<8;++tick)
            play(beat+tick*.5,.13,75,Tone::Hush,(tick%2?.048:.073)*energy,tick%2?.36:-.36);
        if (bar%8==7) {
            play(beat+2.5,.31,46,Tone::Tom,.17*energy,-.2);
            play(beat+3.25,.27,43,Tone::Tom,.2*energy,.2);
        }
        if (bar>=4 && !bridge && bar%2==0) {
            play(beat,8.1,root+12,Tone::Drive,.13*energy,-.23);
            play(beat,8.1,root+19,Tone::Drive,.055*energy,.28);
        }
        if (bridge && bar%2==0)
            play(beat,8.1,root+24,Tone::Veil,.045*energy,-.45);
        if ((bar>=20 && bar<36) || (bar>=44 && bar<64)) {
            const int phrase=(bar>=44?bar-44:bar-20)%8;
            for (const auto& note:hook) {
                const double at=beat+note.at-phrase*4;
                if (at<beat || at>=beat+4) continue;
                play(at,note.hold,note.pitch,Tone::Warm,.105*energy,.13);
            }
        }
    }
    finishTrack(audio);
    return audio;
}

Audio render(const Song& song) {
    const double secondsPerBeat=60.0/song.bpm;
    Audio audio(song.seconds(),2,false);
    const auto seed=seedFor(song.id);
    std::uint32_t event=0;
    const auto play=[&](double beat,double hold,int pitch,Tone tone,double level,double pan=0) {
        voice(audio,beat*secondsPerBeat,hold*secondsPerBeat,pitch,tone,level,pan,seed+(++event));
    };
    for (int bar=0;bar<song.bars;++bar) {
        const double beat=bar*4.0;
        const int root=song.roots[std::size_t(bar/song.chordBars)%song.roots.size()];
        const bool bridge=bar>=song.form[2] && bar<song.form[3];
        const bool theme=(bar>=song.form[1] && bar<song.form[2])
            || (bar>=song.form[3] && bar<song.form[4]);
        const double energy=bar<song.form[0]?.68:bridge?.72:bar>=song.form[4]?.7:theme?1.0:.86;
        play(beat,4.05,root,Tone::Sub,.13*energy);
        if (bar>=song.form[0] && bar%song.chordBars==0) {
            const double hold=song.chordBars*4+.1;
            if (bridge) play(beat,hold,root+24,Tone::Veil,.045*energy,-.3);
            else {
                play(beat,hold,root+12,song.chord,song.chordLevel*energy,-.27);
                play(beat,hold,root+19,song.chord,song.chordLevel*.42*energy,.31);
            }
        }
        for (const auto& note:song.bass) {
            const double at=beat+note.at-4*(bar%2);
            if (at>=beat && at<beat+4)
                play(at,note.hold,root+note.interval,Tone::Sub,.28*note.level*energy);
        }
        for (const auto& hit:song.drums) {
            const double at=beat+hit.at-4*(bar%2);
            if (at<beat || at>=beat+4) continue;
            const int pitch=hit.tone==Tone::Kick?35:hit.tone==Tone::Tom?45:hit.tone==Tone::Metal?73:50;
            const double hold=hit.tone==Tone::Kick?.32:hit.tone==Tone::Tom?.3:.23;
            const double volume=hit.tone==Tone::Kick?.36:hit.tone==Tone::Snare?.29:
                hit.tone==Tone::Tom?.2:hit.tone==Tone::Rim?.15:.08;
            play(at,hold,pitch,hit.tone,volume*hit.level*energy);
        }
        const int ticks=int(4/song.hatStep);
        for (int tick=0;tick<ticks;++tick)
            play(beat+tick*song.hatStep+(tick%2?song.swing:0),.11,77,Tone::Hush,
                (song.hatStep==.25?.026:tick%2?.038:.058)*energy,tick%2?.35:-.35);
        if (theme) {
            const int start=bar<song.form[2]?song.form[1]:song.form[3];
            const int phrase=(bar-start)%8;
            for (const auto& note:song.theme) {
                const double at=beat+note.at-phrase*4;
                if (at>=beat && at<beat+4)
                    play(at,note.hold,note.pitch,song.lead,song.leadLevel*note.level*energy,.12);
            }
        }
    }
    finishTrack(audio);
    return audio;
}

Audio render(const Cue& cue) {
    Audio audio(cue.seconds,cue.channels,false);
    std::uint32_t seed=seedFor(cue.id);
    for (const auto& n:cue.notes) voice(audio,n.at,n.hold,n.pitch,n.tone,n.level,n.pan,++seed);
    finish(audio,false);
    return audio;
}

void little(std::ostream& out,std::uint32_t value,int bytes) {
    for (int i=0;i<bytes;++i) out.put(char((value>>(i*8))&255));
}
void writeWave(const fs::path& path,const Audio& audio) {
    require(audio.pcm.size()<(std::numeric_limits<std::uint32_t>::max()-44)/2, "WAV too large");
    std::ofstream out(path,std::ios::binary|std::ios::trunc);
    require(bool(out), "Cannot create WAV: "+path.string());
    const auto bytes=std::uint32_t(audio.pcm.size()*2);
    out.write("RIFF",4); little(out,bytes+36,4); out.write("WAVEfmt ",8);
    little(out,16,4); little(out,1,2); little(out,std::uint32_t(audio.channels),2);
    little(out,rate,4); little(out,std::uint32_t(rate*audio.channels*2),4);
    little(out,std::uint32_t(audio.channels*2),2); little(out,16,2);
    out.write("data",4); little(out,bytes,4);
    for (const float value:audio.pcm) {
        require(std::isfinite(value) && std::abs(value)<.99, "Unsafe sample before PCM conversion");
        const auto pcm=std::int16_t(std::lround(double(value)*32767));
        little(out,std::uint16_t(pcm),2);
    }
    out.close(); require(bool(out), "WAV write failed: "+path.string());
}

struct Metrics {
    int channels=0;
    double seconds=0, peak=0, rms=0, step=0, seam=0, dc=0, quietSeconds=0;
    std::uint64_t hash=14695981039346656037ull;
};
std::uint32_t readLittle(const std::vector<unsigned char>& data,std::size_t at,int count) {
    require(at+std::size_t(count)<=data.size(), "Truncated WAV integer");
    std::uint32_t out=0;
    for (int i=0;i<count;++i) out|=std::uint32_t(data[at+std::size_t(i)])<<(8*i);
    return out;
}
Metrics inspectWave(const fs::path& path) {
    require(fs::file_size(path)<=64*1024*1024, "Unexpectedly large WAV");
    std::ifstream in(path,std::ios::binary);
    require(bool(in), "Cannot open WAV: "+path.string());
    const std::vector<unsigned char> data{std::istreambuf_iterator<char>(in),{}};
    const auto tag=[&](std::size_t p,std::string_view value) {
        return p+value.size()<=data.size() && std::equal(value.begin(),value.end(),data.begin()+std::ptrdiff_t(p));
    };
    require(data.size()>=44 && tag(0,"RIFF") && tag(8,"WAVE") && readLittle(data,4,4)+8u==data.size(), "Invalid WAV container");
    Metrics metrics;
    std::size_t begin=0,length=0;
    bool format=false;
    for (std::size_t p=12;p+8<=data.size();) {
        const auto bytes=std::size_t(readLittle(data,p+4,4));
        require(bytes<=data.size()-p-8, "Invalid WAV chunk extent");
        if (tag(p,"fmt ")) {
            require(!format && bytes>=16, "Invalid WAV format chunk");
            const auto code=readLittle(data,p+8,2);
            require(code==1 || (code==0xfffe && bytes>=40 && readLittle(data,p+32,2)==1), "Expected PCM WAV");
            metrics.channels=int(readLittle(data,p+10,2));
            require((metrics.channels==1 || metrics.channels==2) && readLittle(data,p+12,4)==rate
                && readLittle(data,p+22,2)==16 && readLittle(data,p+20,2)==unsigned(metrics.channels*2), "Unexpected PCM format");
            format=true;
        } else if (tag(p,"data")) {
            require(begin==0, "Multiple PCM data chunks"); begin=p+8; length=bytes;
        }
        p+=8+bytes+(bytes&1);
    }
    require(format && begin>0 && length>0 && length%std::size_t(metrics.channels*2)==0, "Missing PCM data");
    const auto samples=length/2, frames=samples/std::size_t(metrics.channels);
    double energy=0, windowEnergy=0;
    std::size_t windowSamples=0;
    std::vector<double> windowLevels;
    std::array<double,2> sum{},first{},previous{};
    for (std::size_t i=0;i<samples;++i) {
        const auto p=begin+i*2;
        const int integer=int(readLittle(data,p,2));
        const double x=double(integer>=32768?integer-65536:integer)/32768;
        const auto channel=i%std::size_t(metrics.channels);
        if (i<std::size_t(metrics.channels)) first[channel]=x;
        else metrics.step=std::max(metrics.step,std::abs(x-previous[channel]));
        previous[channel]=x; sum[channel]+=x; energy+=x*x;
        windowEnergy+=x*x;
        if (++windowSamples==std::size_t(rate*metrics.channels/2)) {
            windowLevels.push_back(std::sqrt(windowEnergy/double(windowSamples)));
            windowEnergy=0; windowSamples=0;
        }
        metrics.peak=std::max(metrics.peak,std::abs(x));
        metrics.hash=(metrics.hash^data[p])*1099511628211ull;
        metrics.hash=(metrics.hash^data[p+1])*1099511628211ull;
    }
    metrics.seconds=double(frames)/rate;
    metrics.rms=std::sqrt(energy/double(samples));
    if (!windowLevels.empty()) {
        auto levels=windowLevels;
        std::nth_element(levels.begin(),levels.begin()+std::ptrdiff_t(levels.size()/2),levels.end());
        const double quietLimit=levels[levels.size()/2]*.22;
        int run=0, longest=0;
        for (const double level:windowLevels) {
            run=level<quietLimit?run+1:0;
            longest=std::max(longest,run);
        }
        metrics.quietSeconds=longest*.5;
    }
    for (int c=0;c<metrics.channels;++c) {
        metrics.dc=std::max(metrics.dc,std::abs(sum[std::size_t(c)]/double(frames)));
        metrics.seam=std::max(metrics.seam,std::abs(previous[std::size_t(c)]-first[std::size_t(c)]));
    }
    return metrics;
}

template<class T> const T& findId(const std::vector<T>& values,const std::string& id) {
    const auto found=std::find_if(values.begin(),values.end(),[&](const T& value){return value.id==id;});
    require(found!=values.end(), "Unknown score: "+id); return *found;
}

void audit(const fs::path& stage,const Catalog& catalog,const std::vector<Song>& music,const std::vector<Cue>& sounds) {
    std::set<std::uint64_t> hashes;
    std::uintmax_t total=0;
    int mono=0,stereo=0;
    std::cout << "kind\tid\tchannels\tseconds\tbytes\tpeak\trms\tmax_step\tseam\tdc\tpcm_fnv64\n";
    const auto checked=[&](const Entry& e,bool track) {
        const auto pcm=stage/(track?"decoded":"sfx")/(e.id+".wav");
        const auto asset=stage/(track?"music":"sfx")/(e.id+(track?".m4a":".wav"));
        const auto m=inspectWave(pcm);
        const double expected=track?(e.id=="forge"?72*4*60.0/112:findId(music,e.id).seconds())
            :findId(sounds,e.id).seconds;
        const int channels=track?2:findId(sounds,e.id).channels;
        require(m.channels==channels && std::abs(m.seconds-expected)<.08, "Duration/channel mismatch: "+e.id);
        require(m.peak<=(track?.56:.181) && m.rms>=(track?.02:.008) && m.rms<=(track?.09:.036), "Unsafe peak/RMS or silence: "+e.id);
        require(m.step<=(track?.075:.045) && m.seam<=(track?.035:.001) && m.dc<.002, "Transient, seam or DC limit failed: "+e.id);
        require(!track || m.quietSeconds<=2, "Long dropout in music: "+e.id);
        require(hashes.insert(m.hash).second, "Duplicate rendered PCM: "+e.id);
        const auto bytes=fs::file_size(asset); total+=bytes;
        if (!track) { if (channels==1) ++mono; else ++stereo; }
        std::cout << (track?"music":"sfx") << '\t' << e.id << '\t' << m.channels << '\t'
            << std::fixed << std::setprecision(6) << m.seconds << '\t' << bytes << '\t' << m.peak << '\t'
            << m.rms << '\t' << m.step << '\t' << m.seam << '\t' << m.dc << '\t'
            << std::hex << m.hash << std::dec << '\n';
    };
    for (const auto& e:catalog.sounds) checked(e,false);
    for (const auto& e:catalog.tracks) checked(e,true);
    require(findId(sounds,"soft-confirm").channels==1 && findId(sounds,"critical").channels==2, "Required lifecycle formats changed");
    require(mono>0 && stereo>0 && hashes.size()==30, "Incomplete format/uniqueness coverage");
    std::cout << "PASS: 30 unique PCM assets; 10 original songs; "
        << mono << " mono and " << stereo << " stereo cues; " << total << " asset bytes\n";
}

int main(int argc,char** argv) {
    try {
        require(argc>=3, "Usage: compose plan ROOT | render ROOT sfx|music ID OUTPUT.wav | audit ROOT STAGE");
        const auto catalog=CatalogReader(fs::path(argv[2])/"catalog.json").read();
        const auto music=songs(); const auto sounds=cues();
        validate(catalog,music,sounds);
        const std::string mode=argv[1];
        if (mode=="plan" && argc==3) {
            for (const auto& e:catalog.sounds) std::cout << "sfx\t" << e.id << '\n';
            for (const auto& e:catalog.tracks) std::cout << "music\t" << e.id << '\n';
        } else if (mode=="render" && argc==6) {
            const std::string kind=argv[3],id=argv[4];
            require(kind=="sfx" || kind=="music", "Unknown render category");
            writeWave(argv[5],kind=="music"?(id=="forge"?renderForge():render(findId(music,id)))
                :render(findId(sounds,id)));
            std::cout << "Rendered " << kind << '/' << id << '\n';
        } else if (mode=="audit" && argc==4) audit(argv[3],catalog,music,sounds);
        else throw std::runtime_error("Invalid composer arguments");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Audio generation failed: " << error.what() << '\n'; return 1;
    }
}
