#include <algorithm>
#include <string>
#include <map>
#include <array>

#include "Shader.hpp"
#include "shaders.hpp"
#include "globals.hpp"

#define S(...) #__VA_ARGS__

static std::string defaultVertex = S(
    uniform mat4 v_transform;
    in vec2 v_position;
    in vec2 v_texcoord;
    in vec4 v_color;
    out vec2 f_texcoord;
    out vec4 f_color;
    void main()
    {
        f_texcoord = v_texcoord;
        f_color = v_color;
        gl_Position = v_transform * vec4(v_position.xy,0,1);
    }
);

Shader* createShader(const std::string& mainFragment)
{
    Shader* shader = new Shader;
    std::copy(mainFragment.begin(), mainFragment.end()+1, shader->codeFragment);
    std::copy(defaultVertex.begin(), defaultVertex.end()+1, shader->codeVertex);
    if (!shader->compile()) {
    }
    return shader;
}

bool loadShader(const std::string& name, const std::string& mainFragment)
{
    Shader* shader = createShader(mainFragment);
    shader->name = name;
    gShaders.push_back(shader);
    std::sort(gShaders.begin(), gShaders.end(),
              [](const Shader* lhs, const Shader* rhs) {
                  return lhs->name < rhs->name;
              });
    return true;
}

Shader* getShader(const std::string& name)
{
    for (auto s : gShaders) {
        if (s->name == name)
            return s;
    }
    return nullptr;
}

