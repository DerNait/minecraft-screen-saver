// ============================================================================
//  render.hpp - Renderizador instanciado de cubos
// ----------------------------------------------------------------------------
//  Dibuja el mundo completo con UNA sola llamada glDrawArraysInstanced: la
//  geometria del cubo unitario se sube una vez y por cada bloque solo se envian
//  20 bytes (posicion, indices de textura y escala). El shader reconstruye la
//  posicion final sumando el vertice del cubo a la posicion de la instancia, lo
//  que evita transmitir una matriz de 64 bytes por bloque.
//
//  IMPORTANTE PARA LA VERSION PARALELA: todas las funciones de esta clase hacen
//  llamadas a OpenGL y deben ejecutarse siempre desde el hilo que posee el
//  contexto (el hilo principal). Los hilos de OpenMP solo pueden LLENAR el
//  vector de InstanceData; la subida a la GPU y el dibujo quedan fuera de la
//  region paralela.
// ============================================================================
#pragma once

#include <GL/glew.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace mcss {

// ----------------------------------------------------------------------------
//  InstanceData - datos que se envian a la GPU por cada bloque visible.
//  Ocupa 20 bytes; mantenerlo compacto es critico porque este bloque de memoria
//  se reconstruye y se transfiere completo en cada fotograma.
// ----------------------------------------------------------------------------
struct InstanceData {
    float    x, y, z;    // centro del bloque en coordenadas de mundo
    uint32_t faceTex;    // capas del atlas empaquetadas: top | side<<8 | bottom<<16 | brillo<<24
    float    scale;      // factor de escala 0..1, usado en la aparicion y el desarmado
};

// Empaqueta los tres indices de capa del atlas y la variacion de brillo en el
// entero de 32 bits que consume el shader.
inline uint32_t packFaceTex(int topLayer, int sideLayer, int bottomLayer, uint8_t shade) {
    return (static_cast<uint32_t>(topLayer)    & 0xFFu)
         | ((static_cast<uint32_t>(sideLayer)  & 0xFFu) << 8)
         | ((static_cast<uint32_t>(bottomLayer)& 0xFFu) << 16)
         | (static_cast<uint32_t>(shade) << 24);
}

// Estado compartido por los shaders del terreno y los mobs. Cuando dynamic es
// false se conserva exactamente el sombreado clasico por orientacion de cara.
struct SceneLighting {
    bool      dynamic = false;
    glm::vec3 direction{0.45f, 1.0f, 0.30f}; // desde la superficie hacia el astro
    glm::vec3 diffuseColor{1.0f, 1.0f, 1.0f};
    glm::vec3 ambientColor{1.0f, 1.0f, 1.0f};
    glm::vec3 fogColor{0.53f, 0.81f, 0.92f};
    glm::vec3 cameraPosition{0.0f};
    float     fogDensity = 0.0f;
    bool      shadows = false;
    glm::mat4 lightViewProj{1.0f};
    GLuint    shadowTexture = 0;
    float     shadowStrength = 0.68f;
    glm::vec3 shadowCenter{0.0f};
    float     shadowRadius = 1.0f;
};

class CubeRenderer {
public:
    // ------------------------------------------------------------------------
    //  init
    //  Salidas : error - motivo de la falla si retorna false
    //  Retorno : true si el shader y los buffers quedaron listos
    //  Descripcion: compila el programa de shaders, sube la geometria del cubo
    //  unitario y configura el VAO con los atributos por vertice y por instancia.
    // ------------------------------------------------------------------------
    bool init(std::string& error);

    // Compila el pase de profundidad solo cuando la version paralela activa
    // sombras. El ejecutable secuencial no paga este costo de inicializacion.
    bool enableShadowPass(std::string& error);

