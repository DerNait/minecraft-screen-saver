// ============================================================================
//  render.cpp - Implementacion del renderizador instanciado
// ============================================================================
#include "render.hpp"

#include <glm/gtc/type_ptr.hpp>

#include <cstddef>

namespace mcss {
namespace {

// ----------------------------------------------------------------------------
//  Geometria del cubo unitario centrado en el origen.
//  Cada vertice lleva: posicion (3 floats), coordenada de textura (2 floats) y
//  un identificador de cara (1 float) que el shader usa para dos cosas:
//    - elegir cual de las tres texturas del bloque corresponde a esa cara
//    - aplicar el sombreado direccional fijo (la cara superior recibe mas luz)
//  Identificadores: 0 = superior, 1 = inferior, 2 = caras +-X, 3 = caras +-Z.
//  El orden de los vertices es antihorario visto desde fuera del cubo, para que
//  el descarte de caras traseras (GL_CULL_FACE) elimine las caras interiores.
// ----------------------------------------------------------------------------
constexpr int   kFloatsPerVertex = 6;
constexpr int   kVerticesPerCube = 36;

const float CUBE_VERTICES[kVerticesPerCube * kFloatsPerVertex] = {
    // ---- Cara superior (+Y), cara 0 ----------------------------------------
    //  x     y     z     u     v     cara
    -0.5f, 0.5f, 0.5f, 0.0f, 1.0f, 0.0f,
     0.5f, 0.5f, 0.5f, 1.0f, 1.0f, 0.0f,
     0.5f, 0.5f,-0.5f, 1.0f, 0.0f, 0.0f,
    -0.5f, 0.5f, 0.5f, 0.0f, 1.0f, 0.0f,
     0.5f, 0.5f,-0.5f, 1.0f, 0.0f, 0.0f,
    -0.5f, 0.5f,-0.5f, 0.0f, 0.0f, 0.0f,

    // ---- Cara inferior (-Y), cara 1 ----------------------------------------
    -0.5f,-0.5f,-0.5f, 0.0f, 0.0f, 1.0f,
     0.5f,-0.5f,-0.5f, 1.0f, 0.0f, 1.0f,
     0.5f,-0.5f, 0.5f, 1.0f, 1.0f, 1.0f,
    -0.5f,-0.5f,-0.5f, 0.0f, 0.0f, 1.0f,
     0.5f,-0.5f, 0.5f, 1.0f, 1.0f, 1.0f,
    -0.5f,-0.5f, 0.5f, 0.0f, 1.0f, 1.0f,

    // ---- Cara +X, cara 2 ---------------------------------------------------
     0.5f,-0.5f, 0.5f, 0.0f, 1.0f, 2.0f,
     0.5f,-0.5f,-0.5f, 1.0f, 1.0f, 2.0f,
     0.5f, 0.5f,-0.5f, 1.0f, 0.0f, 2.0f,
     0.5f,-0.5f, 0.5f, 0.0f, 1.0f, 2.0f,
     0.5f, 0.5f,-0.5f, 1.0f, 0.0f, 2.0f,
     0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 2.0f,

    // ---- Cara -X, cara 2 ---------------------------------------------------
    -0.5f,-0.5f,-0.5f, 0.0f, 1.0f, 2.0f,
    -0.5f,-0.5f, 0.5f, 1.0f, 1.0f, 2.0f,
    -0.5f, 0.5f, 0.5f, 1.0f, 0.0f, 2.0f,
    -0.5f,-0.5f,-0.5f, 0.0f, 1.0f, 2.0f,
    -0.5f, 0.5f, 0.5f, 1.0f, 0.0f, 2.0f,
    -0.5f, 0.5f,-0.5f, 0.0f, 0.0f, 2.0f,

    // ---- Cara +Z, cara 3 ---------------------------------------------------
    -0.5f,-0.5f, 0.5f, 0.0f, 1.0f, 3.0f,
     0.5f,-0.5f, 0.5f, 1.0f, 1.0f, 3.0f,
     0.5f, 0.5f, 0.5f, 1.0f, 0.0f, 3.0f,
    -0.5f,-0.5f, 0.5f, 0.0f, 1.0f, 3.0f,
     0.5f, 0.5f, 0.5f, 1.0f, 0.0f, 3.0f,
    -0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 3.0f,

    // ---- Cara -Z, cara 3 ---------------------------------------------------
     0.5f,-0.5f,-0.5f, 0.0f, 1.0f, 3.0f,
    -0.5f,-0.5f,-0.5f, 1.0f, 1.0f, 3.0f,
    -0.5f, 0.5f,-0.5f, 1.0f, 0.0f, 3.0f,
     0.5f,-0.5f,-0.5f, 0.0f, 1.0f, 3.0f,
    -0.5f, 0.5f,-0.5f, 1.0f, 0.0f, 3.0f,
     0.5f, 0.5f,-0.5f, 0.0f, 0.0f, 3.0f,
};

// ----------------------------------------------------------------------------
//  Vertex shader
//  Reconstruye la posicion del vertice como (vertice * escala + posicion de la
//  instancia) y decide, segun el identificador de cara, que capa del atlas leer
//  y con que factor de luz sombrearla.
// ----------------------------------------------------------------------------
const char* VERTEX_SHADER_SRC = R"glsl(
#version 330 core

layout(location = 0) in vec3  aPos;        // esquina del cubo unitario
layout(location = 1) in vec2  aUV;         // coordenada de textura de la cara
layout(location = 2) in float aFace;       // 0 arriba, 1 abajo, 2 lados X, 3 lados Z

layout(location = 3) in vec3  aInstPos;    // centro del bloque
layout(location = 4) in uint  aFaceTex;    // capas empaquetadas + brillo
layout(location = 5) in float aInstScale;  // escala de animacion 0..1

uniform mat4 uViewProj;
uniform mat4 uLightViewProj;
uniform int  uInsetSideLayer;   // capa lateral cuyo bloque no es un cubo completo

out vec2  vUV;
flat out float vLayer;
out float vShade;
out float vJitter;
out vec3 vNormal;
out vec3 vWorldPos;
out vec4 vLightSpacePosition;

void main() {
    // Desempaquetado de las tres capas del atlas.
    uint topLayer    =  aFaceTex        & 0xFFu;
    uint sideLayer   = (aFaceTex >> 8)  & 0xFFu;
    uint bottomLayer = (aFaceTex >> 16) & 0xFFu;

    // El cactus no ocupa el cubo completo. En el modelo del juego original sus
    // cuatro caras laterales estan metidas un pixel (1/16) hacia adentro y solo
    // la cara superior y la inferior conservan el tamano del bloque. La textura
    // lateral trae un borde transparente de un pixel a cada lado, de modo que la
    // parte opaca de cada cara termina justo donde empieza la cara perpendicular
    // y el contorno cierra. Sin este desplazamiento ese borde transparente deja
    // un hueco visible en cada arista vertical.
    vec3 localPos = aPos;
    if (aFace >= 1.5 && int(sideLayer) == uInsetSideLayer) {
        if (aFace < 2.5) localPos.x -= sign(aPos.x) * (1.0 / 16.0);
        else             localPos.z -= sign(aPos.z) * (1.0 / 16.0);
    }

    // Escala alrededor del centro del bloque: al aparecer crece de 0 a 1 y al
    // desarmarse encoge de 1 a 0.
    vec3 worldPos = localPos * aInstScale + aInstPos;
    gl_Position = uViewProj * vec4(worldPos, 1.0);
    vWorldPos = worldPos;
    vLightSpacePosition = uLightViewProj * vec4(worldPos, 1.0);

    if (aFace < 0.5)       vLayer = float(topLayer);
    else if (aFace < 1.5)  vLayer = float(bottomLayer);
    else                   vLayer = float(sideLayer);

    // Sombreado direccional fijo, igual al del juego original: la cara superior
    // recibe luz plena, las laterales menos y la inferior es la mas oscura.
    float faceShade;
    if (aFace < 0.5)       faceShade = 1.00;
    else if (aFace < 1.5)  faceShade = 0.50;
    else if (aFace < 2.5)  faceShade = 0.62;   // caras +-X
    else                   faceShade = 0.80;   // caras +-Z

    // Variacion pseudoaleatoria de brillo por bloque (byte alto de aFaceTex),
    // que rompe la uniformidad de las superficies grandes.
    float jitter = 0.93 + float((aFaceTex >> 24) & 0xFFu) * (0.14 / 255.0);

    vUV    = aUV;
    vShade = faceShade * jitter;
    vJitter = jitter;

    // La posicion local permite recuperar el signo de las caras laterales sin
    // aumentar el tamano de la geometria estatica del cubo.
    if (aFace < 0.5)       vNormal = vec3(0.0, 1.0, 0.0);
    else if (aFace < 1.5)  vNormal = vec3(0.0,-1.0, 0.0);
    else if (aFace < 2.5)  vNormal = vec3(sign(aPos.x), 0.0, 0.0);
    else                   vNormal = vec3(0.0, 0.0, sign(aPos.z));
}
)glsl";

// ----------------------------------------------------------------------------
//  Fragment shader
//  Una sola lectura del arreglo de texturas, el descarte de los pixeles
//  transparentes (hojas y cactus) y la multiplicacion por el factor de luz.
// ----------------------------------------------------------------------------
const char* FRAGMENT_SHADER_SRC = R"glsl(
#version 330 core

in vec2  vUV;
flat in float vLayer;
in float vShade;
in float vJitter;
in vec3 vNormal;
in vec3 vWorldPos;
in vec4 vLightSpacePosition;

uniform sampler2DArray uAtlas;
uniform int uDynamicLighting;
uniform vec3 uLightDirection;
uniform vec3 uDiffuseColor;
uniform vec3 uAmbientColor;
uniform vec3 uFogColor;
uniform vec3 uCameraPosition;
uniform float uFogDensity;
uniform int uShadows;
uniform sampler2DShadow uShadowMap;
uniform float uShadowStrength;
uniform vec3 uShadowCenter;
uniform float uShadowRadius;

out vec4 FragColor;

float shadowVisibility(vec3 normal) {
    vec3 projected = vLightSpacePosition.xyz / vLightSpacePosition.w;
    projected = projected * 0.5 + 0.5;
    if (projected.z <= 0.0 || projected.z >= 1.0 ||
        projected.x <= 0.0 || projected.x >= 1.0 ||
        projected.y <= 0.0 || projected.y >= 1.0) return 1.0;

    float slope = 1.0 - max(dot(normal, normalize(uLightDirection)), 0.0);
    float bias = max(0.00035, 0.0018 * slope);
    vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));
    float visible = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            visible += texture(uShadowMap,
                vec3(projected.xy + vec2(x, y) * texel, projected.z - bias));
        }
    }
    return visible / 9.0;
}

