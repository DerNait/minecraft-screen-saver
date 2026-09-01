// ============================================================================
//  shadow_map.hpp - Mapa de profundidad para sombras direccionales locales
// ============================================================================
#pragma once

#include <GL/glew.h>

#include <string>

namespace mcss {

class ShadowMap {
public:
    bool init(int resolution, std::string& error);
    void beginDepthPass();
    void endDepthPass(int framebufferWidth, int framebufferHeight);
    void destroy();

    GLuint textureId() const { return depthTexture_; }
    int resolution() const { return resolution_; }

private:
    GLuint framebuffer_ = 0;
    GLuint depthTexture_ = 0;
    int resolution_ = 0;
};

} // namespace mcss
