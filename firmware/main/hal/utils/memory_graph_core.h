/*
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

/**
 * @brief The robot's long-term memory as a graph: facts are edges "subject | relation | object" between nodes
 * (people, pets, things, topics). No ESP dependencies, so it is tested on the Mac (tests/memory_graph_test.cpp).
 *
 * File format (memoria/grafo.tsv on the card, one fact per line, editable by the parents):
 *   subject<TAB>relation<TAB>object<TAB>date
 * Lines starting with '#' are comments. Edges are fixed-size so a PSRAM allocator keeps them out of internal RAM
 */
namespace memory_graph {

struct Edge {
    char subject[40];
    char relation[40];
    char object[96];
    char date[12];
    uint32_t hash;  // Of the subject, relation and object keys: duplicate checks without rebuilding keys
};

// Copies a field, cut at a character boundary, without tabs or line breaks (they would break the file)
inline void copy_field(char* dst, size_t size, const std::string& src)
{
    size_t n = 0;
    size_t i = 0;
    // Trim leading spaces
    while (i < src.size() && src[i] == ' ') {
        i++;
    }
    for (; i < src.size() && n + 1 < size; i++) {
        char c = src[i];
        if (c == '\t' || c == '\n' || c == '\r') {
            c = ' ';
        }
        dst[n++] = c;
    }
    // Do not leave half a UTF-8 character at the end
    if (i < src.size()) {
        while (n > 0 && ((uint8_t)dst[n - 1] & 0xC0) == 0x80) {
            n--;
        }
        if (n > 0 && ((uint8_t)dst[n - 1] & 0xC0) == 0xC0) {
            n--;
        }
    }
    while (n > 0 && dst[n - 1] == ' ') {
        n--;
    }
    dst[n] = 0;
}

// Comparison key: lowercase, Portuguese accents folded, spaces collapsed ("Lía " == "lia")
inline std::string key(const char* text)
{
    static const struct {
        uint8_t second;
        char ascii;
    } latin1[] = {{0xA0, 'a'}, {0xA1, 'a'}, {0xA2, 'a'}, {0xA3, 'a'}, {0xA4, 'a'}, {0xA7, 'c'}, {0xA8, 'e'},
                  {0xA9, 'e'}, {0xAA, 'e'}, {0xAD, 'i'}, {0xB3, 'o'}, {0xB4, 'o'}, {0xB5, 'o'}, {0xBA, 'u'},
                  {0xBC, 'u'}, {0x80, 'a'}, {0x81, 'a'}, {0x82, 'a'}, {0x83, 'a'}, {0x87, 'c'}, {0x89, 'e'},
                  {0x8A, 'e'}, {0x8D, 'i'}, {0x93, 'o'}, {0x94, 'o'}, {0x95, 'o'}, {0x9A, 'u'}};
    std::string out;
    size_t len = strlen(text);
    for (size_t i = 0; i < len; i++) {
        uint8_t c = text[i];
        if (c >= 'A' && c <= 'Z') {
            out += (char)(c - 'A' + 'a');
        } else if (c == 0xC3 && i + 1 < len) {
            uint8_t next = text[++i];
            char mapped  = 0;
            for (auto& l : latin1) {
                if (l.second == next) {
                    mapped = l.ascii;
                }
            }
            if (mapped) {
                out += mapped;
            }
        } else if (c == ' ' || c == '-' || c == '_') {
            if (!out.empty() && out.back() != ' ') {
                out += ' ';
            }
        } else {
            out += (char)c;
        }
    }
    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    return out;
}

inline uint32_t fact_hash(const char* subject, const char* relation, const char* object)
{
    uint32_t h = 2166136261u;  // FNV-1a
    for (const char* part : {subject, relation, object}) {
        for (char c : key(part)) {
            h = (h ^ (uint8_t)c) * 16777619u;
        }
        h = (h ^ 0x1F) * 16777619u;
    }
    return h;
}

inline Edge make_edge(const std::string& subject, const std::string& relation, const std::string& object,
                      const std::string& date)
{
    Edge e;
    copy_field(e.subject, sizeof(e.subject), subject);
    copy_field(e.relation, sizeof(e.relation), relation);
    copy_field(e.object, sizeof(e.object), object);
    copy_field(e.date, sizeof(e.date), date);
    e.hash = fact_hash(e.subject, e.relation, e.object);
    return e;
}

inline std::string to_line(const Edge& e)
{
    return std::string(e.subject) + "\t" + e.relation + "\t" + e.object + "\t" + e.date + "\n";
}

// False for comments, blank lines and lines without subject, relation and object
inline bool parse_line(const std::string& line, Edge& out)
{
    if (line.empty() || line[0] == '#') {
        return false;
    }
    std::string fields[4];
    int f = 0;
    for (char c : line) {
        if (c == '\t') {
            if (++f > 3) {
                break;
            }
        } else if (c != '\r' && c != '\n') {
            fields[f] += c;
        }
    }
    out = make_edge(fields[0], fields[1], fields[2], fields[3]);
    return out.subject[0] && out.relation[0] && out.object[0];
}

template <class Alloc = std::allocator<Edge>>
class Graph {
public:
    static constexpr size_t kMaxEdges = 3000;

    void clear()
    {
        _edges.clear();
    }

    size_t size() const
    {
        return _edges.size();
    }

    const Edge& at(size_t i) const
    {
        return _edges[i];
    }

