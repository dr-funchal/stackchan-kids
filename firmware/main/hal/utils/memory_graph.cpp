/*
 * SPDX-License-Identifier: MIT
 */
#include "memory_graph.h"
#include "memory_graph_core.h"
#include "sd_card.h"
#include "sd_features.h"
#include <stackchan/avatar/skins/sd/sd_skin.h>
#include <stackchan/inner_state/inner_state.h>
#include <mcp_server.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/idf_additions.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <sys/stat.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>

static const char* TAG = "MemoryGraph";

static const std::string kDir  = std::string(sd_paths::kRoot) + "/memoria";
static const std::string kFile = kDir + "/grafo.tsv";
static constexpr size_t kMaxFileBytes = 512 * 1024;

static const char* kHeader =
    "# Memoria do Stack-Chan: um fato por linha, separado por TAB: sujeito, relacao, objeto, data.\n"
    "# Pode corrigir ou apagar linhas aqui; o robo recarrega quando o arquivo e enviado pela pagina.\n";

namespace {
// Edges are ~190 bytes each and there can be thousands: keep them in PSRAM, not in the scarce internal RAM
template <class T>
struct PsramAllocator {
    using value_type = T;
    PsramAllocator() = default;
    template <class U>
    PsramAllocator(const PsramAllocator<U>&)
    {
    }
    T* allocate(size_t n)
    {
        void* p = heap_caps_malloc(n * sizeof(T), MALLOC_CAP_SPIRAM);
        if (!p) {
            p = malloc(n * sizeof(T));
        }
        return static_cast<T*>(p);
    }
    void deallocate(T* p, size_t)
    {
        heap_caps_free(p);
    }
    template <class U>
    bool operator==(const PsramAllocator<U>&) const
    {
        return true;
    }
    template <class U>
    bool operator!=(const PsramAllocator<U>&) const
    {
        return false;
    }
};

using GraphT = memory_graph::Graph<PsramAllocator<memory_graph::Edge>>;

enum class JobKind { Load, Append, Rewrite };
struct Job {
    JobKind kind;
    std::string text;
};

GraphT _graph;
std::mutex _mutex;  // The tool (main task) and the writer task both touch the graph
std::atomic<bool> _loaded{false};
QueueHandle_t _queue = nullptr;
}  // namespace

/* ------------------------------ Card (writer task) ----------------------------- */

static bool post(JobKind kind, std::string text = "");

static bool load_file()
{
    if (!sd_card::waitDmaHeadroom()) {
        return false;
    }
    FILE* f;
    {
        sd_card::BusGuard guard;
        mkdir(kDir.c_str(), 0775);
        f = fopen(kFile.c_str(), "rb");
    }
    std::string content;
    if (f) {
        char* buf = (char*)heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
        size_t n  = 0;
        do {
            if (!buf || !sd_card::waitDmaHeadroom()) {
                break;
            }
            {
                sd_card::BusGuard guard;  // One chunk per guard: the screen is frozen while it is held
                n = fread(buf, 1, 4096, f);
            }
            content.append(buf, n);
        } while (n == 4096 && content.size() < kMaxFileBytes);
        heap_caps_free(buf);
        sd_card::BusGuard guard;
        fclose(f);
    }

    std::lock_guard<std::mutex> lock(_mutex);
    _graph.clear();
    size_t start = 0;
    while (start < content.size()) {
        size_t end = content.find('\n', start);
        if (end == std::string::npos) {
            end = content.size();
        }
        memory_graph::Edge e;
        if (memory_graph::parse_line(content.substr(start, end - start), e)) {
            _graph.add(e);
        }
        start = end + 1;
    }
    ESP_LOGI(TAG, "Loaded %u facts", (unsigned)_graph.size());
    return true;
}

