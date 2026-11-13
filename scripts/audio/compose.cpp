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
        require(out.sounds.size() == 20 && out.tracks.size() == 20, "Catalog must contain 20 sounds and 20 tracks");
        return out;
    }
};

enum class Tone { Felt, Pluck, Reed, Glass, Hollow, Warm, Sub, Veil, Halo, Metal, Kick, Rim, Hush, Drive, Brass, Snare, Tom };
struct Note { double at; int pitch; double hold; double level = 1; };
struct Step { double at; int tone; double hold; double level = 1; };
struct Drum { double at; Tone tone; double level; };
using Chord = std::array<int, 4>;
enum Layer { Bed=1, Ground=2, Motion=4, Theme=8, Answer=16, Beat=32 };
constexpr int full = Bed|Ground|Motion|Theme|Answer|Beat;
struct Section { int bars, layers, variant; double energy; int harmonyOffset = 0; };
struct Score {
    std::string id;
    int bpm, meter, chordBars;
    Tone lead, motion, pad;
    std::vector<Chord> harmony = {};
    std::vector<Note> a = {}, b = {}, counter = {};
    std::vector<Step> bass = {}, pulse = {};
    std::vector<Drum> drums = {};
    std::vector<Section> form = {};
    int kit = 0;
    int bars() const { int n=0; for (const auto& s:form) n+=s.bars; return n; }
    double seconds() const { return bars()*meter*60.0/bpm; }
};

