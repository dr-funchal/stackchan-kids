/*
 * SPDX-License-Identifier: MIT
 */
#include "stories.h"
#include "stories_builtin.h"
#include "stories_seeds.h"
#include "sd_card.h"
#include "sd_features.h"
#include <mcp_server.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_random.h>
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

static const char* TAG = "Stories";

static constexpr size_t kPageChars = 750;  // ~120 words for .txt stories from the card

namespace {
std::string _title;
std::vector<std::string> _pages;
size_t _page = 0;
}  // namespace

static const char* kReadingRules =
    "READ-ALOUD RULES: read the page EXACTLY as written, word for word, with an expressive storyteller voice for "
    "small children: do not summarize, skip, add or explain anything. The text has no accents: pronounce it as "
    "correct Portuguese. Right after finishing a page, call self.historia.continuar to get the next one and keep "
    "reading, without waiting or asking. When it says FIM, the story is over: ask the children softly what they "
    "liked most. If a child interrupts, answer briefly and then continue with self.historia.continuar.";

// .txt stories from the card: split into ~kPageChars pages at sentence ends
static bool load_card_story(const std::string& file)
{
    std::string text;
    {
        // Small reads (512-byte sectors) need less DMA headroom than the background writers; wait up to 1 s for it
        // instead of failing (during a conversation the big check failed and the story looked missing)
        for (int i = 0; i < 20 && !sd_card::hasDmaHeadroom(4 * 1024); i++) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        if (!sd_card::hasDmaHeadroom(4 * 1024)) {
            ESP_LOGW(TAG, "No DMA memory to read %s", file.c_str());
            return false;
        }
        sd_card::BusGuard guard;
        FILE* f = fopen((std::string(sd_paths::kStories) + "/" + file).c_str(), "r");
        if (!f) {
            return false;
        }
        char buf[512];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0 && text.size() < 16 * 1024) {
            text.append(buf, n);
        }
        fclose(f);
    }
    for (auto& c : text) {
        if (c == '\n' || c == '\r') {
            c = ' ';
        }
    }
    _pages.clear();
    size_t start = 0;
    while (start < text.size()) {
        size_t end = std::min(text.size(), start + kPageChars);
        if (end < text.size()) {
            size_t dot = text.find_last_of(".!?", end);
            if (dot != std::string::npos && dot > start + kPageChars / 2) {
                end = dot + 1;
            }
        }
        _pages.push_back(text.substr(start, end - start));
        start = end;
    }
    _title = file.substr(0, file.size() - 4);
    return !_pages.empty();
}

static std::vector<std::string> card_stories()
{
    return sd_card::isMounted() ? sd_paths::listFiles(sd_paths::kStories, ".txt") : std::vector<std::string>();
}

static std::string page_text()
{
    std::string text = "PAGE " + std::to_string(_page + 1) + " OF " + std::to_string(_pages.size()) + ": " +
                       _pages[_page];
    if (_page + 1 >= _pages.size()) {
        text += " [FIM - this was the last page]";
    }
    return text;
}

void stories::exportSeeds()
{
    if (!sd_card::isMounted()) {
        return;
    }
    int copied = 0;
    for (auto& seed : kStorySeeds) {
        std::string path = std::string(sd_paths::kStories) + "/" + seed.file;
        sd_card::BusGuard guard;
        FILE* f = fopen(path.c_str(), "r");
        if (f) {
            fclose(f);
            continue;
        }
        f = fopen(path.c_str(), "w");
        if (f) {
            fputs(seed.text, f);
            fclose(f);
            copied++;
        }
    }
    ESP_LOGI(TAG, "Story seeds copied to the card: %d new", copied);
}

std::string stories::listTitles()
{
    std::string list;
    for (auto& s : kBuiltinStories) {
        list += std::string(list.empty() ? "" : "; ") + s.title;
    }
    for (auto& f : card_stories()) {
        list += "; " + f.substr(0, f.size() - 4);
    }
    return list;
}