void main() {
    vec4 texel = texture(uAtlas, vec3(vUV, vLayer));
    if (texel.a < 0.5) discard;   // hojas y cactus tienen pixeles vacios

    if (uDynamicLighting == 0) {
        FragColor = vec4(texel.rgb * vShade, 1.0);
        return;
    }

    vec3 normal = normalize(vNormal);
    float diffuse = max(dot(normal, normalize(uLightDirection)), 0.0);
    float directVisibility = 1.0;
    if (uShadows != 0) {
        float distanceFromCamera = length(vWorldPos.xz - uShadowCenter.xz);
        float fade = 1.0 - smoothstep(uShadowRadius * 0.82,
                                     uShadowRadius, distanceFromCamera);
        directVisibility = mix(1.0, shadowVisibility(normal),
                               uShadowStrength * fade);
    }
    float skyBounce = 0.10 * max(normal.y, 0.0);
    vec3 illumination = uAmbientColor +
        uDiffuseColor * (diffuse * directVisibility + skyBounce);
    vec3 color = texel.rgb * vJitter * illumination;

    float distanceToCamera = length(vWorldPos - uCameraPosition);
    float fog = 1.0 - exp(-distanceToCamera * uFogDensity);
    fog = clamp(fog * fog, 0.0, 0.88);
    FragColor = vec4(mix(color, uFogColor, fog), 1.0);
}
)glsl";

