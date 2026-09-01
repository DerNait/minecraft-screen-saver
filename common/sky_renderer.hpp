// ============================================================================
//  sky_renderer.hpp - Skybox procedural para el ciclo de dia y noche
// ============================================================================
#pragma once

#include <GL/glew.h>
#include <glm/glm.hpp>

#include <string>

namespace mcss {

struct SkyEnvironment {
    glm::vec3 zenithColor{0.25f, 0.60f, 0.95f};
    glm::vec3 horizonColor{0.72f, 0.88f, 1.0f};
    glm::vec3 sunDirection{0.0f, 1.0f, 0.0f};
    glm::vec3 sunColor{1.0f, 0.92f, 0.72f};
    glm::vec3 moonColor{0.72f, 0.82f, 1.0f};
    glm::vec3 sunsetColor{1.0f, 0.30f, 0.08f};
    float daylight = 1.0f;
    float starVisibility = 0.0f;
    float sunsetStrength = 0.0f;
    float timeSeconds = 0.0f;
};

class SkyRenderer {
public:
    bool init(std::string& error);
    void draw(const glm::mat4& projection, const glm::mat4& view,
              const SkyEnvironment& environment);
    void destroy();

private:
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint program_ = 0;
    GLint projectionLoc_ = -1;
    GLint viewLoc_ = -1;
    GLint zenithColorLoc_ = -1;
    GLint horizonColorLoc_ = -1;
    GLint sunDirectionLoc_ = -1;
    GLint sunColorLoc_ = -1;
    GLint moonColorLoc_ = -1;
    GLint sunsetColorLoc_ = -1;
    GLint daylightLoc_ = -1;
    GLint starVisibilityLoc_ = -1;
    GLint sunsetStrengthLoc_ = -1;
    GLint timeLoc_ = -1;
};

} // namespace mcss
