// ============================================================================
//  sky_renderer.cpp - Gradiente atmosferico, sol, luna, estrellas y nubes
// ============================================================================
#include "sky_renderer.hpp"

#include "render.hpp"

#include <glm/gtc/type_ptr.hpp>

namespace mcss {
namespace {

const float SKYBOX_VERTICES[] = {
    -1,-1,-1,  1,-1,-1,  1, 1,-1,  1, 1,-1, -1, 1,-1, -1,-1,-1,
    -1,-1, 1,  1,-1, 1,  1, 1, 1,  1, 1, 1, -1, 1, 1, -1,-1, 1,
    -1, 1, 1, -1, 1,-1, -1,-1,-1, -1,-1,-1, -1,-1, 1, -1, 1, 1,
     1, 1, 1,  1, 1,-1,  1,-1,-1,  1,-1,-1,  1,-1, 1,  1, 1, 1,
    -1,-1,-1,  1,-1,-1,  1,-1, 1,  1,-1, 1, -1,-1, 1, -1,-1,-1,
    -1, 1,-1,  1, 1,-1,  1, 1, 1,  1, 1, 1, -1, 1, 1, -1, 1,-1
};

const char* SKY_VERTEX_SHADER = R"glsl(
#version 330 core

layout(location = 0) in vec3 aPosition;
uniform mat4 uProjection;
uniform mat4 uView;
out vec3 vDirection;

void main() {
    vDirection = aPosition;
    mat4 rotationOnlyView = mat4(mat3(uView));
    vec4 clip = uProjection * rotationOnlyView * vec4(aPosition, 1.0);
    gl_Position = clip.xyww;
}
)glsl";

const char* SKY_FRAGMENT_SHADER = R"glsl(
#version 330 core

in vec3 vDirection;
uniform vec3 uZenithColor;
uniform vec3 uHorizonColor;
uniform vec3 uSunDirection;
uniform vec3 uSunColor;
uniform vec3 uMoonColor;
uniform vec3 uSunsetColor;
uniform float uDaylight;
uniform float uStarVisibility;
uniform float uSunsetStrength;
uniform float uTime;
out vec4 FragColor;

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

void main() {
    vec3 direction = normalize(vDirection);
    float heightMix = smoothstep(-0.18, 0.72, direction.y);
    vec3 color = mix(uHorizonColor, uZenithColor, heightMix);

    vec2 horizontal = normalize(direction.xz + vec2(0.0001));
    vec2 sunHorizontal = normalize(uSunDirection.xz + vec2(0.0001));
    float towardSun = max(dot(horizontal, sunHorizontal), 0.0);
    float horizonBand = exp(-abs(direction.y) * 7.0);
    color += uSunsetColor * uSunsetStrength * horizonBand * pow(towardSun, 4.0);

    float sunDot = dot(direction, normalize(uSunDirection));
    float sunGlow = pow(max(sunDot, 0.0), 96.0) * 0.35 * uDaylight;
    float sunDisc = smoothstep(0.99925, 0.99972, sunDot) * uDaylight;
    color += uSunColor * sunGlow;
    color = mix(color, uSunColor, sunDisc);

    vec3 moonDirection = -normalize(uSunDirection);
    float moonDot = dot(direction, moonDirection);
    float moonGlow = pow(max(moonDot, 0.0), 180.0) * 0.12 * uStarVisibility;
    float moonDisc = smoothstep(0.99945, 0.99978, moonDot) * uStarVisibility;
    color += uMoonColor * moonGlow;
    color = mix(color, uMoonColor, moonDisc);

    vec3 starCell = floor(direction * 420.0);
    float starNoise = hash21(starCell.xy + starCell.z * 0.73);
    float stars = step(0.9960, starNoise) * uStarVisibility;
    stars *= smoothstep(-0.08, 0.20, direction.y);
    color += vec3(stars * (0.75 + 0.45 * hash21(starCell.yz)));

    // Nubes cuadradas y discretas para conservar la estetica de bloques.
    if (direction.y > 0.04) {
        vec2 cloudUv = direction.xz / (direction.y + 0.28);
        cloudUv = cloudUv * 3.0 + vec2(uTime * 0.018, uTime * 0.006);
        float cloudNoise = hash21(floor(cloudUv));
        float cloud = step(0.64, cloudNoise) * smoothstep(0.04, 0.28, direction.y);
        cloud *= uDaylight * 0.20;
        color = mix(color, vec3(0.96, 0.98, 1.0), cloud);
    }

    FragColor = vec4(color, 1.0);
}
)glsl";

} // namespace

