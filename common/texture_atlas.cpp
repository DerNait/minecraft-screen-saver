// ============================================================================
//  texture_atlas.cpp - Implementacion del atlas de texturas
// ============================================================================
#include "texture_atlas.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace mcss {
namespace {

// Multiplica un canal de color de 8 bits por otro, conservando el rango 0..255.
inline unsigned char mulChannel(unsigned char a, unsigned char b) {
    return static_cast<unsigned char>((static_cast<int>(a) * static_cast<int>(b) + 127) / 255);
}

// Extrae los componentes de un color empaquetado 0xRRGGBB.
inline void unpackTint(uint32_t tint, unsigned char& r, unsigned char& g, unsigned char& b) {
    r = static_cast<unsigned char>((tint >> 16) & 0xFF);
    g = static_cast<unsigned char>((tint >> 8)  & 0xFF);
    b = static_cast<unsigned char>( tint        & 0xFF);
}

// Devuelve true si el archivo indicado existe y se puede abrir para lectura.
bool fileExists(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

} // namespace

std::string TextureAtlas::makeKey(const TextureSpec& spec) {
    std::string key = spec.base ? spec.base : "<null>";
    key += '|';
    key += spec.overlay ? spec.overlay : "-";
    key += '|';

    char buf[16];
    std::snprintf(buf, sizeof(buf), "%06X", spec.tint & 0xFFFFFF);
    key += buf;
    return key;
}

int TextureAtlas::registerTexture(const TextureSpec& spec) {
    std::string key = makeKey(spec);

    auto it = lookup_.find(key);
    if (it != lookup_.end()) return it->second;  // ya existe, se reutiliza la capa

    int layer = static_cast<int>(specs_.size());
    specs_.push_back(spec);
    lookup_.emplace(std::move(key), layer);
    return layer;
}

bool TextureAtlas::loadTile(const std::string& assetsDir, const char* name,
                            Image& out, std::string& error) {
    std::string path = assetsDir + "/" + name + ".png";

    // La cache evita volver a decodificar texturas compartidas por varios
    // bloques (por ejemplo "dirt", que usan pasto, podzol y el subsuelo).
    auto cached = cache_.find(path);
    const Image* source = nullptr;
    if (cached != cache_.end()) {
        source = &cached->second;
    } else {
        Image decoded;
        if (!loadPNG(path, decoded, error)) return false;
        source = &cache_.emplace(path, std::move(decoded)).first->second;
    }

    // Normalizacion al tamano del atlas. Las texturas animadas de Minecraft son
    // tiras verticales de fotogramas cuadrados: se toma solo el primero.
    int frameHeight = source->height;
    if (source->width > 0 && source->height > source->width &&
        source->height % source->width == 0) {
        frameHeight = source->width;
    }

    out.width  = kTileSize;
    out.height = kTileSize;
    out.pixels.assign(static_cast<size_t>(kTileSize) * kTileSize * 4, 0);

    // Remuestreo por vecino mas cercano: conserva los bordes duros del pixel art.
    for (int y = 0; y < kTileSize; ++y) {
        int sy = frameHeight  * y / kTileSize;
        for (int x = 0; x < kTileSize; ++x) {
            int sx = source->width * x / kTileSize;
            const unsigned char* src = source->pixelAt(sx, sy);
            unsigned char* dst = out.pixelAt(x, y);
            dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; dst[3] = src[3];
        }
    }
    return true;
}

bool TextureAtlas::build(const std::string& assetsDir, std::string& error) {
    error.clear();

    if (specs_.empty()) {
        error = "no se registro ninguna textura en el atlas";
        return false;
    }

    const size_t tilePixels = static_cast<size_t>(kTileSize) * kTileSize * 4;
    pixels_.assign(tilePixels * specs_.size(), 0);

    for (size_t layer = 0; layer < specs_.size(); ++layer) {
        const TextureSpec& spec = specs_[layer];
        if (!spec.base) {
            error = "especificacion de textura sin archivo base";
            return false;
        }

        Image tile;
        if (!loadTile(assetsDir, spec.base, tile, error)) return false;

        unsigned char tr, tg, tb;
        unpackTint(spec.tint, tr, tg, tb);

        if (spec.overlay) {
            // Caso pasto: la base es tierra y encima va un overlay tenido con el
            // color del bioma, mezclado segun su canal alfa.
            Image overlay;
            if (!loadTile(assetsDir, spec.overlay, overlay, error)) return false;

            for (int y = 0; y < kTileSize; ++y) {
                for (int x = 0; x < kTileSize; ++x) {
                    unsigned char* dst = tile.pixelAt(x, y);
                    const unsigned char* ov = overlay.pixelAt(x, y);
                    int alpha = ov[3];
                    if (alpha == 0) continue;

                    unsigned char or_ = mulChannel(ov[0], tr);
                    unsigned char og  = mulChannel(ov[1], tg);
                    unsigned char ob  = mulChannel(ov[2], tb);

                    dst[0] = static_cast<unsigned char>((or_ * alpha + dst[0] * (255 - alpha)) / 255);
                    dst[1] = static_cast<unsigned char>((og  * alpha + dst[1] * (255 - alpha)) / 255);
                    dst[2] = static_cast<unsigned char>((ob  * alpha + dst[2] * (255 - alpha)) / 255);
                    dst[3] = 255;
                }
            }
        } else if (spec.tint != 0xFFFFFF) {
            // Caso hojas y cara superior del pasto: la textura viene en escala de
            // grises y el color proviene enteramente del tinte del bioma.
            for (int y = 0; y < kTileSize; ++y) {
                for (int x = 0; x < kTileSize; ++x) {
                    unsigned char* dst = tile.pixelAt(x, y);
                    dst[0] = mulChannel(dst[0], tr);
                    dst[1] = mulChannel(dst[1], tg);
                    dst[2] = mulChannel(dst[2], tb);
                }
            }
        }

        std::copy(tile.pixels.begin(), tile.pixels.end(),
                  pixels_.begin() + static_cast<long>(tilePixels * layer));
    }

    // La cache de PNG ya no se necesita: se libera para no retener memoria.
    cache_.clear();
    return true;
}

bool TextureAtlas::upload(std::string& error) {
    error.clear();

    if (pixels_.empty()) {
        error = "el atlas no fue construido antes de subirlo a la GPU";
        return false;
    }

    glGenTextures(1, &textureId_);
    if (textureId_ == 0) {
        error = "OpenGL no pudo crear la textura del atlas";
        return false;
    }

    glBindTexture(GL_TEXTURE_2D_ARRAY, textureId_);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8,
                 kTileSize, kTileSize, layerCount(), 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels_.data());

    // NEAREST en magnificacion conserva el estilo pixelado del juego original;
    // los mipmaps evitan el ruido visual en los bloques lejanos.
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

    GLenum status = glGetError();
    if (status != GL_NO_ERROR) {
        error = "error de OpenGL al subir el atlas (codigo " + std::to_string(status) + ")";
        return false;
    }
    return true;
}

void TextureAtlas::destroy() {
    if (textureId_ != 0) {
        glDeleteTextures(1, &textureId_);
        textureId_ = 0;
    }
    pixels_.clear();
    pixels_.shrink_to_fit();
}

std::string findAssetsDirectory(const std::string& hint) {
    // Archivo testigo que confirma que la carpeta es la correcta.
    const char* probe = "/stone.png";

    if (!hint.empty()) {
        return fileExists(hint + probe) ? hint : std::string();
    }

    // Rutas candidatas relativas: raiz del proyecto, build/, build/algo/, etc.
    const char* candidates[] = {
        "assets/textures",
        "../assets/textures",
        "../../assets/textures",
        "../../../assets/textures",
    };
    for (const char* c : candidates) {
        if (fileExists(std::string(c) + probe)) return c;
    }
    return std::string();
}

} // namespace mcss