// Each entry is an independent score. Note positions and holds are in beats.
// Harmony uses absolute MIDI pitches. Rhythm cells span two bars, themes four.
// Deliberate rests and different section orders are part of each composition.
std::vector<Score> scores() {
    using T = Tone;
    std::vector<Score> out;
    {
        // D minor: an upward fifth answered by a falling, offbeat second.
        Score s{"quiet-perimeter",104,4,2,T::Reed,T::Pluck,T::Veil};
        s.harmony={{50,53,57,64},{46,53,57,60},{48,55,58,62},{45,52,55,61}};
        s.a={{0,62,1.25},{1.75,69,.5},{2.5,67,.75},{4,65,1.5},{6.25,64,.5},{7,62,.75},{8.5,72,1},{10,70,.75},{11.25,69,.5},{12.5,65,1.5},{15,64,.5}};
        s.b={{.5,69,.75},{1.75,72,.75},{3,74,1},{5,70,1.5},{7.25,69,.5},{8,67,1.5},{10.5,64,.75},{12,65,1},{13.5,62,1.75}};
        s.counter={{3.5,77,.5},{6,76,.75},{10.75,74,.5},{14,69,1}};
        s.bass={{0,0,1.25},{1.75,2,.5},{3,0,.75},{4.5,0,1.5},{6.5,3,.5}};
        s.pulse={{.5,1,.4},{2.25,2,.3},{3.5,3,.4},{5,2,.5},{6.75,1,.3}};
        s.drums={{0,T::Kick,.8},{1.5,T::Hush,.3},{2,T::Rim,.42},{3.5,T::Kick,.4},{4,T::Kick,.7},{5.5,T::Hush,.25},{6,T::Rim,.38},{7.25,T::Metal,.16}};
        s.form={{8,Bed|Motion,0,.65},{8,Bed|Ground|Theme|Beat,0,.86},{8,full,0,1},{4,Bed|Answer,0,.62},{8,full,1,.93},{8,full,0,.88},{4,Bed|Ground,0,.62}};
        out.push_back(s);
    }
    {
        // A minor, spacious broken sixths with a delayed high response.
        Score s{"copper-horizon",96,4,2,T::Felt,T::Hollow,T::Halo};
        s.harmony={{45,52,55,59},{41,48,52,57},{48,52,55,62},{43,50,53,57},{40,47,52,56},{45,52,59,60}};
        s.a={{.75,64,1},{2,72,1.5},{4.5,71,.5},{5.5,67,1},{7.25,64,.5},{8.75,62,1.5},{11,60,.75},{12.25,64,1.75},{15,67,.5}};
        s.b={{0,69,2},{2.75,67,.75},{4,64,1.25},{6,62,.5},{7,59,.75},{9,60,1.5},{11.25,64,.5},{12.5,67,1},{14,69,1.5}};
        s.counter={{1,76,.75},{6.5,74,.5},{10,71,.75},{14.5,72,.5}};
        s.bass={{0,0,2},{2.75,2,.65},{4,0,1.5},{6.25,1,.65},{7.25,2,.5}};
        s.pulse={{1.25,1,.7},{3,3,.6},{4.75,2,.7},{6,1,.5},{7.5,3,.3}};
        s.drums={{0,T::Kick,.65},{1.75,T::Metal,.16},{3,T::Rim,.3},{4.5,T::Kick,.5},{6,T::Hush,.3},{7,T::Rim,.26}};
        s.form={{4,Bed,0,.6},{8,Bed|Ground|Motion|Beat,0,.78},{12,full,0,.94},{8,Bed|Ground|Theme,1,.76},{8,full,1,.94},{4,Bed|Motion|Answer,0,.72},{4,Bed,0,.58}};
        s.kit=1; out.push_back(s);
    }
    {
        // F-sharp minor, five beats grouped 3+2. Angular theme, soft metal reply.
        Score s{"signal-weave",112,5,1,T::Hollow,T::Metal,T::Veil};
        s.harmony={{42,49,54,57},{40,47,52,56},{38,45,49,54},{37,44,49,56},{45,52,56,61}};
        s.a={{0,66,.75},{1,69,.5},{2,73,1.25},{3.75,71,.5},{5.5,68,1},{7,66,.5},{8.5,64,1},{10,61,1.5},{12.25,66,.75},{14,68,.5},{15.5,69,1.25},{18,66,1.25}};
        s.b={{.5,73,1},{2,76,.5},{3,73,.75},{5,71,1.5},{7.5,68,.75},{9,64,.5},{10.5,66,2},{13.25,69,.75},{15,68,1},{17,66,2}};
        s.counter={{4,78,.5},{8,76,.75},{13.5,73,.5},{18.75,71,.5}};
        s.bass={{0,0,1},{2.5,2,.6},{3.5,0,1},{5,0,1.5},{7,3,.6},{8.5,2,.8}};
        s.pulse={{.75,1,.3},{2,3,.4},{4,2,.35},{5.75,3,.4},{7.5,1,.3},{9.25,2,.3}};
        s.drums={{0,T::Kick,.7},{1.5,T::Hush,.3},{3,T::Rim,.4},{4.25,T::Metal,.22},{5,T::Kick,.6},{7.5,T::Kick,.35},{8,T::Rim,.38},{9.5,T::Hush,.28}};
        s.form={{4,Bed|Motion,0,.63},{8,Bed|Ground|Theme|Beat,0,.85},{8,full,0,.98},{4,Bed|Answer,0,.62},{8,full,1,1},{4,Bed|Ground|Theme,0,.77},{4,Bed|Motion,0,.6}};
        s.kit=2; out.push_back(s);
    }
    {
        // E minor, low-register logistics pulse and long, separated phrases.
        Score s{"night-logistics",90,4,2,T::Warm,T::Reed,T::Veil};
        s.harmony={{40,47,54,55},{36,43,50,52},{43,50,54,59},{38,45,52,54},{47,54,57,62},{40,47,52,55}};
        s.a={{0,59,2.5},{3.25,62,.75},{5,64,1.5},{7.5,62,.5},{9,57,2},{12,59,1.25},{14.5,55,1}};
        s.b={{1,64,1.75},{3.5,67,1},{5.25,66,.75},{7,62,1.5},{10,59,2},{13,57,.75},{14.25,59,1.25}};
        s.counter={{2,71,1},{6.5,69,.5},{11,67,1.25}};
        s.bass={{0,0,2.75},{3.5,2,.4},{4.75,0,2},{7,1,.5}};
        s.pulse={{.5,2,.6},{2.75,1,.5},{5.5,3,.65},{7.5,2,.3}};
        s.drums={{0,T::Kick,.6},{2.75,T::Rim,.25},{4.75,T::Kick,.5},{6,T::Hush,.25},{7.5,T::Metal,.12}};
        s.form={{8,Bed|Ground,0,.62},{8,Bed|Ground|Motion|Beat,0,.8},{8,full,0,.9},{8,Bed|Theme|Answer,1,.7},{8,full,1,.93},{8,Bed|Ground|Motion,0,.64}};
        s.kit=3; out.push_back(s);
    }
    {
        // C suspended-major waltz. Bell fragments interlock rather than march.
        Score s{"glass-relay",100,3,2,T::Glass,T::Felt,T::Halo};
        s.harmony={{48,55,62,64},{53,60,64,69},{50,57,60,65},{43,50,55,62}};
        s.a={{.25,76,.5},{1.5,72,.75},{3.25,67,.5},{4.5,74,1},{6.5,71,.5},{7.25,67,.75},{9,69,1},{10.5,72,.75}};
        s.b={{0,79,.75},{1.25,76,.5},{2.25,74,.5},{3.5,72,1},{5,69,.75},{6.75,65,1},{8.25,67,.5},{9.5,71,1.5}};
        s.counter={{2,64,.5},{5.25,62,.5},{8,60,.75},{11,67,.5}};
        s.bass={{0,0,1.4},{2,2,.6},{3.5,0,1.2},{5,3,.6}};
        s.pulse={{.75,1,.5},{2.5,3,.3},{3.75,2,.4},{5.5,1,.25}};
        s.drums={{0,T::Kick,.45},{1.5,T::Hush,.22},{2.5,T::Metal,.16},{3.5,T::Kick,.4},{5,T::Rim,.25}};
        s.form={{8,Bed|Motion,0,.65},{8,Bed|Ground|Theme,0,.85},{16,full,0,.96},{8,Bed|Answer|Beat,0,.67},{8,full,1,.94},{8,Bed|Theme|Motion,1,.78},{8,Bed|Ground,0,.6}};
        out.push_back(s);
    }
    {
        // B minor, compound six-beat breathing with a wide descending answer.
        Score s{"low-orbit",84,6,2,T::Reed,T::Hollow,T::Halo};
        s.harmony={{47,54,57,61},{43,50,54,59},{38,45,49,56},{45,52,55,59},{42,49,52,58},{47,54,59,62}};
        s.a={{1,66,2},{4,62,1},{6.5,61,1.5},{9,59,2},{12,54,2.5},{15.5,57,1},{18,61,1.5},{21,59,2}};
        s.b={{0,71,2.5},{3.5,69,1},{6,66,2},{9.5,62,1},{12.5,64,2},{16,61,1},{18.5,57,2},{22,59,1.5}};
        s.counter={{5,74,1},{10.75,73,.75},{17,71,1.25},{23,66,.5}};
        s.bass={{0,0,2.5},{3.5,2,1},{6,0,2},{9,3,1},{11,2,.5}};
        s.pulse={{1.5,1,.7},{4.5,3,.7},{7.5,2,.75},{10,1,.6}};
        s.drums={{0,T::Kick,.52},{3,T::Rim,.3},{5,T::Hush,.2},{6,T::Kick,.5},{9.5,T::Rim,.25},{11,T::Metal,.15}};
        s.form={{4,Bed,0,.57},{4,Bed|Ground|Motion,0,.75},{8,full,0,.92},{4,Bed|Theme,1,.7},{8,full,1,.94},{4,Bed|Motion|Answer,0,.6}};
        s.kit=1; out.push_back(s);
    }
    {
        // D Dorian, clipped anticipations and a brighter, sustained middle.
        Score s{"amber-vector",118,4,1,T::Brass,T::Pluck,T::Veil};
        s.harmony={{50,53,57,59},{43,50,53,59},{48,52,55,62},{45,52,55,62},{47,53,57,62},{50,57,60,64}};
        s.a={{0,65,.5},{.75,69,.5},{1.75,71,.75},{3,67,.5},{4.25,65,.75},{5.5,62,.5},{7,64,.5},{8,69,1},{9.5,72,.5},{10.25,71,.75},{12,67,1},{13.5,65,.5},{14.75,62,.75}};
        s.b={{.25,74,1.25},{2,72,.75},{3.25,69,.5},{4.5,71,1.5},{6.5,67,.75},{8,65,2},{10.75,64,.75},{12.25,62,1},{14,65,1.5}};
        s.counter={{2.5,77,.5},{6,76,.75},{11.25,74,.5},{15,71,.5}};
        s.bass={{0,0,.75},{1.5,2,.5},{2.75,0,.75},{4,0,.75},{5.25,1,.5},{6.5,2,.75},{7.5,3,.3}};
        s.pulse={{.5,1,.25},{2,3,.3},{3.5,2,.25},{4.75,1,.3},{6,3,.3},{7.25,2,.25}};
        s.drums={{0,T::Kick,.9},{.75,T::Hush,.38},{1.5,T::Kick,.42},{2,T::Snare,.65},{2.75,T::Hush,.28},{3.5,T::Kick,.51},{4.75,T::Kick,.65},{5.5,T::Hush,.36},{6,T::Snare,.72},{7.25,T::Tom,.38}};
        s.form={{4,Motion|Beat,0,.74},{8,Ground|Theme|Beat,0,.94},{8,full,0,1},{8,Bed|Answer,0,.54},{8,Ground|Motion|Theme|Beat,1,1},{8,full,0,.92},{4,Motion|Beat,0,.66}};
        s.kit=2; out.push_back(s);
    }
    {
        // C minor, seven beats grouped 2+2+3; droplets answer low reed calls.
        Score s{"iron-rain",108,7,1,T::Drive,T::Metal,T::Veil};
        s.harmony={{48,55,58,62},{44,51,55,60},{41,48,51,58},{43,50,53,59},{46,53,56,60},{48,51,58,63},{39,46,50,55}};
        s.a={{0,60,1},{2.5,63,.75},{4.25,67,1.5},{7.5,65,1},{10,62,1},{12,60,.75},{14.75,58,1.25},{17,62,.75},{19.5,63,1.5},{22,67,1},{24.5,65,.75},{26,60,1.25}};
        s.b={{1,72,1.5},{3.5,70,1},{6,67,.75},{8,63,2},{11,65,1},{14,62,1.5},{16.5,58,1},{19,60,2},{22.5,63,1},{25,62,1.5}};
        s.counter={{3.5,79,.5},{9,77,.6},{15.5,74,.5},{21,75,.75},{27,72,.5}};
        s.bass={{0,0,1.5},{2.25,2,1},{4.5,0,1.5},{7,0,1.5},{9.5,3,1},{12,2,1}};
        s.pulse={{1,1,.4},{3.5,3,.35},{5.75,2,.5},{8.25,1,.35},{10.75,3,.4},{13,2,.4}};
        s.drums={{0,T::Kick,.9},{1.5,T::Metal,.32},{2,T::Snare,.62},{3,T::Kick,.58},{4.5,T::Tom,.5},{5.5,T::Kick,.53},{6,T::Snare,.57},{7,T::Kick,.78},{8.5,T::Tom,.55},{9,T::Snare,.62},{10.5,T::Metal,.28},{11,T::Kick,.51},{12.5,T::Snare,.7},{13.5,T::Tom,.44}};
        s.form={{4,Ground|Beat,0,.72},{8,Ground|Theme|Beat,0,1},{4,full,0,.96},{4,Bed|Motion|Answer,0,.55},{8,Ground|Theme|Beat,1,1},{4,full,1,.91},{4,Ground|Beat,0,.61}};
        s.kit=3; out.push_back(s);
    }
    {
        // F Lydian, sparse high-fourth melody over slow, nonfunctional harmony.
        Score s{"silent-cartography",80,4,2,T::Felt,T::Glass,T::Halo};
        s.harmony={{41,48,52,59},{45,52,55,60},{43,50,54,57},{48,55,59,64},{40,47,52,57}};
        s.a={{.5,69,2},{3.25,71,1},{5.5,76,2},{8.5,72,1.5},{11,67,1},{13,69,2}};
        s.b={{0,77,2.5},{3.5,76,1},{5.25,71,1.75},{8,69,2},{11.25,67,.75},{13,64,2}};
        s.counter={{2,60,1.5},{7,62,1},{10,64,1.25},{15,59,.5}};
        s.bass={{0,0,3},{4.5,2,2.5},{7.5,0,.4}};
        s.pulse={{1.75,3,.8},{3.5,1,.7},{6.25,2,1}};
        s.drums={{0,T::Kick,.4},{3.5,T::Metal,.12},{5,T::Hush,.18},{7,T::Rim,.2}};
        s.form={{4,Bed,0,.58},{8,Bed|Motion|Theme,0,.77},{8,full,0,.87},{4,Bed|Answer,0,.6},{8,Bed|Ground|Theme|Beat,1,.85},{4,full,1,.88},{4,Bed,0,.55}};
        out.push_back(s);
    }
    {
        // G minor, rolling triplet undercurrent against an uneven long melody.
        Score s{"clockwork-tide",110,4,2,T::Hollow,T::Pluck,T::Veil};
        s.harmony={{43,50,53,57},{46,53,57,60},{41,48,53,55},{48,55,58,62},{38,45,50,54},{43,50,58,62}};
        s.a={{0,67,1.5},{2,65,.75},{3.25,62,.5},{4.5,58,1.25},{6.25,62,.75},{8,65,2},{10.75,69,.5},{12,70,1.25},{14,67,1.5}};
        s.b={{.5,74,.75},{1.75,70,1.25},{3.5,69,.75},{5,65,1},{7,62,.5},{8.5,63,1.5},{11,65,.75},{12.5,69,1},{14.5,67,1}};
        s.counter={{1,79,.5},{5.75,77,.5},{9.75,74,.75},{13.75,72,.5}};
        s.bass={{0,0,1},{1.6667,2,.55},{3.3333,0,.5},{4,0,1.25},{6,3,.6},{7.3333,2,.4}};
        s.pulse={{.6667,1,.3},{2,2,.3},{2.6667,3,.3},{4.6667,2,.3},{5.3333,1,.3},{6.6667,3,.3}};
        s.drums={{0,T::Kick,.62},{1.3333,T::Hush,.2},{2,T::Rim,.35},{3.6667,T::Metal,.14},{4,T::Kick,.6},{5.3333,T::Kick,.3},{6,T::Rim,.35},{7.3333,T::Hush,.2}};
        s.form={{8,Bed|Ground|Motion,0,.68},{8,Bed|Ground|Theme|Beat,0,.85},{8,full,0,.97},{4,Bed|Motion|Answer,0,.64},{8,full,1,.96},{8,Bed|Ground|Theme,1,.78},{8,full,0,.9},{4,Bed|Ground,0,.58}};
        s.kit=1; out.push_back(s);
    }
    {
        // E-flat major, a gentle three-beat memory with bass on the second bar.
        Score s{"field-memory",92,3,2,T::Warm,T::Felt,T::Halo};
        s.harmony={{39,46,50,53},{48,55,58,62},{44,51,55,58},{46,53,56,60},{41,48,51,55},{43,50,53,58}};
        s.a={{0,67,1.75},{2.25,65,.5},{3.5,63,1},{5,70,.75},{6.25,68,1.25},{8.25,67,.75},{9.5,62,1.75}};
        s.b={{.5,75,1},{2,74,.75},{3.25,70,1.25},{5.25,67,.5},{6.5,65,1.5},{9,63,2}};
        s.counter={{1.5,58,.75},{4.5,60,.75},{8,55,.75},{11.25,58,.5}};
        s.bass={{.5,0,1.75},{3,0,1},{4.5,2,.8}};
        s.pulse={{0,1,.5},{1.75,3,.5},{3.75,2,.5},{5.25,1,.4}};
        s.drums={{.5,T::Kick,.43},{2.25,T::Hush,.17},{3,T::Kick,.4},{4,T::Metal,.12},{5,T::Rim,.24}};
        s.form={{8,Bed,0,.57},{8,Bed|Ground|Theme,0,.78},{8,full,0,.91},{8,Bed|Ground|Motion|Beat,0,.73},{8,Bed|Theme|Answer,1,.75},{16,full,1,.91},{8,Bed|Motion,0,.58}};
        s.kit=2; out.push_back(s);
    }
    {
        // B Phrygian, five-beat 2+3 cells and downward semitone questions.
        Score s{"mesh-transit",120,5,1,T::Pluck,T::Brass,T::Veil};
        s.harmony={{47,54,57,60},{48,55,59,64},{45,52,55,59},{43,50,54,57},{40,47,52,55}};
        s.a={{.25,72,.5},{1.25,71,.75},{3,66,1},{5,64,.5},{6,60,1.25},{8.25,59,.75},{10.5,64,1},{12.25,66,.5},{14,69,1.25},{16,67,.75},{18,64,1.5}};
        s.b={{0,71,1},{1.75,74,.5},{3.5,72,1},{5.5,67,.75},{7,66,1},{9,64,.5},{10,60,1.5},{12.5,59,.75},{14.25,62,1},{16.25,64,1.75},{19,59,.5}};
        s.counter={{2.5,78,.5},{7.75,76,.5},{13.5,79,.75},{18.75,76,.5}};
        s.bass={{0,0,.7},{1.5,2,.5},{3,0,1},{5,0,.7},{6.5,1,.5},{8,3,1}};
        s.pulse={{.5,1,.25},{2.25,3,.3},{4.25,2,.3},{5.75,2,.25},{7.25,1,.3},{9,3,.4}};
        s.drums={{0,T::Kick,.82},{1,T::Hush,.34},{1.75,T::Snare,.56},{2.5,T::Kick,.47},{3.75,T::Snare,.44},{4.5,T::Hush,.36},{5,T::Kick,.81},{6.5,T::Tom,.45},{7,T::Snare,.62},{8.25,T::Kick,.54},{9,T::Snare,.61},{9.75,T::Hush,.38}};
        s.form={{4,Bed|Motion|Beat,0,.66},{8,Bed|Ground|Theme|Beat,0,.86},{8,full,0,.98},{4,Bed|Ground|Answer,0,.67},{8,full,1,.97},{8,full,0,.9},{4,Bed|Motion,0,.6}};
        s.kit=3; out.push_back(s);
    }
    {
        // C-sharp minor, a slow pedal with staggered metal notes, not a drone.
        Score s{"distant-machinery",86,4,2,T::Reed,T::Metal,T::Halo};
        s.harmony={{37,44,51,52},{45,52,56,61},{42,49,52,56},{40,47,51,56},{44,51,54,60},{37,44,49,52}};
        s.a={{1.25,61,1.5},{4,64,2.25},{7,63,.75},{8.75,56,1.5},{11,59,1},{13.25,61,2}};
        s.b={{0,68,2},{3,66,1.25},{5.5,64,1},{7.25,61,.75},{9,63,2},{12,59,1},{14,56,1.5}};
        s.counter={{2,73,1},{6.5,71,.75},{10.25,68,1},{15,64,.5}};
        s.bass={{0,0,3.25},{4,0,2.5},{7,2,.65}};
        s.pulse={{.75,2,.65},{3.25,3,.5},{5.75,1,.7},{7.5,2,.3}};
        s.drums={{0,T::Tom,.58},{1.25,T::Metal,.21},{2.75,T::Tom,.32},{3.75,T::Hush,.19},{4.5,T::Tom,.53},{6.25,T::Metal,.32},{7.5,T::Rim,.26}};
        s.form={{8,Bed|Motion,0,.59},{8,Bed|Ground|Theme,0,.78},{8,full,0,.9},{4,Bed|Answer,0,.6},{8,Bed|Ground|Theme|Beat,1,.83},{8,full,1,.92},{4,Bed,0,.55}};
        s.kit=1; out.push_back(s);
    }
    {
        // A Mixolydian, six-beat interleaved steps and a broad major answer.
        Score s{"northern-array",106,6,1,T::Brass,T::Reed,T::Veil};
        s.harmony={{45,52,55,61},{38,45,49,56},{43,50,54,57},{47,54,57,61},{40,47,52,56},{45,52,59,61}};
        s.a={{0,69,1.5},{2,73,.75},{3.75,76,1},{6.25,74,1.5},{8.5,71,.75},{10,67,1},{12.25,64,1.5},{14.5,67,.75},{16,69,1.25},{18.5,73,1},{21,71,1.5}};
        s.b={{.75,76,1},{2.25,79,.75},{4,76,1},{6,73,2},{9,71,1},{11,69,.5},{12.75,67,1.5},{15.25,64,.75},{17,66,1},{19,67,1.5},{22,69,1}};
        s.counter={{1,57,1},{5,59,.75},{9.75,61,.75},{16.75,62,.75},{23,64,.5}};
        s.bass={{0,0,1.5},{2.75,2,.8},{4.5,0,1},{6,0,1.5},{8.5,3,.75},{10.25,2,1}};
        s.pulse={{1,1,.4},{3.5,3,.4},{5.5,2,.3},{7.5,1,.5},{9.5,2,.3},{11.5,3,.3}};
        s.drums={{0,T::Tom,.62},{1,T::Tom,.37},{2,T::Kick,.61},{3,T::Snare,.46},{4.5,T::Metal,.21},{6,T::Kick,.68},{7.5,T::Tom,.59},{8.5,T::Tom,.38},{9,T::Snare,.46},{11,T::Hush,.27}};
        s.form={{4,Bed|Ground,0,.62},{8,Bed|Ground|Theme|Beat,0,.83},{8,full,0,.95},{4,Bed|Motion|Answer,0,.65},{8,full,1,.96},{4,Bed|Motion,0,.58}};
        s.kit=2; out.push_back(s);
    }
    {
        // G Dorian, repeated-note opening develops into a stepwise answer.
        Score s{"steady-watch",98,4,2,T::Felt,T::Reed,T::Veil};
        s.harmony={{43,50,53,57},{48,55,58,64},{46,53,57,60},{41,48,52,57},{45,52,55,60},{43,50,57,58}};
        s.a={{0,62,.75},{1.25,62,.5},{2.5,65,1},{4.25,67,1.25},{6.5,69,.75},{8,67,1},{9.75,65,.75},{11.25,64,.5},{12.5,62,1.75},{15,57,.5}};
        s.b={{.5,70,1.25},{2.25,69,.75},{4,67,1},{5.75,65,.75},{7.25,64,.5},{8.5,62,1},{10.25,60,.75},{12,57,1.5},{14.5,62,1}};
        s.counter={{3.5,74,.5},{7,76,.5},{10.75,77,.5},{14,74,.5}};
        s.bass={{0,0,1.75},{2.5,2,.75},{4.25,0,1.5},{6.75,1,.7}};
        s.pulse={{.75,3,.35},{2,1,.45},{3.75,2,.3},{5.5,3,.4},{7.25,1,.35}};
        s.drums={{0,T::Kick,.55},{1.75,T::Hush,.2},{2.5,T::Rim,.3},{4.25,T::Kick,.5},{6,T::Rim,.3},{7.25,T::Metal,.13}};
        s.form={{8,Bed|Motion,0,.62},{8,Bed|Ground|Theme,0,.82},{8,full,0,.92},{8,Bed|Ground|Answer|Beat,0,.72},{8,full,1,.94},{4,Bed|Theme,0,.72},{4,Bed,0,.56}};
        s.kit=3; out.push_back(s);
    }
    {
        // F minor, tight offbeat low pulses with a spacious upper-register arc.
        Score s{"basalt-channel",116,4,1,T::Warm,T::Drive,T::Halo};
        s.harmony={{41,48,51,55},{37,44,48,53},{44,51,55,58},{39,46,49,56},{46,53,56,60},{48,55,58,64}};
        s.a={{.5,68,.75},{1.75,72,.5},{3,75,1.5},{5.25,73,.75},{6.75,70,.5},{8.25,67,1.25},{10,65,.75},{11.5,68,1},{13.25,70,.75},{14.75,65,.75}};
        s.b={{0,77,1.5},{2.25,75,.75},{3.75,72,1},{5.5,68,1.25},{7.5,67,.5},{9,65,1.5},{11.25,63,.75},{13,60,1},{14.5,65,1}};
        s.counter={{2.75,80,.5},{6,79,.5},{10.75,77,.5},{15.25,75,.5}};
        s.bass={{.25,0,.75},{1.75,2,.5},{3.25,0,.5},{4.5,0,.75},{6,1,.5},{7.25,3,.5}};
        s.pulse={{0,1,.25},{1,2,.25},{2.5,3,.3},{4,2,.25},{5.25,1,.3},{6.75,3,.3}};
        s.drums={{0,T::Kick,.95},{1.5,T::Hush,.34},{2,T::Snare,.75},{3.25,T::Kick,.6},{4,T::Kick,.7},{5,T::Tom,.44},{6,T::Snare,.8},{7.5,T::Tom,.42}};
        s.form={{4,Ground|Motion,0,.7},{8,Ground|Motion|Beat,0,.91},{8,full,0,1},{4,Ground|Beat,0,.68},{8,Theme|Ground|Motion|Beat,1,1},{8,full,1,.93},{4,Bed|Ground,0,.54}};
        out.push_back(s);
    }
    {
        // E minor, seven beats grouped 3+2+2. A short call and displaced answer.
        Score s{"silver-switchyard",102,7,1,T::Glass,T::Reed,T::Veil};
        s.harmony={{40,47,55,59},{43,50,54,59},{45,52,55,60},{38,45,52,54},{36,43,50,55},{47,54,57,63},{40,47,54,55}};
        s.a={{.5,71,1},{2,67,.75},{4,64,1.5},{7,66,1},{9.25,69,.75},{11,71,1},{14,74,1.75},{16.5,71,.75},{18.25,67,1},{21.5,66,1.25},{24,64,2}};
        s.b={{0,76,1.25},{2.5,74,1},{5,71,.75},{7.5,69,1.25},{10,67,1},{12,64,1},{15,62,1.5},{18,66,1},{20.5,67,1.25},{23,71,1},{25.5,64,1.5}};
        s.counter={{3,59,1},{8.5,62,.75},{13,64,.75},{19.5,62,.5},{26.5,59,.75}};
        s.bass={{0,0,1.5},{3,2,1},{5,0,1},{7.25,0,1.5},{10,1,1},{12,2,1}};
        s.pulse={{1.5,3,.5},{4,1,.5},{6.25,2,.4},{8.75,1,.4},{11,3,.5},{13.25,2,.4}};
        s.drums={{0,T::Kick,.53},{2,T::Hush,.18},{3,T::Rim,.28},{5,T::Kick,.3},{7.25,T::Kick,.5},{10,T::Metal,.14},{12,T::Rim,.28},{13.5,T::Hush,.18}};
        s.form={{4,Bed|Motion,0,.61},{4,Bed|Ground|Theme,0,.8},{4,full,0,.92},{4,Bed|Ground|Answer|Beat,0,.69},{4,full,1,.93},{4,Bed|Theme|Motion,1,.77},{4,Bed,0,.55}};
        s.kit=1; out.push_back(s);
    }
    {
        // B-flat major, broad sustained melody and a low, delayed counterline.
        Score s{"blue-foundation",88,4,2,T::Reed,T::Felt,T::Halo};
        s.harmony={{46,53,57,60},{43,50,53,58},{39,46,50,55},{41,48,53,57},{48,55,58,62},{46,53,60,62}};
        s.a={{0,65,2.25},{3,62,1},{5,60,1.75},{8,58,2},{10.75,62,.75},{12.5,65,2},{15.25,67,.5}};
        s.b={{1,70,1.5},{3.5,69,1},{5.5,65,1.5},{8.5,62,1.25},{10.5,60,1},{12.25,58,2},{15,62,.5}};
        s.counter={{2,53,1.25},{6.75,55,.75},{11.5,57,.75},{14.5,53,.5}};
        s.bass={{0,0,2.5},{3.25,2,.5},{4.75,0,2},{7.25,3,.5}};
        s.pulse={{1,1,.75},{3,3,.65},{5.75,2,.6},{7.5,1,.3}};
        s.drums={{0,T::Kick,.45},{2.25,T::Rim,.24},{4.75,T::Kick,.42},{6.5,T::Hush,.18},{7.5,T::Metal,.11}};
        s.form={{8,Bed,0,.57},{8,Bed|Ground|Theme,0,.78},{8,full,0,.9},{4,Bed|Motion|Answer,0,.61},{8,full,1,.91},{8,Bed|Ground|Theme|Beat,0,.8},{4,Bed,0,.55}};
        s.kit=2; out.push_back(s);
    }
    {
        // C Lydian, three-beat syncopation and upward fourths, lightly articulated.
        Score s{"radar-garden",114,3,1,T::Pluck,T::Glass,T::Veil};
        s.harmony={{48,55,59,64},{50,57,60,66},{43,50,54,59},{45,52,55,60},{41,48,52,59},{48,55,62,66}};
        s.a={{.5,64,.5},{1.25,69,.75},{3,71,.5},{4,74,1},{5.5,72,.5},{6.5,66,.75},{8,67,.5},{9.25,71,1},{11,64,.75}};
        s.b={{0,76,.5},{1,74,.75},{2.25,71,.5},{3.5,69,1},{5.25,66,.5},{6.25,67,1.25},{8.25,64,.75},{9.5,62,.75},{11,64,.75}};
        s.counter={{2,79,.5},{4.75,78,.5},{7.75,76,.5},{10.75,74,.5}};
        s.bass={{0,0,.8},{1.75,2,.6},{3.25,0,1},{5,1,.5}};
        s.pulse={{.75,1,.3},{2.5,3,.25},{4,2,.35},{5.5,1,.25}};
        s.drums={{0,T::Kick,.5},{1.25,T::Hush,.16},{2,T::Rim,.28},{3.25,T::Kick,.46},{4.5,T::Metal,.13},{5.5,T::Hush,.18}};
        s.form={{8,Bed|Motion,0,.62},{8,Bed|Ground|Theme|Beat,0,.82},{16,full,0,.94},{8,Bed|Ground|Answer,0,.67},{8,full,1,.94},{8,Bed|Theme|Beat,0,.79},{8,Bed|Motion,0,.57}};
        s.kit=3; out.push_back(s);
    }
    {
        // D major with a borrowed flat seventh, an unhurried opening-out phrase.
        Score s{"first-light-protocol",94,4,2,T::Warm,T::Pluck,T::Halo};
        s.harmony={{50,57,61,64},{43,50,54,59},{47,54,57,62},{45,52,55,62},{48,55,59,64},{50,57,62,66}};
        s.a={{.25,62,1.5},{2.5,66,.75},{4,69,2},{7,71,.5},{8.5,74,1.25},{10.5,73,.75},{12,69,1.5},{14.25,66,1.25}};
        s.b={{0,76,1.75},{2.75,74,.75},{4.25,71,1.25},{6.25,69,.75},{8,67,1.5},{10.25,66,.75},{12,64,1},{13.5,62,2}};
        s.counter={{1.25,57,1},{5.5,59,1},{9.75,61,.75},{15,57,.5}};
        s.bass={{0,0,2},{2.75,2,.65},{4.5,0,1.75},{7,3,.6}};
        s.pulse={{1,1,.5},{3.5,3,.4},{5.5,2,.6},{7.5,1,.3}};
        s.drums={{0,T::Kick,.48},{2.5,T::Rim,.27},{4.5,T::Kick,.45},{6,T::Hush,.19},{7.25,T::Metal,.12}};
        s.form={{4,Bed,0,.55},{8,Bed|Ground|Motion,0,.7},{8,Bed|Ground|Theme|Beat,0,.84},{8,full,0,.93},{4,Bed|Theme,1,.67},{8,full,1,.94},{4,Bed|Motion|Answer,0,.67},{4,Bed,0,.54}};
        out.push_back(s);
    }
    return out;
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
    out << std::fixed << std::setprecision(4);
    for (const auto& n:notes) out << n.at << ':' << n.pitch-notes.front().pitch << ':' << n.hold << ';';
    return out.str();
}

