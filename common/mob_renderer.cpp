// ============================================================================
//  mob_renderer.cpp - Modelos de entidades y sus texturas
// ============================================================================
#include "mob_renderer.hpp"

#include "png_loader.hpp"
#include "render.hpp"

#include <glm/gtc/type_ptr.hpp>

#include <cstddef>
#include <vector>

namespace mcss {
namespace {

struct MobVertex {
    float x, y, z;
    float u, v;
    float nx, ny, nz;
};

void appendQuad(std::vector<MobVertex>& vertices,
                const glm::vec3& a, const glm::vec3& b,
                const glm::vec3& c, const glm::vec3& d,
                const glm::vec3& normal,
                float u0, float v0, float u1, float v1) {
    vertices.push_back({a.x, a.y, a.z, u0, v1, normal.x, normal.y, normal.z});
    vertices.push_back({b.x, b.y, b.z, u1, v1, normal.x, normal.y, normal.z});
    vertices.push_back({c.x, c.y, c.z, u1, v0, normal.x, normal.y, normal.z});
    vertices.push_back({a.x, a.y, a.z, u0, v1, normal.x, normal.y, normal.z});
    vertices.push_back({c.x, c.y, c.z, u1, v0, normal.x, normal.y, normal.z});
    vertices.push_back({d.x, d.y, d.z, u0, v0, normal.x, normal.y, normal.z});
}

// Agrega un cuboide usando el desplegado UV tradicional de los modelos de
// Minecraft. u,v,tw,th,td se expresan en pixeles de una piel de 64x64.
void appendBox(std::vector<MobVertex>& vertices,
               float cx, float cy, float cz,
               float sx, float sy, float sz,
               int u, int v, int tw, int th, int td,
               int textureWidth, int textureHeight) {
    const float x0 = cx - sx * 0.5f, x1 = cx + sx * 0.5f;
    const float y0 = cy - sy * 0.5f, y1 = cy + sy * 0.5f;
    const float z0 = cz - sz * 0.5f, z1 = cz + sz * 0.5f;
    const float invW = 1.0f / static_cast<float>(textureWidth);
    const float invH = 1.0f / static_cast<float>(textureHeight);

    const auto U = [invW](int px) { return px * invW; };
    const auto V = [invH](int py) { return py * invH; };

    // Superior e inferior.
    appendQuad(vertices,
               {x0,y1,z1}, {x1,y1,z1}, {x1,y1,z0}, {x0,y1,z0}, {0,1,0},
               U(u + td), V(v), U(u + td + tw), V(v + td));
    appendQuad(vertices,
               {x0,y0,z0}, {x1,y0,z0}, {x1,y0,z1}, {x0,y0,z1}, {0,-1,0},
               U(u + td + tw), V(v), U(u + td + tw * 2), V(v + td));

    const int sideTop = v + td;
    const int sideBottom = sideTop + th;

    // +X, -Z, -X y +Z ocupan las cuatro franjas laterales del desplegado.
    // En los modelos de Minecraft la cara frontal de los cuadrupedos mira hacia
    // -Z; mantener este orden evita mostrar la nuca en lugar de ojos y hocico.
    appendQuad(vertices,
               {x1,y0,z1}, {x1,y0,z0}, {x1,y1,z0}, {x1,y1,z1}, {1,0,0},
               U(u), V(sideTop), U(u + td), V(sideBottom));
    appendQuad(vertices,
               {x0,y0,z1}, {x1,y0,z1}, {x1,y1,z1}, {x0,y1,z1}, {0,0,1},
               U(u + td * 2 + tw), V(sideTop),
               U(u + td * 2 + tw * 2), V(sideBottom));
    appendQuad(vertices,
               {x0,y0,z0}, {x0,y0,z1}, {x0,y1,z1}, {x0,y1,z0}, {-1,0,0},
               U(u + td + tw), V(sideTop), U(u + td * 2 + tw), V(sideBottom));
    appendQuad(vertices,
               {x1,y0,z0}, {x0,y0,z0}, {x0,y1,z0}, {x1,y1,z0}, {0,0,-1},
               U(u + td), V(sideTop), U(u + td + tw), V(sideBottom));
}

// Rota solo la geometria agregada desde "begin". Los UV permanecen en la
// orientacion original del cubo, como ocurre con el cuerpo de los cuadrupedos.
void rotateRecentX90(std::vector<MobVertex>& vertices, size_t begin,
                     float cy, float cz) {
    for (size_t i = begin; i < vertices.size(); ++i) {
        MobVertex& vertex = vertices[i];
        const float localY = vertex.y - cy;
        const float localZ = vertex.z - cz;
        vertex.y = cy - localZ;
        vertex.z = cz + localY;

        const float normalY = vertex.ny;
        const float normalZ = vertex.nz;
        vertex.ny = -normalZ;
        vertex.nz = normalY;
    }
}

void appendRotatedBody(std::vector<MobVertex>& vertices,
                       float cx, float cy, float cz,
                       float sx, float sy, float sz,
                       int u, int v, int tw, int th, int td,
                       int textureWidth, int textureHeight) {
    const size_t begin = vertices.size();
    appendBox(vertices, cx, cy, cz, sx, sy, sz,
              u, v, tw, th, td, textureWidth, textureHeight);
    rotateRecentX90(vertices, begin, cy, cz);
}

std::vector<MobVertex> buildZombieMesh(int textureWidth, int textureHeight) {
    std::vector<MobVertex> vertices;
    vertices.reserve(6 * 36);

    // Medidas en bloques; los parametros UV conservan las dimensiones en
    // pixeles del modelo original de Minecraft.
    appendBox(vertices,  0.000f, 1.750f, 0.0f, 0.50f, 0.50f, 0.50f,
               0,  0, 8,  8, 8, textureWidth, textureHeight); // cabeza
    appendBox(vertices,  0.000f, 1.125f, 0.0f, 0.50f, 0.75f, 0.25f,
              16, 16, 8, 12, 4, textureWidth, textureHeight); // torso

    // La piel clasica del zombie guarda una sola extremidad. Ambos brazos y
    // ambas piernas son cubos distintos que reutilizan el mismo rectangulo UV.
    appendBox(vertices, -0.375f, 1.125f, 0.0f, 0.25f, 0.75f, 0.25f,
              40, 16, 4, 12, 4, textureWidth, textureHeight);
    appendBox(vertices,  0.375f, 1.125f, 0.0f, 0.25f, 0.75f, 0.25f,
              40, 16, 4, 12, 4, textureWidth, textureHeight);
    appendBox(vertices, -0.125f, 0.375f, 0.0f, 0.25f, 0.75f, 0.25f,
               0, 16, 4, 12, 4, textureWidth, textureHeight);
    appendBox(vertices,  0.125f, 0.375f, 0.0f, 0.25f, 0.75f, 0.25f,
               0, 16, 4, 12, 4, textureWidth, textureHeight);
    return vertices;
}

std::vector<MobVertex> buildPigMesh(int textureWidth, int textureHeight) {
    std::vector<MobVertex> vertices;
    vertices.reserve(7 * 36);

    appendBox(vertices, 0.0f, 0.75f, -0.6875f, 0.50f, 0.50f, 0.50f,
               0, 0, 8, 8, 8, textureWidth, textureHeight);                // cabeza
    appendBox(vertices, 0.0f, 0.65625f, -0.96875f, 0.25f, 0.1875f, 0.0625f,
              16, 16, 4, 3, 1, textureWidth, textureHeight);              // hocico

    // El atlas define el cuerpo erguido (10x16x8) y el modelo lo rota 90 grados.
    appendRotatedBody(vertices, 0.0f, 0.75f, 0.0f, 0.625f, 1.0f, 0.50f,
                      28, 8, 10, 16, 8, textureWidth, textureHeight);

    const float legX = 0.1875f, legZ = 0.375f;
    for (int zSign : {-1, 1}) {
        for (int xSign : {-1, 1}) {
            appendBox(vertices, xSign * legX, 0.1875f, zSign * legZ,
                      0.25f, 0.375f, 0.25f,
                      0, 16, 4, 6, 4, textureWidth, textureHeight);
        }
    }
    return vertices;
}

std::vector<MobVertex> buildCowMesh(int textureWidth, int textureHeight) {
    std::vector<MobVertex> vertices;
    vertices.reserve(10 * 36);

    appendBox(vertices, 0.0f, 1.25f, -0.75f, 0.50f, 0.50f, 0.375f,
               0, 0, 8, 8, 6, textureWidth, textureHeight);               // cabeza
    appendBox(vertices, 0.0f, 1.09375f, -0.96875f, 0.375f, 0.1875f, 0.0625f,
               1, 33, 6, 3, 1, textureWidth, textureHeight);              // hocico
    appendBox(vertices, -0.28125f, 1.59375f, -0.75f, 0.0625f, 0.1875f, 0.0625f,
              22, 0, 1, 3, 1, textureWidth, textureHeight);               // cuerno
    appendBox(vertices,  0.28125f, 1.59375f, -0.75f, 0.0625f, 0.1875f, 0.0625f,
              22, 0, 1, 3, 1, textureWidth, textureHeight);               // cuerno reutilizado

    appendRotatedBody(vertices, 0.0f, 1.0625f, 0.0f, 0.75f, 1.125f, 0.625f,
                      18, 4, 12, 18, 10, textureWidth, textureHeight);
    appendRotatedBody(vertices, 0.0f, 0.71875f, 0.0f, 0.25f, 0.375f, 0.0625f,
                      52, 0, 4, 6, 1, textureWidth, textureHeight);        // ubre

    const float legX = 0.25f, legZ = 0.40625f;
    for (int zSign : {-1, 1}) {
        for (int xSign : {-1, 1}) {
            appendBox(vertices, xSign * legX, 0.375f, zSign * legZ,
                      0.25f, 0.75f, 0.25f,
                      0, 16, 4, 12, 4, textureWidth, textureHeight);
        }
    }
    return vertices;
}

std::vector<MobVertex> buildSheepMesh(int textureWidth, int textureHeight,
                                      bool woolLayer) {
    std::vector<MobVertex> vertices;
    vertices.reserve(6 * 36);
    const float inflate = woolLayer ? 0.07f : 0.0f;

    // La lana forma una capucha mas corta que la cabeza base. Si ambas capas
    // llegan hasta el mismo plano frontal, la lana tapa ojos y hocico y hace
    // parecer que la cara esta orientada hacia abajo.
    const float headCenterZ = woolLayer ? -0.50f : -0.625f;
    const float headHalfDepth = woolLayer ? 0.375f + inflate : 0.50f;
    appendBox(vertices, 0.0f, 1.1875f, headCenterZ,
              0.375f + inflate, 0.375f + inflate, headHalfDepth,
              0, 0, 6, 6, 8, textureWidth, textureHeight);
    appendRotatedBody(vertices, 0.0f, 0.9375f, 0.0f,
                      0.50f + inflate, 1.0f + inflate, 0.375f + inflate,
                      28, 8, 8, 16, 6, textureWidth, textureHeight);

    const float legX = 0.1875f, legZ = 0.375f;
    const float legInflate = woolLayer ? 0.035f : 0.0f;
    for (int zSign : {-1, 1}) {
        for (int xSign : {-1, 1}) {
            appendBox(vertices, xSign * legX, 0.375f, zSign * legZ,
                      0.25f + legInflate, 0.75f, 0.25f + legInflate,
                      0, 16, 4, 12, 4, textureWidth, textureHeight);
        }
    }
    return vertices;
}

std::vector<MobVertex> buildMobMesh(MobModel model,
                                    int textureWidth, int textureHeight) {
    switch (model) {
        case MobModel::Pig:       return buildPigMesh(textureWidth, textureHeight);
        case MobModel::Cow:       return buildCowMesh(textureWidth, textureHeight);
        case MobModel::Sheep:     return buildSheepMesh(textureWidth, textureHeight, false);
        case MobModel::SheepWool: return buildSheepMesh(textureWidth, textureHeight, true);
        case MobModel::Zombie:
        default:                  return buildZombieMesh(textureWidth, textureHeight);
    }
}

const char* MOB_VERTEX_SHADER = R"glsl(
#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec3 aNormal;
layout(location = 3) in vec3 aInstPos;
layout(location = 4) in float aYaw;
layout(location = 5) in float aBob;

uniform mat4 uViewProj;

out vec2 vUV;
out float vShade;

void main() {
    float c = cos(aYaw);
    float s = sin(aYaw);
    vec3 local = vec3(c * aPos.x + s * aPos.z,
                      aPos.y + aBob,
                     -s * aPos.x + c * aPos.z);
    vec3 normal = normalize(vec3(c * aNormal.x + s * aNormal.z,
                                 aNormal.y,
                                -s * aNormal.x + c * aNormal.z));
    vec3 worldPos = local + aInstPos;
    gl_Position = uViewProj * vec4(worldPos, 1.0);

    vec3 lightDir = normalize(vec3(0.45, 1.0, 0.30));
    vShade = 0.48 + 0.52 * max(dot(normal, lightDir), 0.0);
    vUV = aUV;
}
)glsl";

const char* MOB_FRAGMENT_SHADER = R"glsl(
#version 330 core

in vec2 vUV;
in float vShade;
uniform sampler2D uTexture;
out vec4 FragColor;

void main() {
    vec4 texel = texture(uTexture, vUV);
    if (texel.a < 0.5) discard;
    FragColor = vec4(texel.rgb * vShade, texel.a);
}
)glsl";

} // namespace