const char* DEPTH_VERTEX_SHADER_SRC = R"glsl(
#version 330 core

layout(location = 0) in vec3  aPos;
layout(location = 3) in vec3  aInstPos;
layout(location = 5) in float aInstScale;
uniform mat4 uLightViewProj;

void main() {
    vec3 worldPos = aPos * aInstScale + aInstPos;
    gl_Position = uLightViewProj * vec4(worldPos, 1.0);
}
)glsl";

const char* DEPTH_FRAGMENT_SHADER_SRC = R"glsl(
#version 330 core
void main() {}
)glsl";

// Compila un shader individual e informa el error del compilador.
GLuint compileStage(GLenum type, const char* src, std::string& error) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint success = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        error = std::string("error compilando shader: ") + log;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

} // namespace

GLuint compileShaderProgram(const char* vertexSrc, const char* fragmentSrc, std::string& error) {
    error.clear();

    GLuint vs = compileStage(GL_VERTEX_SHADER, vertexSrc, error);
    if (vs == 0) return 0;

    GLuint fs = compileStage(GL_FRAGMENT_SHADER, fragmentSrc, error);
    if (fs == 0) { glDeleteShader(vs); return 0; }

    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    // Los shaders ya estan enlazados en el programa; sus objetos se liberan.
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint success = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[1024];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        error = std::string("error enlazando el programa de shaders: ") + log;
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

bool CubeRenderer::init(std::string& error) {
    program_ = compileShaderProgram(VERTEX_SHADER_SRC, FRAGMENT_SHADER_SRC, error);
    if (program_ == 0) return false;

    viewProjLoc_ = glGetUniformLocation(program_, "uViewProj");
    atlasLoc_    = glGetUniformLocation(program_, "uAtlas");
    dynamicLightingLoc_ = glGetUniformLocation(program_, "uDynamicLighting");
    lightDirectionLoc_  = glGetUniformLocation(program_, "uLightDirection");
    diffuseColorLoc_    = glGetUniformLocation(program_, "uDiffuseColor");
    ambientColorLoc_    = glGetUniformLocation(program_, "uAmbientColor");
    fogColorLoc_        = glGetUniformLocation(program_, "uFogColor");
    cameraPositionLoc_  = glGetUniformLocation(program_, "uCameraPosition");
    fogDensityLoc_      = glGetUniformLocation(program_, "uFogDensity");
    lightViewProjLoc_   = glGetUniformLocation(program_, "uLightViewProj");
    shadowsLoc_         = glGetUniformLocation(program_, "uShadows");
    shadowMapLoc_       = glGetUniformLocation(program_, "uShadowMap");
    shadowStrengthLoc_  = glGetUniformLocation(program_, "uShadowStrength");
    shadowCenterLoc_    = glGetUniformLocation(program_, "uShadowCenter");
    shadowRadiusLoc_    = glGetUniformLocation(program_, "uShadowRadius");
    insetSideLayerLoc_  = glGetUniformLocation(program_, "uInsetSideLayer");

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &cubeVbo_);
    glGenBuffers(1, &instanceVbo_);
    if (vao_ == 0 || cubeVbo_ == 0 || instanceVbo_ == 0) {
        error = "OpenGL no pudo crear el VAO o los buffers de vertices";
        return false;
    }

    glBindVertexArray(vao_);

    // --- Atributos por vertice (geometria estatica del cubo) ----------------
    glBindBuffer(GL_ARRAY_BUFFER, cubeVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(CUBE_VERTICES), CUBE_VERTICES, GL_STATIC_DRAW);

    const GLsizei vertexStride = kFloatsPerVertex * sizeof(float);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, vertexStride, (void*)(0));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, vertexStride, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, vertexStride, (void*)(5 * sizeof(float)));
    glEnableVertexAttribArray(2);

    // --- Atributos por instancia (un bloque cada uno) -----------------------
    glBindBuffer(GL_ARRAY_BUFFER, instanceVbo_);

    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(InstanceData),
                          (void*)offsetof(InstanceData, x));
    glEnableVertexAttribArray(3);
    glVertexAttribDivisor(3, 1);   // avanza una vez por instancia, no por vertice

    // faceTex es un entero sin signo: debe declararse con glVertexAttribIPointer
    // para que llegue al shader sin convertirse a punto flotante.
    glVertexAttribIPointer(4, 1, GL_UNSIGNED_INT, sizeof(InstanceData),
                           (void*)offsetof(InstanceData, faceTex));
    glEnableVertexAttribArray(4);
    glVertexAttribDivisor(4, 1);

    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(InstanceData),
                          (void*)offsetof(InstanceData, scale));
    glEnableVertexAttribArray(5);
    glVertexAttribDivisor(5, 1);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return true;
}

