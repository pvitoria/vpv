#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>

#include "imgui.h"
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui_internal.h"

#include "editors.hpp"

struct View;
struct Player;
struct Colormap;
struct Image;
struct SVG;
class ImageCollection;
class ImageProvider;
class EditGUI;

struct Sequence {
    std::string ID;
    std::string glob;
    std::string glob_;

    ImageCollection* collection;
    std::vector<std::string> svgglobs;
    std::vector<std::vector<std::string>> svgcollection;
    bool valid;

    int loadedFrame;
    mutable float previousFactor;

    View* view;
    Player* player;
    Colormap* colormap;
    std::shared_ptr<ImageProvider> imageprovider;
    std::shared_ptr<Image> image;
    std::string error;

    ImageCollection* uneditedCollection;
    EditGUI* editGUI;

    Sequence();
    ~Sequence();

    void loadFilenames();

    void tick();
    void forgetImage();

    void autoScaleAndBias();
    void snapScaleAndBias();
    void localAutoScaleAndBias(ImVec2 p1, ImVec2 p2);
    void cutScaleAndBias(float percentile);

    std::shared_ptr<Image> getCurrentImage();
    float getViewRescaleFactor() const;
    std::vector<const SVG*> getCurrentSVGs() const;

    const std::string getTitle() const;
    void showInfo() const;

    void setEdit(const std::string& edit, EditType edittype=PLAMBDA);
    std::string getEdit();
    int getId();

    std::string getGlob() const;
    void setGlob(const std::string& glob);
};