bool MobRenderer::init(MobModel model, const std::string& texturePath, std::string& error) {
    error.clear();
    destroy();

    Image skin;
    if (!loadPNG(texturePath, skin, error)) return false;
    if (skin.width != 64 || (skin.height != 32 && skin.height != 64)) {
        error = "la textura del mob debe medir 64x32 o 64x64: " + texturePath;
        return false;
    }

    program_ = compileShaderProgram(MOB_VERTEX_SHADER, MOB_FRAGMENT_SHADER, error);
    if (program_ == 0) return false;

    std::vector<MobVertex> vertices = buildMobMesh(model, skin.width, skin.height);
    vertexCount_ = static_cast<GLsizei>(vertices.size());

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vertexVbo_);
    glGenBuffers(1, &instanceVbo_);
    glBindVertexArray(vao_);

    glBindBuffer(GL_ARRAY_BUFFER, vertexVbo_);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(MobVertex),
                 vertices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(MobVertex),
                          reinterpret_cast<void*>(offsetof(MobVertex, x)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(MobVertex),
                          reinterpret_cast<void*>(offsetof(MobVertex, u)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(MobVertex),
                          reinterpret_cast<void*>(offsetof(MobVertex, nx)));
    glEnableVertexAttribArray(2);

    glBindBuffer(GL_ARRAY_BUFFER, instanceVbo_);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(MobInstanceData),
                          reinterpret_cast<void*>(offsetof(MobInstanceData, x)));
    glEnableVertexAttribArray(3);
    glVertexAttribDivisor(3, 1);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(MobInstanceData),
                          reinterpret_cast<void*>(offsetof(MobInstanceData, yaw)));
    glEnableVertexAttribArray(4);
    glVertexAttribDivisor(4, 1);
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(MobInstanceData),
                          reinterpret_cast<void*>(offsetof(MobInstanceData, bob)));
    glEnableVertexAttribArray(5);
    glVertexAttribDivisor(5, 1);
    glBindVertexArray(0);

    glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, skin.width, skin.height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, skin.pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);

    viewProjLoc_ = glGetUniformLocation(program_, "uViewProj");
    textureLoc_ = glGetUniformLocation(program_, "uTexture");
    if (glGetError() != GL_NO_ERROR) {
        error = "OpenGL no pudo preparar el renderizador de mobs";
        destroy();
        return false;
    }
    return true;
}

