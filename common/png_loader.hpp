// ============================================================================
//  png_loader.hpp - Decodificador PNG propio (Minecraft Screen Saver)
// ----------------------------------------------------------------------------
//  Las texturas de Minecraft vienen en varios formatos de PNG (paleta de 2, 4
//  y 8 bits, escala de grises, RGB y RGBA). En lugar de depender de una
//  libreria externa de imagenes, aqui se implementa un decodificador propio
//  que cubre todos los tipos de color del estandar PNG sin entrelazado, y que
//  entrega siempre el resultado normalizado a RGBA de 8 bits por canal.
//  La descompresion del bloque DEFLATE se delega a zlib.
// ============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mcss {

// Imagen decodificada en memoria, siempre en formato RGBA8.
struct Image {
    int width  = 0;                      // ancho en pixeles
    int height = 0;                      // alto en pixeles
    std::vector<unsigned char> pixels;   // RGBA8, fila superior primero, sin padding

    // Indica si la imagen tiene contenido coherente con sus dimensiones.
    bool valid() const {
        return width > 0 && height > 0 &&
               pixels.size() == static_cast<size_t>(width) * height * 4;
    }

    // Acceso de solo lectura al pixel (x, y). Devuelve el puntero a sus 4 bytes.
    const unsigned char* pixelAt(int x, int y) const {
        return pixels.data() + (static_cast<size_t>(y) * width + x) * 4;
    }

    // Acceso de escritura al pixel (x, y).
    unsigned char* pixelAt(int x, int y) {
        return pixels.data() + (static_cast<size_t>(y) * width + x) * 4;
    }
};

// ----------------------------------------------------------------------------
//  loadPNG
//  Entradas : path  - ruta al archivo .png en disco
//  Salidas  : out   - imagen decodificada en RGBA8 (solo valida si retorna true)
//             error - descripcion legible de la falla (solo si retorna false)
//  Retorno  : true si la decodificacion fue exitosa.
//  Soporta  : bit depth 1/2/4/8/16, tipos de color 0 (gris), 2 (RGB),
//             3 (paleta), 4 (gris+alfa) y 6 (RGBA), con transparencia tRNS.
//             No soporta imagenes entrelazadas (Adam7), que Minecraft no usa.
// ----------------------------------------------------------------------------
bool loadPNG(const std::string& path, Image& out, std::string& error);

} // namespace mcss
