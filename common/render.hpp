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

    // Libera VAO, VBOs y el programa de shaders. Es seguro llamarla dos veces.
    void destroy();

    // Bytes transferidos a la GPU en el ultimo draw(), util para el diagnostico.
    size_t lastUploadBytes() const { return lastUploadBytes_; }

private:
    GLuint vao_          = 0;  // descriptor de atributos
    GLuint cubeVbo_      = 0;  // geometria estatica del cubo unitario
    GLuint instanceVbo_  = 0;  // datos por instancia, reescritos cada fotograma
    GLuint program_      = 0;  // programa de shaders
    GLint  viewProjLoc_  = -1; // ubicacion del uniform de la matriz
    GLint  atlasLoc_     = -1; // ubicacion del uniform del muestreador
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
