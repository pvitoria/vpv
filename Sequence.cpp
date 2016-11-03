#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cfloat>
#include <glob.h>

extern "C" {
#include "iio.h"
}

#include "Sequence.hpp"
#include "Player.hpp"
#include "alphanum.hpp"

Sequence::Sequence()
{
    static int id = 0;
    id++;
    ID = "Sequence " + std::to_string(id);

    view = nullptr;
    player = nullptr;
    valid = false;
    visible = false;
    glob.reserve(1024);
    glob_.reserve(1024);
    glob = "";
    glob_ = "";
}

void Sequence::loadFilenames() {
    glob_t res;
    ::glob(glob.c_str(), GLOB_TILDE | GLOB_NOSORT, NULL, &res);
    filenames.resize(res.gl_pathc);
    for(unsigned int j = 0; j < res.gl_pathc; j++) {
        filenames[j] = res.gl_pathv[j];
    }
    globfree(&res);
    std::sort(filenames.begin(), filenames.end(), doj::alphanum_less<std::string>());

    valid = filenames.size() > 0;
    strcpy(&glob_[0], &glob[0]);

    loadedFrame = -1;
}

void Sequence::loadTextureIfNeeded()
{
    if (valid && visible && player && loadedFrame != player->frame) {
        int frame = player->frame;
        if (!texture.loadFromFile(filenames[frame - 1])) {
            int w, h, d;
            float* pixels = iio_read_image_float_vec(filenames[frame - 1].c_str(), &w, &h, &d);
            float min = 0;
            float max = FLT_MIN;
            for (int i = 0; i < w*h*d; i++) {
                min = fminf(min, pixels[i]);
                max = fmaxf(max, pixels[i]);
            }
            float a = 1.f;
            float b = 0.f;
            if (fabsf(min - 0.f) < 0.01f && fabsf(max - 1.f) < 0.01f) {
                a = 255.f;
            } else {
                a = 255.f / (max - min);
                b = - min;
            }
            unsigned char* rgba = new unsigned char[w * h * 4];
            for (int i = 0; i < w*h; i++) {
                rgba[i * 4 + 0] = b + a*pixels[i * d + 0];
                if (d > 1)
                    rgba[i * 4 + 1] = b + a*pixels[i * d + 1];
                if (d > 2)
                    rgba[i * 4 + 2] = b + a*pixels[i * d + 2];
                for (int dd = d; dd < 3; dd++) {
                    rgba[i * 4 + dd] = rgba[i * 4];
                }
                rgba[i * 4 + 3] = 255;
            }
            texture.create(w, h);
            texture.update(rgba);
            free(pixels);
            delete[] rgba;
        }
        loadedFrame = frame;
    }
}