bool CubeRenderer::enableShadowPass(std::string& error) {
    if (depthProgram_ != 0) return true;
    depthProgram_ = compileShaderProgram(DEPTH_VERTEX_SHADER_SRC,
                                         DEPTH_FRAGMENT_SHADER_SRC, error);
    if (depthProgram_ == 0) return false;
    depthLightViewProjLoc_ = glGetUniformLocation(depthProgram_, "uLightViewProj");
    return true;
}

void CubeRenderer::draw(const InstanceData* instances, size_t count,
                        const glm::mat4& viewProj, GLuint atlasTex) {
    draw(instances, count, viewProj, atlasTex, SceneLighting{});
}

void CubeRenderer::draw(const InstanceData* instances, size_t count,
                        const glm::mat4& viewProj, GLuint atlasTex,
                        const SceneLighting& lighting) {
    lastUploadBytes_ = 0;
    if (instances == nullptr || count == 0) return;

    const size_t bytes = count * sizeof(InstanceData);

    glBindBuffer(GL_ARRAY_BUFFER, instanceVbo_);
    if (bytes > instanceCapacity_) {
        // Se reserva un 25% extra para no reasignar el buffer en cada fotograma
        // mientras el mundo va creciendo durante la fase de construccion.
        instanceCapacity_ = bytes + bytes / 4;
        glBufferData(GL_ARRAY_BUFFER, instanceCapacity_, nullptr, GL_STREAM_DRAW);
    }
    // "Orphaning": se descarta el contenido anterior para que el controlador no
    // tenga que esperar a que la GPU termine de leer el fotograma previo.
    glBufferData(GL_ARRAY_BUFFER, instanceCapacity_, nullptr, GL_STREAM_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, instances);
    lastUploadBytes_ = bytes;

    glUseProgram(program_);
    glUniformMatrix4fv(viewProjLoc_, 1, GL_FALSE, glm::value_ptr(viewProj));
    glUniform1i(insetSideLayerLoc_, insetSideLayer_);
    glUniform1i(dynamicLightingLoc_, lighting.dynamic ? 1 : 0);
    if (lighting.dynamic) {
        glUniform3fv(lightDirectionLoc_, 1, glm::value_ptr(lighting.direction));
        glUniform3fv(diffuseColorLoc_, 1, glm::value_ptr(lighting.diffuseColor));
        glUniform3fv(ambientColorLoc_, 1, glm::value_ptr(lighting.ambientColor));
        glUniform3fv(fogColorLoc_, 1, glm::value_ptr(lighting.fogColor));
        glUniform3fv(cameraPositionLoc_, 1, glm::value_ptr(lighting.cameraPosition));
        glUniform1f(fogDensityLoc_, lighting.fogDensity);
        glUniformMatrix4fv(lightViewProjLoc_, 1, GL_FALSE,
                           glm::value_ptr(lighting.lightViewProj));
        glUniform1i(shadowsLoc_, lighting.shadows ? 1 : 0);
        glUniform1f(shadowStrengthLoc_, lighting.shadowStrength);
        glUniform3fv(shadowCenterLoc_, 1, glm::value_ptr(lighting.shadowCenter));
        glUniform1f(shadowRadiusLoc_, lighting.shadowRadius);
        if (lighting.shadows) {
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, lighting.shadowTexture);
            glUniform1i(shadowMapLoc_, 2);
        }
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, atlasTex);
    glUniform1i(atlasLoc_, 0);

    glBindVertexArray(vao_);
    glDrawArraysInstanced(GL_TRIANGLES, 0, kVerticesPerCube,
                          static_cast<GLsizei>(count));
    glBindVertexArray(0);
}

