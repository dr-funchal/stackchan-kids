#include <hal/utils/memory_graph_core.h>
#include <cstdio>

using namespace memory_graph;

static int failures = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                \
        }                                                              \
    } while (0)

static bool contains(const std::string& text, const char* part)
{
    return text.find(part) != std::string::npos;
}

int main()
{
    CHECK(key("  L\xC3\xAD" "a ") == "lia");
    CHECK(key("Irm\xC3\xA3 de") == "irma de");
    CHECK(key("gosta_de") == "gosta de");

    Edge e;
    CHECK(parse_line("Lia\tgosta de\tdinossauros\t2026-10-10\n", e));
    CHECK(std::string(e.subject) == "Lia" && std::string(e.object) == "dinossauros" &&
          std::string(e.date) == "2026-10-10");
    CHECK(!parse_line("# comment", e));
    CHECK(!parse_line("Lia\tgosta de", e));
    CHECK(to_line(make_edge("A\tB", "r", "o", "")) == "A B\tr\to\t\n");

    // Long fields are cut without leaving half a UTF-8 character
    std::string long_text(38, 'x');
    long_text += "\xC3\xA3\xC3\xA3";
    Edge cut = make_edge(long_text, "r", "o", "");
    CHECK(strlen(cut.subject) == 38);

    Graph<> g;
    CHECK(g.add(make_edge("Lia", "gosta de", "dinossauros", "2026-10-01")));
    CHECK(g.add(make_edge("Lia", "irm\xC3\xA3 de", "Gui", "2026-10-02")));
    CHECK(g.add(make_edge("Gui", "tem medo de", "escuro", "2026-10-03")));
    CHECK(g.add(make_edge("Bolinha", "cachorro de", "Gui", "2026-10-04")));
    CHECK(g.add(make_edge("Theo", "gosta de", "futebol", "2026-10-05")));
    CHECK(!g.add(make_edge("lia", "Gosta de", "Dinossauros", "")));  // Same fact
    CHECK(g.size() == 5);

    std::string lia = g.recall("Lia");
    CHECK(contains(lia, "Lia | gosta de | dinossauros"));
    CHECK(contains(lia, "Related:"));
    CHECK(contains(lia, "Gui | tem medo de | escuro"));  // One step away, through Gui
    CHECK(contains(lia, "Bolinha | cachorro de | Gui"));
    CHECK(!contains(lia, "futebol"));
    CHECK(contains(g.recall("bol"), "cachorro"));  // Partial name
    CHECK(g.recall("ui").empty());                 // Too short for a partial match
    CHECK(g.add(make_edge("eu", "gosto de", "historias de piratas", "")));
    CHECK(g.add(make_edge("Theo", "mora em", "meu bairro", "")));
    CHECK(!contains(g.recall("eu"), "Theo"));       // "eu" is exact, never "meu"
    CHECK(g.recall("ninguem").empty());
    CHECK(contains(g.recall(""), "Lia (2)"));
    CHECK(g.recall("Lia", 40).size() <= 40);

    CHECK(g.siblings(make_edge("Lia", "gosta de", "bonecas", "")).size() == 1);
    CHECK(g.remove("LIA", "gosta de", "dinossauros") == 1);
    CHECK(g.remove("Lia", "gosta de", "dinossauros") == 0);
    CHECK(g.add(make_edge("Lia", "idade", "5", "")));
    CHECK(g.add(make_edge("Lia", "idade", "6", "")));
    CHECK(g.remove("Lia", "idade", "") == 2);  // Empty object: the whole relation
    CHECK(g.size() == 6);

    if (failures == 0) {
        std::printf("memory_graph_test: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
