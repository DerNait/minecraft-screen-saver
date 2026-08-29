// ============================================================================
//  noise.hpp - Ruido pseudoaleatorio coherente para la generacion del terreno
// ----------------------------------------------------------------------------
//  Implementacion propia de "value noise" 2D y 3D con interpolacion suave y su
//  combinacion en octavas (fBm, fractional Brownian motion). Todo el ruido es
//  una funcion pura de (coordenada, semilla): no guarda estado interno, por lo
//  que dos hilos distintos pueden evaluarlo simultaneamente sobre regiones
//  diferentes del mundo sin necesidad de sincronizacion. Esta propiedad es la
//  que permitira, en la version paralela, repartir el terreno entre hilos.
//
//  Las funciones se declaran inline en el encabezado porque se evaluan millones
//  de veces durante la generacion y conviene que el compilador pueda insertarlas
//  directamente en el punto de llamada.
// ============================================================================
#pragma once

#include <cmath>
#include <cstdint>

namespace mcss {

// ----------------------------------------------------------------------------
//  hashUint
//  Entradas: x - valor de 32 bits a mezclar
//  Salida  : entero de 32 bits con los bits bien dispersos
//  Mezcla de bits estilo "integer hash" (variante de MurmurHash3 finalizer).
//  Sirve como generador pseudoaleatorio sin estado.
// ----------------------------------------------------------------------------
inline uint32_t hashUint(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

// Combina coordenadas enteras y una semilla en un unico hash de 32 bits.
inline uint32_t hashCoords(int x, int y, uint32_t seed) {
    uint32_t h = seed;
    h = hashUint(h ^ (static_cast<uint32_t>(x) * 0x9E3779B1u));
    h = hashUint(h ^ (static_cast<uint32_t>(y) * 0x85EBCA77u));
    return h;
}

inline uint32_t hashCoords(int x, int y, int z, uint32_t seed) {
    uint32_t h = seed;
    h = hashUint(h ^ (static_cast<uint32_t>(x) * 0x9E3779B1u));
    h = hashUint(h ^ (static_cast<uint32_t>(y) * 0x85EBCA77u));
    h = hashUint(h ^ (static_cast<uint32_t>(z) * 0xC2B2AE3Du));
    return h;
}

// Convierte un hash de 32 bits a un flotante en el rango [0, 1).
inline float hashToUnitFloat(uint32_t h) {
    return static_cast<float>(h >> 8) * (1.0f / 16777216.0f);  // 24 bits utiles
}

// Valor pseudoaleatorio en [0, 1) asociado a una celda entera de la reticula.
inline float latticeValue(int x, int y, uint32_t seed) {
    return hashToUnitFloat(hashCoords(x, y, seed));
}

inline float latticeValue(int x, int y, int z, uint32_t seed) {
    return hashToUnitFloat(hashCoords(x, y, z, seed));
}

// ----------------------------------------------------------------------------
//  smoothStep
//  Curva de Hermite 3t^2 - 2t^3 usada para interpolar entre celdas vecinas.
//  Evita los bordes rectos que produciria una interpolacion lineal pura.
// ----------------------------------------------------------------------------
inline float smoothStep(float t) {
    return t * t * (3.0f - 2.0f * t);
}

// Interpolacion lineal simple entre a y b.
inline float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

// ----------------------------------------------------------------------------
//  valueNoise2D / valueNoise3D
//  Entradas: x, y, z - coordenadas continuas en el espacio del ruido
//            seed    - semilla del mundo
//  Salida  : valor en [0, 1] continuo y con derivada suave
//  Interpola los valores pseudoaleatorios de las esquinas de la celda entera
//  que contiene al punto consultado.
// ----------------------------------------------------------------------------
inline float valueNoise2D(float x, float y, uint32_t seed) {
    int   xi = static_cast<int>(std::floor(x));
    int   yi = static_cast<int>(std::floor(y));
    float tx = smoothStep(x - static_cast<float>(xi));
    float ty = smoothStep(y - static_cast<float>(yi));

    float v00 = latticeValue(xi,     yi,     seed);
    float v10 = latticeValue(xi + 1, yi,     seed);
    float v01 = latticeValue(xi,     yi + 1, seed);
    float v11 = latticeValue(xi + 1, yi + 1, seed);

    return lerp(lerp(v00, v10, tx), lerp(v01, v11, tx), ty);
}

inline float valueNoise3D(float x, float y, float z, uint32_t seed) {
    int   xi = static_cast<int>(std::floor(x));
    int   yi = static_cast<int>(std::floor(y));
    int   zi = static_cast<int>(std::floor(z));
    float tx = smoothStep(x - static_cast<float>(xi));
    float ty = smoothStep(y - static_cast<float>(yi));
    float tz = smoothStep(z - static_cast<float>(zi));

    float v000 = latticeValue(xi,     yi,     zi,     seed);
    float v100 = latticeValue(xi + 1, yi,     zi,     seed);
    float v010 = latticeValue(xi,     yi + 1, zi,     seed);
    float v110 = latticeValue(xi + 1, yi + 1, zi,     seed);
    float v001 = latticeValue(xi,     yi,     zi + 1, seed);
    float v101 = latticeValue(xi + 1, yi,     zi + 1, seed);
    float v011 = latticeValue(xi,     yi + 1, zi + 1, seed);
    float v111 = latticeValue(xi + 1, yi + 1, zi + 1, seed);

    float y0 = lerp(lerp(v000, v100, tx), lerp(v010, v110, tx), ty);
    float y1 = lerp(lerp(v001, v101, tx), lerp(v011, v111, tx), ty);
    return lerp(y0, y1, tz);
}

// ----------------------------------------------------------------------------
//  fbm2D / fbm3D  (fractional Brownian motion)
//  Entradas: x, y, z    - coordenadas continuas
//            seed       - semilla del mundo
//            octaves    - cantidad de capas de ruido a sumar (1..8)
//            lacunarity - factor de crecimiento de la frecuencia por octava
//            gain       - factor de decaimiento de la amplitud por octava
//  Salida  : valor normalizado en [0, 1]
//  Superponer octavas de frecuencia creciente y amplitud decreciente produce
//  relieve con detalle a varias escalas: colinas grandes con rugosidad fina.
// ----------------------------------------------------------------------------
inline float fbm2D(float x, float y, uint32_t seed, int octaves,
                   float lacunarity = 2.0f, float gain = 0.5f) {
    float total     = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float maxTotal  = 0.0f;

    for (int i = 0; i < octaves; ++i) {
        // Cada octava usa una semilla derivada para que no se repita el patron.
        total    += amplitude * valueNoise2D(x * frequency, y * frequency,
                                             seed + static_cast<uint32_t>(i) * 0x68BC21EBu);
        maxTotal += amplitude;
        amplitude *= gain;
        frequency *= lacunarity;
    }
    return maxTotal > 0.0f ? total / maxTotal : 0.0f;
}

inline float fbm3D(float x, float y, float z, uint32_t seed, int octaves,
                   float lacunarity = 2.0f, float gain = 0.5f) {
    float total     = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float maxTotal  = 0.0f;

    for (int i = 0; i < octaves; ++i) {
        total    += amplitude * valueNoise3D(x * frequency, y * frequency, z * frequency,
                                             seed + static_cast<uint32_t>(i) * 0x68BC21EBu);
        maxTotal += amplitude;
        amplitude *= gain;
        frequency *= lacunarity;
    }
    return maxTotal > 0.0f ? total / maxTotal : 0.0f;
}

} // namespace mcss