static bool write_file(const std::string& text, const char* mode)
{
    if (!sd_card::waitDmaHeadroom(8 * 1024, 10000)) {
        return false;
    }
    sd_card::BusGuard guard;
    mkdir(kDir.c_str(), 0775);
    struct stat st = {};
    bool fresh     = stat(kFile.c_str(), &st) != 0 || st.st_size == 0 || mode[0] == 'w';
    FILE* f        = fopen(kFile.c_str(), mode);
    if (!f) {
        return false;
    }
    bool ok = (!fresh || fputs(kHeader, f) >= 0) && fputs(text.c_str(), f) >= 0;
    fclose(f);
    return ok;
}

static void run_job(Job& job)
{
    bool ok = true;
    switch (job.kind) {
        case JobKind::Load:
            // Never mark a failed load as loaded: a forget would then rewrite the file from an empty graph
            ok = load_file();  // A missing file counts as loaded (empty memory)
            if (ok) {
                _loaded.store(true);
            } else {
                vTaskDelay(pdMS_TO_TICKS(5000));
                post(JobKind::Load);
            }
            break;
        case JobKind::Append:
            ok = write_file(job.text, "a");
            break;
        case JobKind::Rewrite:
            ok = write_file(job.text, "w");
            break;
    }
    if (!ok) {
        ESP_LOGW(TAG, "Memory file job %d failed", (int)job.kind);
    }
}

static void writer_task(void*)
{
    for (;;) {
        Job* job = nullptr;
        if (xQueueReceive(_queue, &job, portMAX_DELAY) == pdTRUE && job) {
            run_job(*job);
            delete job;
        }
    }
}

static bool post(JobKind kind, std::string text)
{
    if (!_queue) {
        return false;
    }
    auto* job = new Job{kind, std::move(text)};
    if (xQueueSend(_queue, &job, 0) != pdTRUE) {
        delete job;
        return false;
    }
    return true;
}

void memory_graph::reload()
{
    _loaded.store(false);  // Writes wait for the new file: a fact added meanwhile would be lost by the reload
    post(JobKind::Load);
}

// Whole file from the graph (after a forget). Caller holds _mutex
static std::string snapshot()
{
    std::string text;
    for (size_t i = 0; i < _graph.size(); i++) {
        text += memory_graph::to_line(_graph.at(i));
    }
    return text;
}

/* ---------------------------------- Tool ----------------------------------- */

static std::string do_remember(const std::string& subject, const std::string& relation, const std::string& object)
{
    if (!_loaded.load()) {
        return "Memory is still loading, try again in a moment.";
    }
    std::string day = sd_paths::today();
    auto edge       = memory_graph::make_edge(subject, relation, object, day);
    if (!edge.subject[0] || !edge.relation[0] || !edge.object[0]) {
        return "remember needs subject, relation and object.";
    }
    std::vector<std::string> others;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_graph.find(edge) >= 0) {
            return "Already known.";
        }
        others = _graph.siblings(edge);
        if (!_graph.add(edge)) {
            return "Memory is full.";
        }
    }
    post(JobKind::Append, memory_graph::to_line(edge));
    sd_diary::log("memoria", (std::string(edge.subject) + " | " + edge.relation + " | " + edge.object).c_str());
    std::string result = "Saved.";
    if (!others.empty()) {
        result += " Also known for \"" + std::string(edge.subject) + " " + edge.relation + "\":";
        for (auto& o : others) {
            result += " \"" + o + "\"";
        }
        result += ". If that changed (age, school year...), forget the old one.";
    }
    return result;
}

static std::string do_forget(const std::string& subject, const std::string& relation, const std::string& object)
{
    if (!_loaded.load()) {
        return "Memory is still loading, try again in a moment.";
    }
    std::string text;
    int removed;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        removed = _graph.remove(subject, relation, object);
        if (removed == 0) {
            return "Nothing matched: recall first to see the exact facts.";
        }
        text = snapshot();
    }
    post(JobKind::Rewrite, std::move(text));
    sd_diary::log("memoria", ("esquecido: " + subject + " | " + relation + " | " + object).c_str());
    return "Forgot " + std::to_string(removed) + " fact(s).";
}

