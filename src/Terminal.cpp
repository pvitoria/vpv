// http://pstreams.sourceforge.net/doc/
// http://libexecstream.sourceforge.net/reference.html#set_wait_timeout
// https://github.com/skystrife/procxx

#include <iostream>
#include <regex>

#include "imgui.h"
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui_internal.h"
#include "imgui_custom.hpp"

#include "events.hpp"
#include "Sequence.hpp"
#include "Player.hpp"
#include "ImageCollection.hpp"
#include "Progressable.hpp"
#include "LoadingThread.hpp"
#include "globals.hpp"
#include "Terminal.hpp"

class Process : public Progressable {
    Terminal& term;
    std::string command;
    bool finished;
    std::array<char, 1<<16> buffer;
    std::string result;
    FILE* pipe;

public:
    Process(Terminal& term, const std::string& command)
        : term(term), command(command), finished(false) {
        pipe = popen(command.c_str(), "r");
        if (!pipe) {
            finished = true;
        }
    }

    virtual float getProgressPercentage() const {
        return 0.f;
    }

    virtual bool isLoaded() const {
        return finished;
    }

    virtual void progress() {
        int read = fread(buffer.data(), 1, buffer.size(), pipe);
        buffer[read] = 0;
        if (read != 0) {
            result += buffer.data();
        } else {
            pclose(pipe);
            finished = true;
            {
                std::lock_guard<std::mutex> _lock(term.lock);
                term.cache[command] = result;
                gActive = std::max(gActive, 2);
                for (auto it = term.queuecommands.begin(); it != term.queuecommands.end(); it++) {
                    if (*it == command) {
                        term.queuecommands.erase(it);
                        break;
                    }
                }
            }
        }
    }
};

Terminal::Terminal() {
    runner = new SleepyLoadingThread([&]() -> std::shared_ptr<Progressable> {
        std::lock_guard<std::mutex> _lock(lock);
        if (!queuecommands.empty()) {
            std::string c = queuecommands.front();
            return std::make_shared<Process>(*this, c);
        }
        return nullptr;
    });
    runner->start();
}

void Terminal::setVisible(bool visible) {
    shown = visible;
    focusInput |= visible;
}

static void help(const char* text) {
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void Terminal::tick() {
    if (!shown)
        return;

    ImGui::SetNextWindowSize(ImVec2(500, 800), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Terminal", &shown, 0)) {
        ImGui::BringFront();
        if ((isKeyPressed("return") && ImGui::IsWindowFocused()) || focusInput)
            ImGui::SetKeyboardFocusHere();
        ImGui::InputText("", bufcommand, sizeof(bufcommand));
        if (!ImGui::GetIO().WantCaptureKeyboard)
            updateOutput();
        ImGui::SameLine();
        if (ImGui::Button(" C ")) {
            std::lock_guard<std::mutex> _lock(lock);
            queuecommands.clear();
            cache.clear();
        }
        help("Clear the result cache and rerun the command.");
        ImGui::BeginChild("..", ImVec2(0,0), false, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::TextUnformatted(output.c_str());
        ImGui::EndChild();
    }
    ImGui::End();
    focusInput = false;
}

void Terminal::updateOutput() {
    // build the command
    command = bufcommand;
    for (int i = gSequences.size() - 1; i >= 0; i--) {
        const Sequence& seq = *gSequences[i];
        std::string name = seq.collection->getFilename(seq.player->frame-1);
        command = std::regex_replace(command, std::regex("#" + std::to_string(i+1)), name);
    }

    // cache lookup or push to the thread
    std::lock_guard<std::mutex> _lock(lock);
    if (cache.find(command) == cache.end()) {
        for (auto& c : queuecommands) {
            if (c == command) {
                return;
            }
        }
        queuecommands.push_front(command);
        runner->notify();
    } else {
        output = cache[command];
    }
}