void CubeRenderer::drawDepth(const InstanceData* instances, size_t count,
                             const glm::mat4& lightViewProj) {
    if (instances == nullptr || count == 0 || depthProgram_ == 0) return;

    const size_t bytes = count * sizeof(InstanceData);
    glBindBuffer(GL_ARRAY_BUFFER, instanceVbo_);
    if (bytes > instanceCapacity_) {
        instanceCapacity_ = bytes + bytes / 4;
        glBufferData(GL_ARRAY_BUFFER, instanceCapacity_, nullptr, GL_STREAM_DRAW);
    }
    glBufferData(GL_ARRAY_BUFFER, instanceCapacity_, nullptr, GL_STREAM_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, instances);

    glUseProgram(depthProgram_);
    glUniformMatrix4fv(depthLightViewProjLoc_, 1, GL_FALSE,
                       glm::value_ptr(lightViewProj));
    glBindVertexArray(vao_);
    glDrawArraysInstanced(GL_TRIANGLES, 0, kVerticesPerCube,
                          static_cast<GLsizei>(count));
    glBindVertexArray(0);
}

void CubeRenderer::destroy() {
    if (instanceVbo_ != 0) { glDeleteBuffers(1, &instanceVbo_); instanceVbo_ = 0; }
    if (cubeVbo_     != 0) { glDeleteBuffers(1, &cubeVbo_);     cubeVbo_     = 0; }
    if (vao_         != 0) { glDeleteVertexArrays(1, &vao_);    vao_         = 0; }
    if (program_     != 0) { glDeleteProgram(program_);         program_     = 0; }
    if (depthProgram_ != 0) { glDeleteProgram(depthProgram_); depthProgram_ = 0; }
    viewProjLoc_ = atlasLoc_ = -1;
    dynamicLightingLoc_ = lightDirectionLoc_ = diffuseColorLoc_ = -1;
    ambientColorLoc_ = fogColorLoc_ = cameraPositionLoc_ = fogDensityLoc_ = -1;
    lightViewProjLoc_ = shadowsLoc_ = shadowMapLoc_ = shadowStrengthLoc_ = -1;
    shadowCenterLoc_ = shadowRadiusLoc_ = -1;
    depthLightViewProjLoc_ = -1;
    instanceCapacity_ = 0;
}

} // namespace mcss