void validate(const Catalog& catalog, const std::vector<Score>& music, const std::vector<Cue>& sounds) {
    require(music.size()==20 && sounds.size()==20, "Expected twenty explicit scores and cues");
    std::set<std::string> scoreIds, cueIds, shapes;
    for (const auto& s:music) {
        require(scoreIds.insert(s.id).second, "Duplicate score ID");
        require(s.bpm>=40 && s.bpm<=160 && s.meter>=3 && s.meter<=7 && s.chordBars>0, "Invalid score timing: "+s.id);
        require(s.seconds()>=90 && s.seconds()<=150, "Score duration outside 90-150 seconds: "+s.id);
        require(!s.harmony.empty() && s.a.size()>=6 && s.b.size()>=6 && !s.counter.empty(), "Incomplete composition: "+s.id);
        require(shapes.insert(contour(s.a)).second, "Transposed or tempo-only duplicate opening theme: "+s.id);
        require(contour(s.a)!=contour(s.b), "Answer repeats the opening theme: "+s.id);
        for (const auto* line:{&s.a,&s.b,&s.counter}) for (const auto& n:*line)
            require(n.at>=0 && n.hold>0 && n.at+n.hold<=s.meter*4 && n.pitch>=36 && n.pitch<=88, "Invalid theme event: "+s.id);
        for (const auto* line:{&s.bass,&s.pulse}) for (const auto& n:*line)
            require(n.at>=0 && n.hold>0 && n.at+n.hold<=s.meter*2 && n.tone>=0 && n.tone<4, "Invalid rhythm event: "+s.id);
        for (const auto& d:s.drums) require(d.at>=0 && d.at<s.meter*2 && d.level>0 && d.level<=1, "Invalid percussion event: "+s.id);
        for (const auto& chord:s.harmony) for (const int pitch:chord) require(pitch>=24 && pitch<=80, "Invalid harmony: "+s.id);
        for (const auto& section:s.form) require(section.bars>0 && section.layers>0 && section.layers<=full
            && section.variant>=0 && section.variant<=1 && section.energy>0 && section.energy<=1, "Invalid section: "+s.id);
    }
    for (const auto& cue:sounds) {
        require(cueIds.insert(cue.id).second, "Duplicate cue ID");
        require(cue.seconds>=.1 && cue.seconds<=2 && (cue.channels==1 || cue.channels==2) && !cue.notes.empty(), "Invalid cue: "+cue.id);
        for (const auto& n:cue.notes) require(n.at>=0 && n.hold>0 && n.at+n.hold<=cue.seconds && n.pitch>=36
            && n.pitch<=88 && std::abs(n.pan)<=1 && n.level>0 && n.level<=1, "Invalid cue note: "+cue.id);
    }
    const auto entries = [](const std::vector<Entry>& items, const std::set<std::string>& ids, bool sound) {
        std::set<std::string> found;
        for (const auto& e:items) {
            require(ids.count(e.id)==1 && found.insert(e.id).second, "Catalog/score ID mismatch: "+e.id);
            require(!e.id.empty() && e.id.front()!='-' && e.id.back()!='-', "Invalid asset ID");
            char previous=0;
            for (const char c:e.id) {
                require((c>='a'&&c<='z') || (c>='0'&&c<='9') || (c=='-'&&previous!='-'), "Unsafe asset ID");
                previous=c;
            }
            require(e.asset=="qrc:/podlord/audio/"+std::string(sound?"sfx/":"music/")+e.id+(sound?".wav":".m4a"), "Unexpected asset path: "+e.id);
        }
        require(found==ids, "Catalog omits a composition");
    };
    entries(catalog.sounds,cueIds,true); entries(catalog.tracks,scoreIds,false);
    for (const auto& [old,target]:catalog.legacy) require(!old.empty() && cueIds.count(target)==1, "Invalid legacy sound alias: "+old);
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
    const double attack=std::min(duration*.3,pad?.32:drum?.009:tone==Tone::Sub?.018:.02);
    const double release=std::min(duration*.4,pad?.65:tone==Tone::Sub?.13:.16);
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

Audio render(const Score& score) {
    Audio audio(score.seconds(),2,true);
    const double spb=60.0/score.bpm;
    const auto seed=seedFor(score.id);
    const int character=int(seed%5);
    std::uint32_t event=0;
    int firstBar=0;
    for (const auto& section:score.form) {
        const int lastBar=firstBar+section.bars;
        const double energy=section.energy;
        for (int bar=firstBar;bar<lastBar;++bar) {
            const auto& chord=score.harmony[std::size_t(bar/score.chordBars+section.harmonyOffset)%score.harmony.size()];
            if ((section.layers&Bed) && bar%score.chordBars==0) {
                const int articulations=1+((character+bar/score.chordBars)%4);
                const double span=score.chordBars*score.meter*spb;
                const double hold=span/double(articulations)*.86;
                for (int pulse=0;pulse<articulations;++pulse) for (int c=0;c<4;++c)
                    voice(audio,bar*score.meter*spb+pulse*span/double(articulations),hold,
                        chord[std::size_t(c)]+12,score.pad,.10*energy,c%2? .62:-.62,seed+(++event));
            }
        }
        for (int cell=firstBar;cell<lastBar;cell+=2) {
            const int cycle=(cell-firstBar)/2;
            const double end=lastBar*score.meter;
            const auto stepped=[&](const std::vector<Step>& line,Tone tone,double level,int octave,double pan) {
                for (const auto& n:line) {
                    double beat=cell*score.meter+n.at;
                    if ((character==1 || character==4) && n.at-std::floor(n.at)>.1) beat+=.11;
                    if (character==2 && cycle%4==3 && tone==Tone::Sub && n.at==0) continue;
                    if (beat>=end) continue;
                    const auto bar=int(beat/score.meter);
                    const auto& chord=score.harmony[std::size_t(bar/score.chordBars+section.harmonyOffset)%score.harmony.size()];
                    voice(audio,beat*spb,std::min(n.hold,end-beat)*spb,
                        chord[std::size_t(n.tone)]+octave,tone,level*n.level*energy,pan,seed+(++event));
                }
            };
            if (section.layers&Ground) stepped(score.bass,Tone::Sub,.29,-12,0);
            if (section.layers&Motion) stepped(score.pulse,score.motion,.11,12,-.28);
            if (section.layers&Beat) for (const auto& d:score.drums) {
                double beat=cell*score.meter+d.at;
                if ((character==1 || character==4) && d.at-std::floor(d.at)>.1) beat+=.11;
                if (character==2 && cycle%4==3 && d.tone==Tone::Kick) continue;
                if (beat>=end) continue;
                const bool backbeat=d.tone==Tone::Rim || d.tone==Tone::Snare;
                const int pitch=d.tone==Tone::Kick?32+score.kit:d.tone==Tone::Tom?42+score.kit:backbeat?51+score.kit*2:70+score.kit;
                const double duration=d.tone==Tone::Kick?.24:d.tone==Tone::Tom?.28:backbeat?.16:.12;
                const double level=d.tone==Tone::Kick?.32:d.tone==Tone::Tom?.23:d.tone==Tone::Snare?.25:backbeat?.17:.075;
                voice(audio,beat*spb,duration,pitch,d.tone,level*d.level*energy,
                    d.tone==Tone::Metal?.38:d.tone==Tone::Hush?-.42:0,seed+(++event));
            }
        }
        const auto melodic=[&](const std::vector<Note>& line,Tone tone,double level,double pan) {
            for (int phrase=firstBar;phrase<lastBar;phrase+=4) for (const auto& n:line) {
                const double at=phrase*score.meter+n.at, end=lastBar*score.meter;
                if (at>=end) continue;
                voice(audio,at*spb,std::min(n.hold,end-at)*spb,n.pitch,tone,level*n.level*energy,pan,seed+(++event));
            }
        };
        if (section.layers&Theme) {
            for (int phrase=firstBar;phrase<lastBar;phrase+=4) {
                const int variation=section.variant+((phrase-firstBar)/4)+(character%2);
                const auto& line=variation%2?score.b:score.a;
                for (const auto& n:line) {
                    const double at=phrase*score.meter+n.at, end=lastBar*score.meter;
                    if (at>=end) continue;
                    voice(audio,at*spb,std::min(n.hold,end-at)*spb,n.pitch,score.lead,
                        .19*n.level*energy,.12,seed+(++event));
                }
            }
        }
        if (section.layers&Answer) melodic(score.counter,score.lead==Tone::Glass?Tone::Felt:Tone::Glass,.075,.38);
        firstBar=lastBar;
    }
    finish(audio,true);
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
    double seconds=0, peak=0, rms=0, step=0, seam=0, dc=0;
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
    double energy=0;
    std::array<double,2> sum{},first{},previous{};
    for (std::size_t i=0;i<samples;++i) {
        const auto p=begin+i*2;
        const int integer=int(readLittle(data,p,2));
        const double x=double(integer>=32768?integer-65536:integer)/32768;
        const auto channel=i%std::size_t(metrics.channels);
        if (i<std::size_t(metrics.channels)) first[channel]=x;
        else metrics.step=std::max(metrics.step,std::abs(x-previous[channel]));
        previous[channel]=x; sum[channel]+=x; energy+=x*x;
        metrics.peak=std::max(metrics.peak,std::abs(x));
        metrics.hash=(metrics.hash^data[p])*1099511628211ull;
        metrics.hash=(metrics.hash^data[p+1])*1099511628211ull;
    }
    metrics.seconds=double(frames)/rate;
    metrics.rms=std::sqrt(energy/double(samples));
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

void audit(const fs::path& stage,const Catalog& catalog,const std::vector<Score>& music,const std::vector<Cue>& sounds) {
    std::set<std::uint64_t> hashes;
    std::uintmax_t total=0;
    int mono=0,stereo=0;
    std::cout << "kind\tid\tchannels\tseconds\tbytes\tpeak\trms\tmax_step\tseam\tdc\tpcm_fnv64\n";
    const auto checked=[&](const Entry& e,bool track) {
        const auto pcm=stage/(track?"decoded":"sfx")/(e.id+".wav");
        const auto asset=stage/(track?"music":"sfx")/(e.id+(track?".m4a":".wav"));
        const auto m=inspectWave(pcm);
        const double expected=track?findId(music,e.id).seconds():findId(sounds,e.id).seconds;
        const int channels=track?2:findId(sounds,e.id).channels;
        require(m.channels==channels && std::abs(m.seconds-expected)<.08, "Duration/channel mismatch: "+e.id);
        require(m.peak<=(track?.56:.181) && m.rms>=(track?.02:.008) && m.rms<=(track?.09:.036), "Unsafe peak/RMS or silence: "+e.id);
        require(m.step<=(track?.075:.045) && m.seam<=(track?.035:.001) && m.dc<.002, "Transient, seam or DC limit failed: "+e.id);
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
    require(mono>0 && stereo>0 && hashes.size()==40, "Incomplete format/uniqueness coverage");
    std::cout << "PASS: 40 unique PCM assets; 20 distinct tempo/transposition-independent opening themes; "
        << mono << " mono and " << stereo << " stereo cues; " << total << " asset bytes\n";
}

int main(int argc,char** argv) {
    try {
        require(argc>=3, "Usage: compose plan ROOT | render ROOT sfx|music ID OUTPUT.wav | audit ROOT STAGE");
        const auto catalog=CatalogReader(fs::path(argv[2])/"catalog.json").read();
        const auto music=scores(); const auto sounds=cues();
        validate(catalog,music,sounds);
        const std::string mode=argv[1];
        if (mode=="plan" && argc==3) {
            for (const auto& e:catalog.sounds) std::cout << "sfx\t" << e.id << '\n';
            for (const auto& e:catalog.tracks) std::cout << "music\t" << e.id << '\n';
        } else if (mode=="render" && argc==6) {
            const std::string kind=argv[3],id=argv[4];
            require(kind=="sfx" || kind=="music", "Unknown render category");
            writeWave(argv[5],kind=="music"?render(findId(music,id)):render(findId(sounds,id)));
            std::cout << "Rendered " << kind << '/' << id << '\n';
        } else if (mode=="audit" && argc==4) audit(argv[3],catalog,music,sounds);
        else throw std::runtime_error("Invalid composer arguments");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Audio generation failed: " << error.what() << '\n'; return 1;
    }
}
