#include <string>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <mutex>

#include "imgui.h"
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui_internal.h"

#define NANOSVG_ALL_COLOR_KEYWORDS
#define NANOSVG_IMPLEMENTATION	// Expands implementation
#include "SVG.hpp"
#include "watcher.hpp"
#include "globals.hpp"

std::unordered_map<std::string, SVG*> SVG::cache;
static std::mutex lock;

SVG::SVG()
    : nsvg(nullptr), filename(""), valid(false)
{
}

void SVG::loadFromFile(const std::string& filename)
{
    this->filename = filename;
    valid = (nsvg = nsvgParseFromFile(filename.c_str(), "px", 96));
}

void SVG::loadFromString(const std::string& str)
{
    filename = "buffer";
    std::string copy(str);
    valid = (nsvg = nsvgParse(&copy[0], "px", 96));
}

SVG::~SVG()
{
    if (nsvg)
        nsvgDelete(nsvg);
}

void SVG::draw(ImVec2 basepos, ImVec2 pos, float zoom) const
{
    if (!nsvg || !valid)
        return;

    const auto adjust = [basepos,pos,zoom](float x, float y, bool relative) {
        if (relative)
            return ImVec2(x,y) * zoom + pos + basepos;
        return ImVec2(x,y) + basepos;
    };

    auto dl = ImGui::GetWindowDrawList();
    for (auto shape = nsvg->shapes; shape != NULL; shape = shape->next) {
        if (!(shape->flags & NSVG_FLAGS_VISIBLE))
            continue;

        bool rel = shape->flags & NSVG_FLAGS_RELATIVE;
        ImU32 fillColor = shape->fill.color;
        ImU32 strokeColor = shape->stroke.color;
        float strokeWidth = shape->strokeWidth * (rel?zoom:1.f);
        if (shape->strokeWidth < 0) {
            strokeWidth = -shape->strokeWidth;
        }

        if (shape->isText) {
            dl->AddText(nullptr, shape->fontSize*(rel?zoom:1), adjust(shape->paths->pts[0], shape->paths->pts[1], rel),
                        fillColor, shape->textData);
            continue;
        }

        for (auto path = shape->paths; path != NULL; path = path->next) {
            dl->PathClear();
            dl->PathLineTo(adjust(path->pts[0], path->pts[1], rel));

            for (int i = 0; i < path->npts-1; i += 3) {
                float* p = &path->pts[i*2];
                dl->PathBezierCurveTo(adjust(p[2], p[3], rel), adjust(p[4], p[5], rel), adjust(p[6], p[7], rel));
            }

            if (path->closed)
                dl->PathLineTo(adjust(path->pts[0], path->pts[1], rel));
            if (shape->stroke.type)
                dl->PathStroke(strokeColor, false, strokeWidth);
            if (shape->fill.type && dl->_Path.Size)
                dl->AddConvexPolyFilled(dl->_Path.Data, dl->_Path.Size, fillColor);
        }
    }
    dl->PathClear();
}

SVG* SVG::get(const std::string& filename)
{
    lock.lock();
    auto i = cache.find(filename);
    if (i != cache.end()) {
        SVG* svg = i->second;
        lock.unlock();
        return svg;
    }
    lock.unlock();

    SVG* svg = new SVG;
    svg->loadFromFile(filename);
    lock.lock();
    cache[filename] = svg;
    lock.unlock();
    if (svg->valid) {
        //printf("'%s' loaded\n", filename.c_str());
    } else {
        printf("'%s' invalid\n", filename.c_str());
    }

    watcher_add_file(filename, [&](const std::string& f) {
        lock.lock();
        auto entry = cache.find(filename);
        if (entry != cache.end()) {
            SVG* svg = entry->second;
            delete svg;
            cache.erase(entry);
        }
        lock.unlock();
        //printf("'%s' modified on disk, cache invalidated\n", filename.c_str());
        gActive = std::max(gActive, 2);
    });

    return svg;
}

std::shared_ptr<SVG> SVG::createFromString(const std::string& str)
{
    struct sharablesvg : public SVG {};
    std::shared_ptr<SVG> svg = std::make_shared<sharablesvg>();
    svg->loadFromString(str);
    return svg;
}

void SVG::flushCache()
{
    for (auto v : cache) {
        delete v.second;
    }
    cache.clear();
}