    // Index of the same fact (same subject, relation and object keys), or -1
    int find(const Edge& e) const
    {
        std::string s, r, o;
        for (size_t i = 0; i < _edges.size(); i++) {
            if (_edges[i].hash != e.hash) {
                continue;
            }
            if (s.empty()) {
                s = key(e.subject), r = key(e.relation), o = key(e.object);
            }
            if (key(_edges[i].subject) == s && key(_edges[i].relation) == r && key(_edges[i].object) == o) {
                return (int)i;
            }
        }
        return -1;
    }

    // False if it was already known (or the graph is full)
    bool add(const Edge& e)
    {
        if (!e.subject[0] || !e.relation[0] || !e.object[0] || find(e) >= 0 || _edges.size() >= kMaxEdges) {
            return false;
        }
        _edges.push_back(e);
        return true;
    }

    // Removes the fact; with an empty object, every fact of that subject and relation. Returns how many
    int remove(const std::string& subject, const std::string& relation, const std::string& object)
    {
        std::string s = key(subject.c_str()), r = key(relation.c_str()), o = key(object.c_str());
        size_t before = _edges.size();
        _edges.erase(std::remove_if(_edges.begin(), _edges.end(),
                                    [&](const Edge& e) {
                                        return key(e.subject) == s && key(e.relation) == r &&
                                               (o.empty() || key(e.object) == o);
                                    }),
                     _edges.end());
        return (int)(before - _edges.size());
    }

    // Other objects already known for the same subject and relation ("Lia idade 5" when saving "Lia idade 6")
    std::vector<std::string> siblings(const Edge& e) const
    {
        std::vector<std::string> out;
        std::string s = key(e.subject), r = key(e.relation), o = key(e.object);
        for (auto& x : _edges) {
            if (key(x.subject) == s && key(x.relation) == r && key(x.object) != o) {
                out.emplace_back(x.object);
            }
        }
        return out;
    }

    /**
     * @brief Facts about a name or topic and about the nodes one step away from it ("Lia" also brings what is
     * known about her brother Gui). Partial names match ("li" finds "Lia"). Newest facts first, cut at maxChars.
     * An empty query lists the best-known nodes
     */
    std::string recall(const std::string& query, size_t maxChars = 1500) const
    {
        std::string q = key(query.c_str());
        if (q.empty()) {
            return overview(maxChars);
        }

        // 1) Nodes matching the query
        std::vector<std::string> seeds;
        auto add_seed = [&](const char* node) {
            std::string k = key(node);
            if (k.find(q) != std::string::npos && std::find(seeds.begin(), seeds.end(), k) == seeds.end()) {
                seeds.push_back(k);
            }
        };
        for (auto& e : _edges) {
            add_seed(e.subject);
            add_seed(e.object);
        }
        if (seeds.empty()) {
            return "";
        }

        // 2) Direct facts, newest first, and the neighbors they reach
        std::vector<size_t> direct;
        std::vector<std::string> neighbors;
        for (size_t i = _edges.size(); i-- > 0;) {
            std::string s = key(_edges[i].subject), o = key(_edges[i].object);
            bool s_hit = std::find(seeds.begin(), seeds.end(), s) != seeds.end();
            bool o_hit = std::find(seeds.begin(), seeds.end(), o) != seeds.end();
            if (!s_hit && !o_hit) {
                continue;
            }
            direct.push_back(i);
            const std::string& other = s_hit ? o : s;
            if (std::find(seeds.begin(), seeds.end(), other) == seeds.end() &&
                std::find(neighbors.begin(), neighbors.end(), other) == neighbors.end()) {
                neighbors.push_back(other);
            }
        }

        std::string out;
        auto append = [&](const Edge& e) {
            std::string line = std::string(e.subject) + " | " + e.relation + " | " + e.object + "\n";
            if (out.size() + line.size() > maxChars) {
                return false;
            }
            out += line;
            return true;
        };
        for (size_t i : direct) {
            if (!append(_edges[i])) {
                return out;
            }
        }

        // 3) One step further: facts about the neighbors that are themselves subjects (people, pets...)
        bool header = false;
        for (size_t i = _edges.size(); i-- > 0;) {
            if (std::find(direct.begin(), direct.end(), i) != direct.end()) {
                continue;
            }
            std::string s = key(_edges[i].subject), o = key(_edges[i].object);
            bool near = std::find(neighbors.begin(), neighbors.end(), s) != neighbors.end() ||
                        std::find(neighbors.begin(), neighbors.end(), o) != neighbors.end();
            if (!near) {
                continue;
            }
            if (!header) {
                if (out.size() + 10 > maxChars) {
                    return out;
                }
                out += "Related:\n";
                header = true;
            }
            if (!append(_edges[i])) {
                return out;
            }
        }
        return out;
    }

    std::vector<Edge, Alloc>& edges()
    {
        return _edges;
    }

private:
    // "Lia (12), Gui (8), ..." by number of facts
    std::string overview(size_t maxChars) const
    {
        std::vector<std::pair<std::string, int>> counts;  // display name, facts
        auto bump = [&](const char* node) {
            std::string k = key(node);
            for (auto& c : counts) {
                if (key(c.first.c_str()) == k) {
                    c.second++;
                    return;
                }
            }
            counts.emplace_back(node, 1);
        };
        for (auto& e : _edges) {
            bump(e.subject);
        }
        std::stable_sort(counts.begin(), counts.end(), [](auto& a, auto& b) { return a.second > b.second; });
        std::string out;
        for (auto& c : counts) {
            std::string item = (out.empty() ? "" : ", ") + c.first + " (" + std::to_string(c.second) + ")";
            if (out.size() + item.size() > maxChars) {
                break;
            }
            out += item;
        }
        return out;
    }

    std::vector<Edge, Alloc> _edges;
};

}  // namespace memory_graph
