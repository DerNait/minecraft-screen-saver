// ============================================================================
//  mob_renderer.hpp - Renderizador instanciado de mobs cubicos
// ----------------------------------------------------------------------------
//  Dibuja distintos modelos de Minecraft con sus texturas originales de
//  entidad. OpenGL permanece en el hilo principal; la simulacion solo llena el
//  arreglo compacto de MobInstanceData que se transfiere a la GPU.
// ============================================================================
#pragma once

#include <GL/glew.h>
#include <glm/glm.hpp>

#include <cstddef>
#include <string>

namespace mcss {

struct MobInstanceData {
    float x, y, z; // posicion de los pies en coordenadas del mundo
    float yaw;     // orientacion horizontal en radianes
    float bob;     // desplazamiento vertical del salto
};

enum class MobModel {
    Zombie,    // conservado como referencia, no forma parte de la poblacion
    Pig,
    Cow,
    Sheep,
    SheepWool  // segunda capa ligeramente expandida de la oveja
};

class MobRenderer {
public:
    bool init(MobModel model, const std::string& texturePath, std::string& error);
    void draw(const MobInstanceData* instances, size_t count,
              const glm::mat4& viewProj);
    void destroy();

private:
    GLuint vao_         = 0;
    GLuint vertexVbo_   = 0;
    GLuint instanceVbo_ = 0;
    GLuint texture_     = 0;
    GLuint program_     = 0;
    GLint viewProjLoc_  = -1;
    GLint textureLoc_   = -1;
    GLsizei vertexCount_ = 0;
    size_t instanceCapacity_ = 0;
};

} // namespace mcss