bool stories::start(const std::string& name, std::string& result, bool allowRandom)
{
    ESP_LOGI(TAG, "Looking for '%s' (card has %u .txt)", name.c_str(), (unsigned)card_stories().size());
    std::string wanted = sd_paths::sanitize(name);
    _pages.clear();
    _page = 0;

    const BuiltinStory* found = nullptr;
    // Card stories by the full request (a .txt title is its file name)
    {
        for (auto& f : card_stories()) {
            if (!wanted.empty() && sd_paths::sanitize(f).find(wanted) != std::string::npos &&
                load_card_story(f)) {
                break;
            }
        }
    }
    for (auto& s : kBuiltinStories) {
        if (!_pages.empty()) {
            break;  // A card story already matched the full request
        }
        std::string title = sd_paths::sanitize(s.title);
        // Title contains the request, or the request is exactly the id. Never "request contains the id": "narnia"
        // is inside "narnia - o sobrinho do mago" and picked the wrong Narnia
        if (!wanted.empty() && (title.find(wanted) != std::string::npos || wanted == s.id)) {
            found = &s;
            break;
        }
    }
    // Loose match on any word of the request ("a do morcego" -> morcego)
    if (!found && _pages.empty()) {
        for (auto& f : card_stories()) {
            std::string file = sd_paths::sanitize(f);
            size_t pos = 0;
            bool hit   = false;
            while (!hit && pos < wanted.size()) {
                size_t end       = wanted.find('_', pos);
                std::string word = wanted.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
                hit = word.size() >= 5 && word != "narnia" && file.find(word) != std::string::npos;
                pos = end == std::string::npos ? wanted.size() : end + 1;
            }
            if (hit && load_card_story(f)) {
                break;
            }
        }
    }
    if (!found && _pages.empty()) {
        for (auto& s : kBuiltinStories) {
            std::string title = sd_paths::sanitize(s.title);
            size_t pos        = 0;
            while (!found && pos < wanted.size()) {
                size_t end       = wanted.find('_', pos);
                std::string word = wanted.substr(pos, end == std::string::npos ? std::string::npos
                                                                               : end - pos);
                if (word.size() >= 4 && title.find(word) != std::string::npos) {
                    found = &s;
                }
                pos = end == std::string::npos ? wanted.size() : end + 1;
            }
            if (found) {
                break;
            }
        }
    }
    if (!found && _pages.empty() && allowRandom) {
        found = &kBuiltinStories[esp_random() % (sizeof(kBuiltinStories) / sizeof(kBuiltinStories[0]))];
    }
    if (found) {
        _title = found->title;
        for (auto* p : found->pages) {
            _pages.emplace_back(p);
        }
    }

    if (!found && _pages.empty()) {
        return false;
    }
    ESP_LOGI(TAG, "Reading '%s' (%u pages)", _title.c_str(), (unsigned)_pages.size());
    sd_diary::log("historia", _title.c_str());
    result = std::string(kReadingRules) + " TITLE: " + _title + ". Say the title, then read. " +
             page_text();
    return true;
}

void stories::registerMcpTools()
{
    auto& mcp = McpServer::GetInstance();

    mcp.AddTool("self.historia.continuar",
                "Get the next page of the story being read aloud. Call right after finishing each page.",
                PropertyList(), [](const PropertyList&) -> ReturnValue {
                    if (_pages.empty()) {
                        return std::string("No story being read. Use self.story.play.");
                    }
                    if (_page + 1 >= _pages.size()) {
                        return std::string("The story is over (FIM).");
                    }
                    _page++;
                    return page_text();
                });

    mcp.AddTool("self.historia.parar", "Stop reading the current story.", PropertyList(),
                [](const PropertyList&) -> ReturnValue {
                    _pages.clear();
                    _page = 0;
                    return true;
                });

    ESP_LOGI(TAG, "%u built-in stories", (unsigned)(sizeof(kBuiltinStories) / sizeof(kBuiltinStories[0])));
}