bool SkyRenderer::init(std::string& error) {
    destroy();
    program_ = compileShaderProgram(SKY_VERTEX_SHADER, SKY_FRAGMENT_SHADER, error);
    if (program_ == 0) return false;

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    if (vao_ == 0 || vbo_ == 0) {
        error = "OpenGL no pudo crear la geometria del skybox";
        destroy();
        return false;
    }

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(SKYBOX_VERTICES), SKYBOX_VERTICES,
                 GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    projectionLoc_ = glGetUniformLocation(program_, "uProjection");
    viewLoc_ = glGetUniformLocation(program_, "uView");
    zenithColorLoc_ = glGetUniformLocation(program_, "uZenithColor");
    horizonColorLoc_ = glGetUniformLocation(program_, "uHorizonColor");
    sunDirectionLoc_ = glGetUniformLocation(program_, "uSunDirection");
    sunColorLoc_ = glGetUniformLocation(program_, "uSunColor");
    moonColorLoc_ = glGetUniformLocation(program_, "uMoonColor");
    sunsetColorLoc_ = glGetUniformLocation(program_, "uSunsetColor");
    daylightLoc_ = glGetUniformLocation(program_, "uDaylight");
    starVisibilityLoc_ = glGetUniformLocation(program_, "uStarVisibility");
    sunsetStrengthLoc_ = glGetUniformLocation(program_, "uSunsetStrength");
    timeLoc_ = glGetUniformLocation(program_, "uTime");
    return true;
}

void SkyRenderer::draw(const glm::mat4& projection, const glm::mat4& view,
                       const SkyEnvironment& environment) {
    if (program_ == 0) return;

    glDepthMask(GL_FALSE);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);

    glUseProgram(program_);
    glUniformMatrix4fv(projectionLoc_, 1, GL_FALSE, glm::value_ptr(projection));
    glUniformMatrix4fv(viewLoc_, 1, GL_FALSE, glm::value_ptr(view));
    glUniform3fv(zenithColorLoc_, 1, glm::value_ptr(environment.zenithColor));
    glUniform3fv(horizonColorLoc_, 1, glm::value_ptr(environment.horizonColor));
    glUniform3fv(sunDirectionLoc_, 1, glm::value_ptr(environment.sunDirection));
    glUniform3fv(sunColorLoc_, 1, glm::value_ptr(environment.sunColor));
    glUniform3fv(moonColorLoc_, 1, glm::value_ptr(environment.moonColor));
    glUniform3fv(sunsetColorLoc_, 1, glm::value_ptr(environment.sunsetColor));
    glUniform1f(daylightLoc_, environment.daylight);
    glUniform1f(starVisibilityLoc_, environment.starVisibility);
    glUniform1f(sunsetStrengthLoc_, environment.sunsetStrength);
    glUniform1f(timeLoc_, environment.timeSeconds);
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);

    glEnable(GL_CULL_FACE);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
}

void SkyRenderer::destroy() {
    if (vbo_ != 0) glDeleteBuffers(1, &vbo_);
    if (vao_ != 0) glDeleteVertexArrays(1, &vao_);
    if (program_ != 0) glDeleteProgram(program_);
    vbo_ = vao_ = program_ = 0;
    projectionLoc_ = viewLoc_ = zenithColorLoc_ = horizonColorLoc_ = -1;
    sunDirectionLoc_ = sunColorLoc_ = moonColorLoc_ = sunsetColorLoc_ = -1;
    daylightLoc_ = starVisibilityLoc_ = sunsetStrengthLoc_ = timeLoc_ = -1;
}

} // namespace mcss
