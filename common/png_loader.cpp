// ============================================================================
//  png_loader.cpp - Implementacion del decodificador PNG
// ----------------------------------------------------------------------------
//  Flujo de decodificacion:
//    1. Verificar la firma de 8 bytes del formato PNG.
//    2. Recorrer los "chunks" y quedarse con IHDR, PLTE, tRNS e IDAT.
//    3. Descomprimir con zlib el flujo DEFLATE formado por todos los IDAT.
//    4. Deshacer el filtro por fila (None, Sub, Up, Average, Paeth).
//    5. Expandir los pixeles crudos al formato uniforme RGBA8.
// ============================================================================
#include "png_loader.hpp"

#include <zlib.h>

#include <cstdio>
#include <cstring>

namespace mcss {
namespace {

// Firma obligatoria al inicio de todo archivo PNG.
const unsigned char PNG_SIGNATURE[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

// Lee un entero de 32 bits big-endian desde un buffer de bytes.
uint32_t readBE32(const unsigned char* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |  static_cast<uint32_t>(p[3]);
}

// Cantidad de canales de color segun el tipo de color declarado en el IHDR.
int channelsForColorType(int colorType) {
    switch (colorType) {
        case 0: return 1;  // escala de grises
        case 2: return 3;  // RGB
        case 3: return 1;  // indice de paleta
        case 4: return 2;  // escala de grises + alfa
        case 6: return 4;  // RGBA
        default: return 0; // tipo invalido
    }
}

// Predictor de Paeth definido por el estandar PNG: elige entre el vecino
// izquierdo (a), superior (b) y superior-izquierdo (c) el mas cercano a la
// estimacion lineal a + b - c.
unsigned char paethPredictor(int a, int b, int c) {
    int p  = a + b - c;
    int pa = p > a ? p - a : a - p;
    int pb = p > b ? p - b : b - p;
    int pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) return static_cast<unsigned char>(a);
    if (pb <= pc)             return static_cast<unsigned char>(b);
    return static_cast<unsigned char>(c);
}

// Lee un archivo completo a memoria. Devuelve false si no se pudo abrir o leer.
bool readWholeFile(const std::string& path, std::vector<unsigned char>& data) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;

    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    if (size < 0) { std::fclose(f); return false; }
    std::fseek(f, 0, SEEK_SET);

    data.resize(static_cast<size_t>(size));
    size_t read = size > 0 ? std::fread(data.data(), 1, data.size(), f) : 0;
    std::fclose(f);
    return read == data.size();
}

// Extrae el valor de una muestra de "bitDepth" bits ubicada en la posicion
// logica "index" de una fila empaquetada (los bits se leen del mas significativo
// al menos significativo, como exige el estandar).
unsigned int extractSample(const unsigned char* row, size_t index, int bitDepth) {
    switch (bitDepth) {
        case 8:  return row[index];
        case 16: return row[index * 2];  // se conserva solo el byte alto
        case 4:  return (row[index / 2] >> (index % 2 ? 0 : 4)) & 0x0F;
        case 2:  return (row[index / 4] >> (6 - 2 * (index % 4))) & 0x03;
        case 1:  return (row[index / 8] >> (7 - (index % 8))) & 0x01;
        default: return 0;
    }
}

// Escala una muestra de "bitDepth" bits al rango completo 0..255.
unsigned char scaleToByte(unsigned int value, int bitDepth) {
    switch (bitDepth) {
        case 8:
        case 16: return static_cast<unsigned char>(value);
        case 4:  return static_cast<unsigned char>(value * 17);         // 15 -> 255
        case 2:  return static_cast<unsigned char>(value * 85);         //  3 -> 255
        case 1:  return static_cast<unsigned char>(value * 255);        //  1 -> 255
        default: return 0;
    }
}

} // namespace