    // ------------------------------------------------------------------------
    //  draw
    //  Entradas: instances - arreglo de bloques a dibujar en este fotograma
    //            count     - cuantos elementos del arreglo son validos
    //            viewProj  - matriz combinada de vista y proyeccion
    //            atlasTex  - identificador del GL_TEXTURE_2D_ARRAY del atlas
    //  Descripcion: transfiere el arreglo de instancias y emite un unico
    //  glDrawArraysInstanced con 36 vertices por bloque.
    // ------------------------------------------------------------------------
    void draw(const InstanceData* instances, size_t count,
              const glm::mat4& viewProj, GLuint atlasTex);

    // Variante usada por la version paralela para compartir el sol, la luz
    // ambiental y la neblina del ciclo dia/noche con todos los objetos.
    void draw(const InstanceData* instances, size_t count,
              const glm::mat4& viewProj, GLuint atlasTex,
              const SceneLighting& lighting);

    void drawDepth(const InstanceData* instances, size_t count,
                   const glm::mat4& lightViewProj);

    // ------------------------------------------------------------------------
    //  setInsetSideLayer
    //  Entradas: layer - capa del atlas de la textura lateral afectada, o -1
    //  Descripcion: marca un tipo de bloque cuyo modelo NO es un cubo completo.
    //  El cactus del juego original tiene sus cuatro caras laterales metidas un
    //  pixel (1/16) hacia adentro, mientras que la cara superior y la inferior
    //  conservan el tamano del bloque. Sus texturas traen un borde transparente
    //  de un pixel que encaja exactamente con ese desplazamiento; dibujado como
    //  cubo completo, ese borde deja un hueco visible en cada arista vertical.
    // ------------------------------------------------------------------------
    void setInsetSideLayer(int layer) { insetSideLayer_ = layer; }

    // Libera VAO, VBOs y el programa de shaders. Es seguro llamarla dos veces.
    void destroy();

    // Bytes transferidos a la GPU en el ultimo draw(), util para el diagnostico.
    size_t lastUploadBytes() const { return lastUploadBytes_; }

private:
    GLuint vao_          = 0;  // descriptor de atributos
    GLuint cubeVbo_      = 0;  // geometria estatica del cubo unitario
    GLuint instanceVbo_  = 0;  // datos por instancia, reescritos cada fotograma
    GLuint program_      = 0;  // programa de shaders
    GLuint depthProgram_ = 0;  // programa opcional del mapa de sombras
    GLint  viewProjLoc_  = -1; // ubicacion del uniform de la matriz
    GLint  atlasLoc_     = -1; // ubicacion del uniform del muestreador
    GLint  dynamicLightingLoc_ = -1;
    GLint  lightDirectionLoc_  = -1;
    GLint  diffuseColorLoc_    = -1;
    GLint  ambientColorLoc_    = -1;
    GLint  fogColorLoc_        = -1;
    GLint  cameraPositionLoc_  = -1;
    GLint  fogDensityLoc_      = -1;
    GLint  lightViewProjLoc_   = -1;
    GLint  shadowsLoc_         = -1;
    GLint  shadowMapLoc_       = -1;
    GLint  shadowStrengthLoc_  = -1;
    GLint  shadowCenterLoc_    = -1;
    GLint  shadowRadiusLoc_    = -1;
    GLint  depthLightViewProjLoc_ = -1;
    GLint  insetSideLayerLoc_ = -1;   // uniform de la capa con caras metidas
    int    insetSideLayer_    = -1;   // -1 = todos los bloques son cubos completos
    size_t instanceCapacity_ = 0;  // capacidad reservada del buffer de instancias
    size_t lastUploadBytes_  = 0;
};

// ----------------------------------------------------------------------------
//  compileShaderProgram
//  Entradas: vertexSrc, fragmentSrc - codigo fuente GLSL
//  Salidas : error - registro del compilador o enlazador si algo falla
//  Retorno : identificador del programa, o 0 si hubo error
//  Descripcion: utilidad independiente para compilar y enlazar un par de
//  shaders informando el error exacto en vez de terminar el proceso.
// ----------------------------------------------------------------------------
GLuint compileShaderProgram(const char* vertexSrc, const char* fragmentSrc, std::string& error);

} // namespace mcss