void MobRenderer::draw(const MobInstanceData* instances, size_t count,
                       const glm::mat4& viewProj) {
    if (instances == nullptr || count == 0 || program_ == 0) return;

    const size_t bytes = count * sizeof(MobInstanceData);
    glBindBuffer(GL_ARRAY_BUFFER, instanceVbo_);
    if (bytes > instanceCapacity_) {
        instanceCapacity_ = bytes + bytes / 4;
        glBufferData(GL_ARRAY_BUFFER, instanceCapacity_, nullptr, GL_STREAM_DRAW);
    }
    glBufferData(GL_ARRAY_BUFFER, instanceCapacity_, nullptr, GL_STREAM_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, instances);

    glUseProgram(program_);
    glUniformMatrix4fv(viewProjLoc_, 1, GL_FALSE, glm::value_ptr(viewProj));
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glUniform1i(textureLoc_, 1);
    glBindVertexArray(vao_);
    glDrawArraysInstanced(GL_TRIANGLES, 0, vertexCount_,
                          static_cast<GLsizei>(count));
    glBindVertexArray(0);
}

void MobRenderer::destroy() {
    if (texture_ != 0) glDeleteTextures(1, &texture_);
    if (instanceVbo_ != 0) glDeleteBuffers(1, &instanceVbo_);
    if (vertexVbo_ != 0) glDeleteBuffers(1, &vertexVbo_);
    if (vao_ != 0) glDeleteVertexArrays(1, &vao_);
    if (program_ != 0) glDeleteProgram(program_);
    texture_ = instanceVbo_ = vertexVbo_ = vao_ = program_ = 0;
    viewProjLoc_ = textureLoc_ = -1;
    vertexCount_ = 0;
    instanceCapacity_ = 0;
}

} // namespace mcss