bool loadPNG(const std::string& path, Image& out, std::string& error) {
    error.clear();
    out = Image{};

    std::vector<unsigned char> file;
    if (!readWholeFile(path, file)) {
        error = "no se pudo leer el archivo: " + path;
        return false;
    }
    if (file.size() < 8 || std::memcmp(file.data(), PNG_SIGNATURE, 8) != 0) {
        error = "firma PNG invalida: " + path;
        return false;
    }

    // --- Recorrido de chunks -------------------------------------------------
    uint32_t width = 0, height = 0;
    int bitDepth = 0, colorType = 0, interlace = 0;
    std::vector<unsigned char> palette;      // PLTE: tripletas RGB
    std::vector<unsigned char> paletteAlpha; // tRNS para tipo 3
    bool hasColorKey = false;                // tRNS para tipos 0 y 2
    unsigned int keyR = 0, keyG = 0, keyB = 0;
    std::vector<unsigned char> deflateData;  // concatenacion de todos los IDAT
    bool sawHeader = false;

    size_t offset = 8;
    while (offset + 8 <= file.size()) {
        uint32_t length = readBE32(&file[offset]);
        const char* type = reinterpret_cast<const char*>(&file[offset + 4]);
        size_t dataStart = offset + 8;
        // Cada chunk termina con 4 bytes de CRC que no necesitamos validar aqui.
        if (dataStart + length + 4 > file.size()) {
            error = "chunk PNG truncado en: " + path;
            return false;
        }
        const unsigned char* data = &file[dataStart];

        if (std::memcmp(type, "IHDR", 4) == 0) {
            if (length < 13) { error = "IHDR incompleto en: " + path; return false; }
            width     = readBE32(data);
            height    = readBE32(data + 4);
            bitDepth  = data[8];
            colorType = data[9];
            // data[10] = metodo de compresion, data[11] = metodo de filtrado
            interlace = data[12];
            sawHeader = true;
        } else if (std::memcmp(type, "PLTE", 4) == 0) {
            palette.assign(data, data + length);
        } else if (std::memcmp(type, "tRNS", 4) == 0) {
            if (colorType == 3) {
                paletteAlpha.assign(data, data + length);
            } else if (colorType == 0 && length >= 2) {
                hasColorKey = true;
                keyR = keyG = keyB = (static_cast<unsigned int>(data[0]) << 8) | data[1];
            } else if (colorType == 2 && length >= 6) {
                hasColorKey = true;
                keyR = (static_cast<unsigned int>(data[0]) << 8) | data[1];
                keyG = (static_cast<unsigned int>(data[2]) << 8) | data[3];
                keyB = (static_cast<unsigned int>(data[4]) << 8) | data[5];
            }
        } else if (std::memcmp(type, "IDAT", 4) == 0) {
            deflateData.insert(deflateData.end(), data, data + length);
        } else if (std::memcmp(type, "IEND", 4) == 0) {
            break;
        }

        offset = dataStart + length + 4;
    }

    // --- Validaciones defensivas del encabezado ------------------------------
    if (!sawHeader)      { error = "PNG sin IHDR: " + path;               return false; }
    if (width == 0 || height == 0) { error = "dimensiones nulas: " + path; return false; }
    if (interlace != 0)  { error = "PNG entrelazado no soportado: " + path; return false; }
    if (deflateData.empty()) { error = "PNG sin datos IDAT: " + path;     return false; }

    int channels = channelsForColorType(colorType);
    if (channels == 0) {
        error = "tipo de color PNG no soportado (" + std::to_string(colorType) + "): " + path;
        return false;
    }
    if (colorType == 3 && palette.empty()) {
        error = "PNG con paleta pero sin chunk PLTE: " + path;
        return false;
    }

    // --- Descompresion DEFLATE ----------------------------------------------
    // Cada fila trae 1 byte extra al inicio que indica el filtro aplicado.
    size_t rowBytes  = (static_cast<size_t>(width) * channels * bitDepth + 7) / 8;
    size_t rawSize   = (rowBytes + 1) * height;
    std::vector<unsigned char> raw(rawSize);

    uLongf destLen = static_cast<uLongf>(rawSize);
    int zr = uncompress(raw.data(), &destLen,
                        deflateData.data(), static_cast<uLong>(deflateData.size()));
    if (zr != Z_OK || destLen != rawSize) {
        error = "fallo al descomprimir los datos PNG: " + path;
        return false;
    }

    // --- Deshacer el filtrado por fila --------------------------------------
    // bpp = bytes por pixel, con minimo 1 (necesario cuando bitDepth < 8).
    size_t bpp = (static_cast<size_t>(channels) * bitDepth + 7) / 8;
    if (bpp == 0) bpp = 1;

    std::vector<unsigned char> unfiltered(static_cast<size_t>(rowBytes) * height);
    for (uint32_t y = 0; y < height; ++y) {
        unsigned char filterType = raw[(rowBytes + 1) * y];
        const unsigned char* src = &raw[(rowBytes + 1) * y + 1];
        unsigned char* cur  = &unfiltered[rowBytes * y];
        const unsigned char* prev = (y > 0) ? &unfiltered[rowBytes * (y - 1)] : nullptr;

        for (size_t x = 0; x < rowBytes; ++x) {
            int a = (x >= bpp)        ? cur[x - bpp]        : 0;  // pixel izquierdo
            int b = prev              ? prev[x]             : 0;  // pixel superior
            int c = (prev && x >= bpp)? prev[x - bpp]       : 0;  // superior izquierdo
            int value = src[x];
            switch (filterType) {
                case 0: break;                                    // None
                case 1: value += a; break;                        // Sub
                case 2: value += b; break;                        // Up
                case 3: value += (a + b) / 2; break;              // Average
                case 4: value += paethPredictor(a, b, c); break;  // Paeth
                default:
                    error = "tipo de filtro PNG invalido: " + path;
                    return false;
            }
            cur[x] = static_cast<unsigned char>(value & 0xFF);
        }
    }

    // --- Expansion a RGBA8 ---------------------------------------------------
    out.width  = static_cast<int>(width);
    out.height = static_cast<int>(height);
    out.pixels.assign(static_cast<size_t>(width) * height * 4, 0);

    const size_t paletteEntries = palette.size() / 3;

    for (uint32_t y = 0; y < height; ++y) {
        const unsigned char* row = &unfiltered[rowBytes * y];
        unsigned char* dst = &out.pixels[static_cast<size_t>(y) * width * 4];

        for (uint32_t x = 0; x < width; ++x) {
            unsigned char r = 0, g = 0, b = 0, a = 255;

            switch (colorType) {
                case 0: {  // escala de grises
                    unsigned int v = extractSample(row, x, bitDepth);
                    r = g = b = scaleToByte(v, bitDepth);
                    if (hasColorKey && v == keyR) a = 0;
                    break;
                }
                case 2: {  // RGB
                    size_t base = static_cast<size_t>(x) * 3;
                    unsigned int vr = extractSample(row, base + 0, bitDepth);
                    unsigned int vg = extractSample(row, base + 1, bitDepth);
                    unsigned int vb = extractSample(row, base + 2, bitDepth);
                    r = scaleToByte(vr, bitDepth);
                    g = scaleToByte(vg, bitDepth);
                    b = scaleToByte(vb, bitDepth);
                    if (hasColorKey && vr == keyR && vg == keyG && vb == keyB) a = 0;
                    break;
                }
                case 3: {  // indice a la paleta
                    unsigned int idx = extractSample(row, x, bitDepth);
                    if (idx < paletteEntries) {
                        r = palette[idx * 3 + 0];
                        g = palette[idx * 3 + 1];
                        b = palette[idx * 3 + 2];
                    }
                    a = (idx < paletteAlpha.size()) ? paletteAlpha[idx] : 255;
                    break;
                }
                case 4: {  // escala de grises + alfa
                    size_t base = static_cast<size_t>(x) * 2;
                    r = g = b = scaleToByte(extractSample(row, base + 0, bitDepth), bitDepth);
                    a         = scaleToByte(extractSample(row, base + 1, bitDepth), bitDepth);
                    break;
                }
                case 6: {  // RGBA
                    size_t base = static_cast<size_t>(x) * 4;
                    r = scaleToByte(extractSample(row, base + 0, bitDepth), bitDepth);
                    g = scaleToByte(extractSample(row, base + 1, bitDepth), bitDepth);
                    b = scaleToByte(extractSample(row, base + 2, bitDepth), bitDepth);
                    a = scaleToByte(extractSample(row, base + 3, bitDepth), bitDepth);
                    break;
                }
                default: break;
            }

            dst[x * 4 + 0] = r;
            dst[x * 4 + 1] = g;
            dst[x * 4 + 2] = b;
            dst[x * 4 + 3] = a;
        }
    }

    return true;
}

} // namespace mcss