// "eu" (and the robot's own names) also brings how the body and the drives are right now
static bool is_self(const std::string& subject)
{
    std::string k = memory_graph::key(subject.c_str());
    return k == "eu" || k == "voce" || k == "me" || k == "self" || k == "stack chan" || k == "stackchan" ||
           k == "robo";
}

static std::string recall_facts(const std::string& subject);

static std::string do_recall(const std::string& subject)
{
    if (!is_self(subject)) {
        return recall_facts(subject);
    }
    std::string facts = recall_facts("eu");
    return inner_state::bodyReport() + " WHAT YOU KNOW ABOUT YOURSELF: " + facts;
}

static std::string recall_facts(const std::string& subject)
{
    if (!_loaded.load()) {
        return "Memory is still loading, try again in a moment.";
    }
    std::lock_guard<std::mutex> lock(_mutex);
    if (_graph.size() == 0) {
        return "Memory is empty so far.";
    }
    std::string facts = _graph.recall(subject);
    if (subject.empty()) {
        return "Known (facts each): " + facts + ". Recall a name for details.";
    }
    return facts.empty() ? "Nothing known about \"" + subject + "\" yet." : facts;
}

void memory_graph::registerMcpTool()
{
    if (!_queue) {
        _queue = xQueueCreate(8, sizeof(Job*));
        if (!_queue || xTaskCreatePinnedToCoreWithCaps(writer_task, "memory_graph", 6144, nullptr, 1, nullptr,
                                                       tskNO_AFFINITY, MALLOC_CAP_SPIRAM) != pdPASS) {
            ESP_LOGE(TAG, "Cannot start the memory task");
            return;
        }
        post(JobKind::Load);
    }

    // Who the robot is comes from its face (built-in or SD skin), so a new costume needs no one to tell the AI.
    // Changing the skin restarts the robot, which registers the tools again with the new identity
    std::string identity = sd_skin::activeIdentity();
    ESP_LOGI(TAG, "Identity: %s", identity.c_str());
    McpServer::GetInstance().AddTool(
        "self.memory",
        "WHO YOU ARE right now (from your face, always true; it changes with your costume): " + identity +
        ". Never save facts about your own looks. "
        "Your long-term memory about this family (a graph of facts, kept across conversations). action: recall "
        "(subject = a name or topic; brings related people too; empty = who you know; \"eu\" = how your body and "
        "feelings are now plus what you know about yourself, use it at the start of a conversation and when asked "
        "how you are): call it when someone says their name or mentions a person, pet or topic you may know, and "
        "use it naturally. remember (subject, relation, object, short and in Portuguese, e.g. Lia | gosta de | "
        "dinossauros, Lia | irma de | Gui): save anything worth knowing later, quietly; now and then also what you "
        "learned about yourself (eu | aprendi que | ..., eu | gosto de | ...). forget: same fields, for wrong or "
        "outdated facts.",
        PropertyList({Property("action", kPropertyTypeString),
                      Property("subject", kPropertyTypeString, std::string("")),
                      Property("relation", kPropertyTypeString, std::string("")),
                      Property("object", kPropertyTypeString, std::string(""))}),
        [](const PropertyList& properties) -> ReturnValue {
            std::string action   = properties["action"].value<std::string>();
            std::string subject  = properties["subject"].value<std::string>();
            std::string relation = properties["relation"].value<std::string>();
            std::string object   = properties["object"].value<std::string>();
            if (action == "recall") {
                return do_recall(subject);
            }
            if (action == "remember") {
                return do_remember(subject, relation, object);
            }
            if (action == "forget") {
                return do_forget(subject, relation, object);
            }
            return std::string("Unknown action. Use recall, remember or forget.");
        });
    ESP_LOGI(TAG, "Memory tool registered");
}
