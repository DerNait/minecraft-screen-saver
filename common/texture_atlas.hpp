// ============================================================================
//  texture_atlas.hpp - Atlas de texturas como arreglo de texturas de OpenGL
// ----------------------------------------------------------------------------
//  Todas las texturas de bloque se cargan en un unico GL_TEXTURE_2D_ARRAY: un
//  arreglo de imagenes del mismo tamano donde cada "capa" es la textura de una
//  cara. Esto permite dibujar el mundo entero con una sola llamada de dibujo
//  instanciada, porque el shader elige la capa por indice en vez de obligar a
//  cambiar de textura entre bloques.
//
//  El atlas tambien resuelve en CPU la composicion de overlays y el tinte por
//  bioma, de modo que el fragment shader solo hace una lectura de textura.
// ============================================================================
#pragma once

#include <GL/glew.h>

#include <string>
#include <unordered_map>
#include <vector>

#include "block_catalog.hpp"
#include "png_loader.hpp"

namespace mcss {

class TextureAtlas {
public:
    // Tamano en pixeles al que se normaliza toda textura del atlas.
    static constexpr int kTileSize = 16;

    // ------------------------------------------------------------------------
    //  registerTexture
    //  Entradas: spec - descripcion de la textura (base, overlay y tinte)
    //  Salida  : indice de la capa asignada dentro del arreglo de texturas
    //  Descripcion: si una especificacion identica ya fue registrada devuelve la
    //  misma capa, evitando duplicar imagenes en memoria de video.
    //  Solo debe llamarse antes de upload().
    // ------------------------------------------------------------------------
    int registerTexture(const TextureSpec& spec);

    // ------------------------------------------------------------------------
    //  build
    //  Entradas: assetsDir - carpeta que contiene los archivos .png
    //  Salidas : error     - motivo de la falla si retorna false
    //  Retorno : true si todas las capas registradas se pudieron componer
    //  Descripcion: decodifica los PNG, aplica overlays y tintes, y arma el
    //  bloque de pixeles contiguo que se subira a la GPU. No toca OpenGL.
    // ------------------------------------------------------------------------
    bool build(const std::string& assetsDir, std::string& error);

    // ------------------------------------------------------------------------
    //  upload
    //  Salidas : error - motivo de la falla si retorna false
    //  Retorno : true si la textura quedo creada en la GPU
    //  Descripcion: sube el resultado de build() como GL_TEXTURE_2D_ARRAY con
    //  filtrado NEAREST (para conservar el aspecto de pixel art) y mipmaps
    //  (para que los bloques lejanos no titilen). Debe llamarse desde el hilo
    //  que posee el contexto de OpenGL.
    // ------------------------------------------------------------------------
    bool upload(std::string& error);

    // Libera la textura de la GPU. Es seguro llamarla mas de una vez.
    void destroy();

    // Identificador de la textura de OpenGL (0 si aun no se subio).
    GLuint textureId() const { return textureId_; }

    // Cantidad de capas registradas.
    int layerCount() const { return static_cast<int>(specs_.size()); }

    // Memoria ocupada por los pixeles del atlas en RAM, en bytes.
    size_t pixelBytes() const { return pixels_.size(); }

private:
    // Genera la clave de deduplicacion de una especificacion de textura.
    static std::string makeKey(const TextureSpec& spec);

    // Carga un PNG desde disco reutilizando la cache, y lo normaliza a
    // kTileSize x kTileSize (tomando el primer fotograma si la imagen es una
    // tira de animacion vertical).
    bool loadTile(const std::string& assetsDir, const char* name,
                  Image& out, std::string& error);

    std::vector<TextureSpec>               specs_;    // una entrada por capa
    std::unordered_map<std::string, int>   lookup_;   // clave -> indice de capa
    std::unordered_map<std::string, Image> cache_;    // PNG ya decodificados
    std::vector<unsigned char>             pixels_;   // capas contiguas en RGBA8
    GLuint                                 textureId_ = 0;
};

// ----------------------------------------------------------------------------
//  findAssetsDirectory
//  Entradas: hint - ruta indicada por el usuario, o cadena vacia
//  Salida  : ruta de la carpeta de texturas encontrada, o cadena vacia
//  Descripcion: si el usuario no indica una ruta, busca "assets/textures" en el
//  directorio actual y en varios niveles hacia arriba, de modo que el programa
//  funcione tanto si se ejecuta desde la raiz del proyecto como desde build/.
// ----------------------------------------------------------------------------
std::string findAssetsDirectory(const std::string& hint);

} // namespace mcss
