// ============================================================================
//  shadow_map.cpp - Framebuffer de profundidad con comparacion para PCF
// ============================================================================
#include "shadow_map.hpp"

namespace mcss {

bool ShadowMap::init(int resolution, std::string& error) {
    destroy();
    error.clear();
    resolution_ = resolution;

    glGenFramebuffers(1, &framebuffer_);
    glGenTextures(1, &depthTexture_);
    if (framebuffer_ == 0 || depthTexture_ == 0) {
        error = "OpenGL no pudo crear el mapa de sombras";
        destroy();
        return false;
    }

    glBindTexture(GL_TEXTURE_2D, depthTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24,
                 resolution_, resolution_, 0, GL_DEPTH_COMPONENT,
                 GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE,
                    GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    const float borderColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                           GL_TEXTURE_2D, depthTexture_, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        error = "el framebuffer del mapa de sombras esta incompleto";
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        destroy();
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

void ShadowMap::beginDepthPass() {
    glViewport(0, 0, resolution_, resolution_);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f, 4.0f);
    glCullFace(GL_FRONT);
}

void ShadowMap::endDepthPass(int framebufferWidth, int framebufferHeight) {
    glCullFace(GL_BACK);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, framebufferWidth, framebufferHeight);
}

void ShadowMap::destroy() {
    if (depthTexture_ != 0) glDeleteTextures(1, &depthTexture_);
    if (framebuffer_ != 0) glDeleteFramebuffers(1, &framebuffer_);
    depthTexture_ = framebuffer_ = 0;
    resolution_ = 0;
}

} // namespace mcss
