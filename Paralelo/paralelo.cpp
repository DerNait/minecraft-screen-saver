// ============================================================================
//  paralelo.cpp - Minecraft Screen Saver, VERSION PARALELA CON OPENMP
//  Computacion Paralela y Distribuida - Universidad del Valle de Guatemala
// ----------------------------------------------------------------------------
//  Protector de pantalla que genera un mundo de bloques estilo Minecraft a
//  partir de una semilla aleatoria, lo construye capa por capa de abajo hacia
//  arriba, lo sostiene unos segundos, lo desarma de arriba hacia abajo y vuelve
//  a empezar con un mundo nuevo.
//
//  Esta version conserva el mismo resultado y el mismo flujo visual de la linea
//  base secuencial, pero reparte con OpenMP el trabajo de CPU proporcional a N.
//  OpenGL permanece en el hilo principal porque el contexto no es compartido.
//  Los puntos paralelizados son:
//
//    1. generateVoxels()        - mapa, estratos y vegetacion por regiones
//    2. buildBlockList()        - compactacion determinista por prefijos
//    3. updateWorld()           - fisica con reduccion de bloques vivos
//    4. buildInstanceBuffer()   - compactacion determinista por prefijos
//    5. buildNearbyShadowInstances() - casters locales para el shadow map
//
//  Los mobs opcionales comienzan con una IA secuencial para establecer el
//  comportamiento de referencia antes de paralelizarla en la siguiente tanda.
//
//  Elemento de fisica/trigonometria requerido por el enunciado:
//    - Cada bloque cae con aceleracion constante (integracion de Euler de la
//      gravedad) hasta asentarse en su posicion final.
//    - La camara puede orbitar el mundo o alternar entre encuadres exteriores e
//      interiores; los recorridos usan funciones trigonometricas (seno y coseno).
// ============================================================================

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <omp.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#include "app_config.hpp"
#include "benchmark.hpp"
#include "block_catalog.hpp"
#include "mob_renderer.hpp"
#include "noise.hpp"
#include "render.hpp"
#include "shadow_map.hpp"
#include "sky_renderer.hpp"
#include "texture_atlas.hpp"

using namespace mcss;

// ============================================================================
//  CONSTANTES DE SIMULACION
//  Ninguna dimension del mundo esta fija aqui: todas se derivan de N y de los
//  argumentos. Estas constantes describen unicamente el comportamiento fisico
//  y el ritmo de la animacion.
// ============================================================================
static const float kGravity        = 45.0f;  // aceleracion de caida (bloques/s^2)
static const float kDropHeight     = 6.0f;   // altura desde la que cae cada bloque
static const float kPopSeconds     = 0.12f;  // tiempo que tarda en crecer al aparecer
static const float kShrinkSeconds  = 0.25f;  // tiempo que tarda en encogerse al desarmarse
static const float kNoiseFrequency = 0.035f; // escala horizontal de las colinas
static const float kBaseHeight     = 9.0f;   // altura minima de una columna
static const float kReliefAmplitud = 14.0f;  // amplitud maxima del relieve
static const int   kVegetationHalo = 2;      // radio horizontal maximo de una copa

// ============================================================================
//  ESTADO DE UN BLOQUE
// ============================================================================

// Etapas por las que pasa un bloque durante el ciclo del protector de pantalla.
enum class BlockState : uint8_t {
    Waiting,    // todavia no le toca aparecer
    Falling,    // esta cayendo hacia su posicion final
    Placed,     // ya asentado y formando parte del terreno solido
    Vanishing,  // encogiendose durante el desarmado
    Gone        // ya desaparecio, no se dibuja
};

// ----------------------------------------------------------------------------
//  Block - un voxel del mundo con su estado de animacion.
//  Se mantiene compacto (32 bytes) porque el arreglo completo se recorre entero
//  en cada fotograma y su tamano determina el trafico de memoria del programa.
// ----------------------------------------------------------------------------
struct Block {
    int16_t    gx, gy, gz;    // posicion entera dentro de la reticula del mundo
    BlockId    id;            // tipo de bloque (determina sus texturas)
    uint8_t    shade;         // variacion pseudoaleatoria de brillo
    BlockState state;         // etapa actual de la animacion
    float      revealAt;      // segundo del ciclo en que empieza a caer
    float      vanishAt;      // segundo del ciclo en que empieza a desaparecer
    float      posY;          // altura actual, animada por la gravedad
    float      velY;          // velocidad vertical actual
    float      scale;         // escala 0..1 para la aparicion y el desarmado
};

// ============================================================================
//  MUNDO
// ============================================================================
struct World {
    int          side       = 0;        // columnas por lado (el mundo es side x side)
    int          height     = 0;        // cantidad de capas verticales (Y)
    uint32_t     seed       = 0;        // semilla que genero este mundo
    int          biomeIndex = 0;        // bioma elegido
    const Biome* biome      = nullptr;  // reglas visuales del bioma

    std::vector<Block>   blocks;        // todos los bloques, ordenados por capa Y
    std::vector<uint8_t> occupancy;     // 1 si la celda esta ocupada AHORA MISMO
    std::vector<int>     heightMap;     // altura de la superficie por columna

    // Indice lineal de una celda de la reticula. El orden es Y-mayor para que
    // recorrer las capas de abajo hacia arriba sea un recorrido contiguo en
    // memoria, que es exactamente el orden en que se construye el mundo.
    inline size_t index(int x, int y, int z) const {
        return (static_cast<size_t>(y) * side + z) * side + x;
    }

    // Consulta segura de ocupacion: las celdas fuera de la reticula se
    // consideran vacias, de modo que las caras del borde siempre se dibujan.
    inline bool occupiedAt(int x, int y, int z) const {
        if (x < 0 || x >= side || z < 0 || z >= side || y < 0 || y >= height) return false;
        return occupancy[index(x, y, z)] != 0;
    }
};

// Fases del ciclo del protector de pantalla.
enum class Phase { Building, Holding, Dissolving, Paused };

// ============================================================================
//  CAMARA
// ============================================================================

// Un encuadre completo. Ademas de la posicion y el punto observado se conserva
// el vector vertical porque la toma cenital necesita una orientacion distinta.
struct CameraView {
    glm::vec3   eye;
    glm::vec3   target;
    glm::vec3   up;
    float       fovDegrees;
    int         shot;
    const char* name;
};

static const int kCameraShotCount = 6;
static const char* const kCameraShotNames[kCameraShotCount] = {
    "orbita exterior",
    "mirador interior",
    "panoramica interior",
    "orbita interior",
    "diagonal elevada",
    "vista cenital"
};

// Secuencia activa del modo automatico. El mirador interior fijo (toma 1) se
// conserva implementado en makeCameraShot(), pero queda fuera de la rotacion
// para poder reactivarlo en el futuro agregando nuevamente el 1 a esta lista.
static const int kAutoCameraShots[] = { 0, 2, 3, 4, 5 };
static const int kAutoCameraShotCount =
    static_cast<int>(sizeof(kAutoCameraShots) / sizeof(kAutoCameraShots[0]));

// Devuelve la altura de la superficie bajo una posicion expresada en las
// coordenadas centradas que usa el renderizador. Se interpola entre las cuatro
// columnas vecinas: elegir una sola columna con redondeo produciria un salto
// vertical cada vez que una camara movil cruza el limite entre dos bloques.
static float terrainTopAt(const World& world, float worldX, float worldZ) {
    if (world.heightMap.empty() || world.side <= 0) return 1.0f;

    const float offset = world.side * 0.5f - 0.5f;
    float gridX = std::max(0.0f, std::min(static_cast<float>(world.side - 1),
                                         worldX + offset));
    float gridZ = std::max(0.0f, std::min(static_cast<float>(world.side - 1),
                                         worldZ + offset));

    const int x0 = static_cast<int>(std::floor(gridX));
    const int z0 = static_cast<int>(std::floor(gridZ));
    const int x1 = std::min(x0 + 1, world.side - 1);
    const int z1 = std::min(z0 + 1, world.side - 1);
    const float tx = gridX - static_cast<float>(x0);
    const float tz = gridZ - static_cast<float>(z0);

    const auto heightAt = [&world](int x, int z) {
        return static_cast<float>(
            world.heightMap[static_cast<size_t>(z) * world.side + x]);
    };

    const float nearHeight = heightAt(x0, z0) * (1.0f - tx) + heightAt(x1, z0) * tx;
    const float farHeight  = heightAt(x0, z1) * (1.0f - tx) + heightAt(x1, z1) * tx;
    return nearHeight * (1.0f - tz) + farHeight * tz + 0.5f;
}

// Construye uno de los encuadres de la secuencia automatica. Todos escalan con
// las dimensiones del mundo, de modo que funcionan igual para distintos N.
static CameraView makeCameraShot(const World& world, int shot, double now) {
    const float side = static_cast<float>(world.side);
    const float time = static_cast<float>(now);
    const float centerSurface = terrainTopAt(world, 0.0f, 0.0f);
    const glm::vec3 center(0.0f, centerSurface + 2.0f, 0.0f);
    // Las tomas interiores moviles mantienen esta altura durante todo el mundo.
    // No siguen el relieve local, por lo que su movimiento vertical es nulo.
    const float interiorEyeY = static_cast<float>(world.height) - 2.0f;

    CameraView view{};
    view.shot = shot;
    view.name = kCameraShotNames[shot];
    view.up = glm::vec3(0.0f, 1.0f, 0.0f);

    switch (shot) {
        case 0: { // Orbita exterior original.
            const float radius = side * 1.05f + 14.0f;
            const float height = world.height * 0.85f + side * 0.30f;
            const float angle = time * 0.15f;
            view.eye = glm::vec3(std::cos(angle) * radius,
                                 height,
                                 std::sin(angle) * radius);
            view.target = glm::vec3(0.0f, world.height * 0.30f, 0.0f);
            view.fovDegrees = 55.0f;
            break;
        }

        case 1: { // Mirador fijo desactivado de la secuencia automatica.
            const float x = -side * 0.22f;
            const float z = -side * 0.18f;
            const float tx = side * 0.18f;
            const float tz = side * 0.16f;
            view.eye = glm::vec3(x, terrainTopAt(world, x, z) + 8.5f, z);
            view.target = glm::vec3(tx, terrainTopAt(world, tx, tz) + 2.0f, tz);
            view.fovDegrees = 62.0f;
            break;
        }

        case 2: { // Posicion interior fija que gira lentamente.
            const float x = side * 0.08f;
            const float z = -side * 0.10f;
            const float angle = time * 0.18f;
            const float lookDistance = side * 0.34f;
            const float tx = x + std::cos(angle) * lookDistance;
            const float tz = z + std::sin(angle) * lookDistance;
            view.eye = glm::vec3(x, interiorEyeY, z);
            view.target = glm::vec3(tx, center.y, tz);
            view.fovDegrees = 66.0f;
            break;
        }

        case 3: { // Orbita baja que recorre el interior del mundo.
            const float angle = time * 0.11f;
            const float radius = side * 0.31f;
            const float x = std::cos(angle) * radius;
            const float z = std::sin(angle) * radius;
            view.eye = glm::vec3(x, interiorEyeY + 1.0f, z);
            view.target = center;
            view.fovDegrees = 60.0f;
            break;
        }

        case 4: { // Vista exterior elevada desde una esquina.
            const float drift = std::sin(time * 0.08f) * side * 0.08f;
            view.eye = glm::vec3(side * 0.62f + drift,
                                 world.height * 0.95f + side * 0.38f,
                                -side * 0.62f + drift);
            view.target = center;
            view.fovDegrees = 52.0f;
            break;
        }

        default: { // Vista cenital con un desplazamiento circular pequeno.
            const float angle = time * 0.07f;
            const float radius = side * 0.12f;
            view.eye = glm::vec3(std::cos(angle) * radius,
                                 world.height + side * 0.90f + 12.0f,
                                 std::sin(angle) * radius);
            view.target = center;
            view.up = glm::vec3(0.0f, 0.0f, -1.0f);
            view.fovDegrees = 50.0f;
            break;
        }
    }

    return view;
}

// Selecciona la toma activa y suaviza el cambio desde la anterior. Durante la
// interpolacion se agrega un arco vertical que evita atravesar el terreno.
static CameraView calculateCamera(const World& world, const AppConfig& cfg, double now) {
    if (cfg.cameraMode != "auto") return makeCameraShot(world, 0, now);

    const double interval = static_cast<double>(cfg.cameraChangeSeconds);
    const long long slot = static_cast<long long>(std::floor(now / interval));
    const int playlistIndex = static_cast<int>(slot % kAutoCameraShotCount);
    const int shot = kAutoCameraShots[playlistIndex];
    CameraView current = makeCameraShot(world, shot, now);

    const double inSlot = now - static_cast<double>(slot) * interval;
    const float transitionSeconds = std::min(1.5f, cfg.cameraChangeSeconds * 0.25f);
    if (slot <= 0 || inSlot >= transitionSeconds) return current;

    const int previousPlaylistIndex =
        static_cast<int>((slot - 1) % kAutoCameraShotCount);
    const int previousShot = kAutoCameraShots[previousPlaylistIndex];
    const CameraView previous = makeCameraShot(world, previousShot, now);
    float t = static_cast<float>(inSlot / transitionSeconds);
    t = t * t * (3.0f - 2.0f * t); // smoothstep

    current.eye = previous.eye * (1.0f - t) + current.eye * t;
    current.eye.y += std::sin(t * 3.14159265f) * std::max(4.0f, world.side * 0.06f);
    current.target = previous.target * (1.0f - t) + current.target * t;
    current.up = glm::normalize(previous.up * (1.0f - t) + current.up * t);
    current.fovDegrees = previous.fovDegrees * (1.0f - t) + current.fovDegrees * t;
    return current;
}

// ============================================================================
//  ILUMINACION Y CICLO DE DIA/NOCHE
// ============================================================================

struct DayNightEnvironment {
    SceneLighting lighting;
    SkyEnvironment sky;
    const char* phaseName;
};

static float smoothRange(float edge0, float edge1, float value) {
    float t = std::max(0.0f, std::min(1.0f, (value - edge0) / (edge1 - edge0)));
    return t * t * (3.0f - 2.0f * t);
}

static glm::vec3 rgbColor(uint32_t color) {
    return glm::vec3(((color >> 16) & 0xFF) / 255.0f,
                     ((color >> 8)  & 0xFF) / 255.0f,
                     ( color        & 0xFF) / 255.0f);
}

// El ciclo comienza al mediodia para que el mundo sea visible desde el primer
// fotograma. El arco diurno ocupa 70% del tiempo total y el nocturno 30%; ambos
// conservan continuidad en el horizonte aunque avancen a velocidades distintas.
static DayNightEnvironment calculateDayNight(const World& world,
                                              const AppConfig& cfg,
                                              double now,
                                              const glm::vec3& cameraPosition) {
    constexpr float kTwoPi = 6.28318530f;
    constexpr float kPi = 3.14159265f;
    constexpr float kDayFraction = 0.70f;
    constexpr float kNightFraction = 1.0f - kDayFraction;
    constexpr float kHalfDay = kDayFraction * 0.5f;
    const float cycle = static_cast<float>(
        std::fmod(now / static_cast<double>(cfg.dayCycleSeconds), 1.0));
    float angle = 0.0f;
    if (cycle < kHalfDay) {
        // Mediodia -> atardecer: primer 35% del ciclo.
        angle = kPi * 0.5f + (cycle / kHalfDay) * kPi * 0.5f;
    } else if (cycle < kHalfDay + kNightFraction) {
        // Atardecer -> amanecer: la noche completa ocupa 30%.
        const float nightProgress =
            (cycle - kHalfDay) / kNightFraction;
        angle = kPi + nightProgress * kPi;
    } else {
        // Amanecer -> mediodia: ultimo 35%, cerrando el ciclo sin salto.
        const float morningProgress =
            (cycle - kHalfDay - kNightFraction) / kHalfDay;
        angle = kTwoPi + morningProgress * kPi * 0.5f;
    }
    const glm::vec3 sunDirection = glm::normalize(
        glm::vec3(std::cos(angle), std::sin(angle), 0.32f));

    const float daylight = smoothRange(-0.16f, 0.14f, sunDirection.y);
    const float night = 1.0f - smoothRange(-0.20f, 0.08f, sunDirection.y);
    const float sunsetStrength =
        (1.0f - smoothRange(0.02f, 0.38f, std::abs(sunDirection.y))) *
        (0.35f + daylight * 0.65f);

    const glm::vec3 biomeSky = rgbColor(world.biome->skyColor);
    const glm::vec3 dayZenith = glm::mix(biomeSky,
                                         glm::vec3(0.18f, 0.48f, 0.92f), 0.40f);
    const glm::vec3 dayHorizon = glm::mix(biomeSky,
                                          glm::vec3(0.90f, 0.95f, 1.0f), 0.50f);
    const glm::vec3 nightZenith(0.006f, 0.010f, 0.050f);
    const glm::vec3 nightHorizon(0.025f, 0.045f, 0.115f);

    DayNightEnvironment environment{};
    environment.sky.zenithColor = glm::mix(nightZenith, dayZenith, daylight);
    environment.sky.horizonColor = glm::mix(nightHorizon, dayHorizon, daylight);
    environment.sky.sunDirection = sunDirection;
    environment.sky.sunColor = glm::mix(glm::vec3(1.0f, 0.42f, 0.16f),
                                        glm::vec3(1.0f, 0.94f, 0.74f),
                                        smoothRange(0.02f, 0.48f, sunDirection.y));
    environment.sky.moonColor = glm::vec3(0.72f, 0.82f, 1.0f);
    environment.sky.sunsetColor = glm::vec3(1.0f, 0.20f, 0.035f);
    environment.sky.daylight = daylight;
    environment.sky.starVisibility = night;
    environment.sky.sunsetStrength = sunsetStrength;
    environment.sky.timeSeconds = static_cast<float>(now);

    environment.lighting.dynamic = true;
    environment.lighting.direction =
        sunDirection.y > -0.12f ? sunDirection : -sunDirection;
    environment.lighting.ambientColor = glm::mix(
        glm::vec3(0.14f, 0.16f, 0.23f),
        glm::vec3(0.42f, 0.43f, 0.43f), daylight);
    environment.lighting.ambientColor +=
        glm::vec3(0.10f, 0.055f, 0.025f) * sunsetStrength;
    const glm::vec3 daylightColor = glm::mix(
        glm::vec3(1.0f, 0.40f, 0.16f),
        glm::vec3(0.72f, 0.70f, 0.66f),
        smoothRange(0.02f, 0.45f, sunDirection.y));
    environment.lighting.diffuseColor = glm::mix(
        glm::vec3(0.20f, 0.23f, 0.34f), daylightColor, daylight);
    environment.lighting.fogColor = environment.sky.horizonColor +
        environment.sky.sunsetColor * sunsetStrength * 0.18f;
    environment.lighting.cameraPosition = cameraPosition;
    environment.lighting.fogDensity =
        1.0f / std::max(85.0f, static_cast<float>(world.side) * 2.2f);

    if (sunDirection.y > 0.22f) {
        environment.phaseName = "dia";
    } else if (sunDirection.y > -0.16f) {
        environment.phaseName = std::cos(angle) < 0.0f ? "atardecer" : "amanecer";
    } else {
        environment.phaseName = "noche";
    }
    return environment;
}

// ============================================================================
//  MOBS - PRIMERA TANDA: IA SECUENCIAL
// ============================================================================

enum class MobAction : uint8_t { Waiting, Walking };
enum class MobKind : uint8_t { Pig, Cow, Sheep };

struct Mob {
    float x, y, z;          // x,z en coordenadas de la reticula; y en el render
    float yaw;              // direccion actual
    float speed;            // bloques por segundo
    float actionRemaining;  // tiempo antes de elegir otra accion
    float hopPhase;         // fase visual del paso/salto
    uint32_t randomState;   // RNG propio para decisiones reproducibles
    MobAction action;
    MobKind kind;
};

static uint32_t nextMobRandom(Mob& mob) {
    mob.randomState = hashUint(mob.randomState + 0x9E3779B9u);
    return mob.randomState;
}

static float mobRandom01(Mob& mob) {
    return hashToUnitFloat(nextMobRandom(mob));
}

// Altura continua para que el mob suba desniveles de un bloque sin saltos
// visuales. Las decisiones de colision siguen usando celdas enteras.
static float mobSurfaceAt(const World& world, float gridX, float gridZ) {
    gridX = std::max(0.0f, std::min(static_cast<float>(world.side - 1), gridX));
    gridZ = std::max(0.0f, std::min(static_cast<float>(world.side - 1), gridZ));
    const int x0 = static_cast<int>(std::floor(gridX));
    const int z0 = static_cast<int>(std::floor(gridZ));
    const int x1 = std::min(x0 + 1, world.side - 1);
    const int z1 = std::min(z0 + 1, world.side - 1);
    const float tx = gridX - x0;
    const float tz = gridZ - z0;
    const auto h = [&world](int x, int z) {
        return static_cast<float>(
            world.heightMap[static_cast<size_t>(z) * world.side + x]);
    };
    const float h0 = h(x0, z0) * (1.0f - tx) + h(x1, z0) * tx;
    const float h1 = h(x0, z1) * (1.0f - tx) + h(x1, z1) * tx;
    return h0 * (1.0f - tz) + h1 * tz;
}

static bool mobCellIsClear(const World& world, int gx, int gz) {
    if (gx < 2 || gx >= world.side - 2 || gz < 2 || gz >= world.side - 2) {
        return false;
    }
    const int surface = world.heightMap[static_cast<size_t>(gz) * world.side + gx];
    if (surface + 2 >= world.height) return false;
    return !world.occupiedAt(gx, surface + 1, gz) &&
           !world.occupiedAt(gx, surface + 2, gz);
}

// Se ejecuta cuando inicia Holding, momento en que occupancy ya representa el
// terreno armado. Esto permite evitar arboles y cactus al elegir posiciones.
static std::vector<Mob> spawnMobs(const World& world, int requested) {
    std::vector<Mob> mobs;
    if (requested <= 0 || world.side < 6) return mobs;

    mobs.reserve(static_cast<size_t>(requested));
    std::vector<uint8_t> taken(static_cast<size_t>(world.side) * world.side, 0);
    uint32_t randomState = hashUint(world.seed ^ 0x4D4F4253u); // "MOBS"
    const int usableSide = world.side - 4;

    for (int i = 0; i < requested; ++i) {
        bool placed = false;
        for (int attempt = 0; attempt < 64 && !placed; ++attempt) {
            randomState = hashUint(randomState + 0x9E3779B9u);
            const int gx = 2 + static_cast<int>(randomState %
                                                static_cast<uint32_t>(usableSide));
            randomState = hashUint(randomState + 0x9E3779B9u);
            const int gz = 2 + static_cast<int>(randomState %
                                                static_cast<uint32_t>(usableSide));
            const size_t cell = static_cast<size_t>(gz) * world.side + gx;
            if (taken[cell] || !mobCellIsClear(world, gx, gz)) continue;

            taken[cell] = 1;
            Mob mob{};
            mob.x = static_cast<float>(gx);
            mob.z = static_cast<float>(gz);
            mob.y = static_cast<float>(
                world.heightMap[static_cast<size_t>(gz) * world.side + gx]) + 0.5f;
            mob.randomState = hashUint(randomState ^ static_cast<uint32_t>(i));
            mob.yaw = mobRandom01(mob) * 6.28318530f;
            mob.speed = 0.8f + mobRandom01(mob) * 0.6f;
            mob.actionRemaining = 0.25f + mobRandom01(mob) * 1.0f;
            mob.action = MobAction::Waiting;
            mob.kind = static_cast<MobKind>(i % 3);
            mobs.push_back(mob);
            placed = true;
        }
        if (!placed) break; // el mundo ya no tiene mas columnas libres
    }
    return mobs;
}

static void chooseNextMobAction(Mob& mob) {
    if (mobRandom01(mob) < 0.28f) {
        mob.action = MobAction::Waiting;
        mob.actionRemaining = 0.5f + mobRandom01(mob) * 1.5f;
        return;
    }

    mob.action = MobAction::Walking;
    mob.yaw = mobRandom01(mob) * 6.28318530f;
    mob.speed = 0.8f + mobRandom01(mob) * 0.7f;
    mob.actionRemaining = 1.2f + mobRandom01(mob) * 2.8f;
}

// Primera implementacion deliberadamente secuencial. En la siguiente tanda se
// separara en percepcion/propuesta/resolucion para actualizar mobs en paralelo.
static void updateMobsSequential(std::vector<Mob>& mobs, const World& world, float dt) {
    for (Mob& mob : mobs) {
        mob.actionRemaining -= dt;
        if (mob.actionRemaining <= 0.0f) chooseNextMobAction(mob);

        if (mob.action == MobAction::Walking) {
            const float nextX = mob.x + std::cos(mob.yaw) * mob.speed * dt;
            const float nextZ = mob.z + std::sin(mob.yaw) * mob.speed * dt;
            const int gx = static_cast<int>(std::lround(nextX));
            const int gz = static_cast<int>(std::lround(nextZ));
            bool clear = mobCellIsClear(world, gx, gz);

            if (clear) {
                const int oldX = static_cast<int>(std::lround(mob.x));
                const int oldZ = static_cast<int>(std::lround(mob.z));
                const int oldSurface = world.heightMap[
                    static_cast<size_t>(oldZ) * world.side + oldX];
                const int newSurface = world.heightMap[
                    static_cast<size_t>(gz) * world.side + gx];
                clear = std::abs(newSurface - oldSurface) <= 1;
            }

            if (clear) {
                mob.x = nextX;
                mob.z = nextZ;
                mob.hopPhase += dt * (7.0f + mob.speed * 2.0f);
            } else {
                // Giro determinista al encontrar un borde, arbol o desnivel.
                const float side = mobRandom01(mob) < 0.5f ? -1.0f : 1.0f;
                mob.yaw += side * (1.2f + mobRandom01(mob) * 1.2f);
                mob.actionRemaining = 0.35f + mobRandom01(mob) * 0.45f;
            }
        }

        const float targetY = mobSurfaceAt(world, mob.x, mob.z) + 0.5f;
        mob.y += (targetY - mob.y) * std::min(1.0f, dt * 8.0f);
    }
}

static void buildMobInstances(const std::vector<Mob>& mobs, const World& world,
                              std::vector<MobInstanceData>& pigs,
                              std::vector<MobInstanceData>& cows,
                              std::vector<MobInstanceData>& sheep) {
    const float offset = world.side * 0.5f - 0.5f;
    pigs.clear();
    cows.clear();
    sheep.clear();
    pigs.reserve((mobs.size() + 2) / 3);
    cows.reserve((mobs.size() + 2) / 3);
    sheep.reserve((mobs.size() + 2) / 3);

    for (const Mob& mob : mobs) {
        const float bob = mob.action == MobAction::Walking
                        ? std::max(0.0f, std::sin(mob.hopPhase)) * 0.10f
                        : 0.0f;
        // Los modelos cuadrupedos miran hacia -Z en su espacio local. Este
        // desfase alinea su cabeza con la direccion en que avanza la IA.
        const MobInstanceData instance{
            mob.x - offset, mob.y, mob.z - offset,
            mob.yaw - 1.57079633f, bob
        };
        if (mob.kind == MobKind::Pig) pigs.push_back(instance);
        else if (mob.kind == MobKind::Cow) cows.push_back(instance);
        else sheep.push_back(instance);
    }
}

// ============================================================================
//  GENERACION DEL TERRENO
// ============================================================================

// ----------------------------------------------------------------------------
//  surfaceHeight
//  Entradas: gx, gz  - coordenadas enteras de la columna
//            seed    - semilla del mundo
//            relief  - multiplicador de relieve pedido por el usuario
//            biome   - bioma activo (aporta su propio factor de relieve)
//  Salida  : indice Y del bloque mas alto de esa columna
//  Descripcion: suma varias octavas de ruido para obtener colinas con detalle a
//  distintas escalas. Es una funcion pura: no depende de ningun estado global,
//  por lo que puede evaluarse en paralelo sobre columnas distintas.
// ----------------------------------------------------------------------------
static int surfaceHeight(int gx, int gz, uint32_t seed, float relief, const Biome& biome) {
    float n = fbm2D(static_cast<float>(gx) * kNoiseFrequency,
                    static_cast<float>(gz) * kNoiseFrequency,
                    seed, 5);
    float amplitude = kReliefAmplitud * relief * biome.reliefScale;
    int h = static_cast<int>(kBaseHeight + n * amplitude);
    return h < 1 ? 1 : h;
}

// ----------------------------------------------------------------------------
//  averageColumnBlocks
//  Entradas: seed, relief, biome - parametros de generacion
//  Salida  : promedio estimado de bloques que aporta cada columna
//  Descripcion: muestrea un numero fijo de columnas dispersas para estimar el
//  tamano del mundo sin tener que generarlo. Permite deducir el lado de la
//  reticula que produce aproximadamente los N bloques pedidos por el usuario.
// ----------------------------------------------------------------------------
static float averageColumnBlocks(uint32_t seed, float relief, const Biome& biome) {
    const int kSamples = 4096;
    double total = 0.0;
    for (int i = 0; i < kSamples; ++i) {
        // Coordenadas dispersas y deterministas para cubrir bien el dominio.
        int gx = static_cast<int>(hashUint(seed ^ (0x1000u + i)) % 100000u);
        int gz = static_cast<int>(hashUint(seed ^ (0x2000u + i)) % 100000u);
        total += surfaceHeight(gx, gz, seed, relief, biome) + 1;  // +1 por incluir y=0
    }
    float perColumn = static_cast<float>(total / kSamples);

    // La vegetacion agrega bloques por encima de la superficie: se estima su
    // aporte promedio para que el N final no se desvie demasiado del pedido.
    perColumn += biome.treeChance * 42.0f;
    return perColumn;
}

// ----------------------------------------------------------------------------
//  chooseStratum
//  Entradas: y             - altura de la celda
//            surface       - altura de la superficie de esa columna
//            gx, gz        - coordenadas de la columna
//            seed          - semilla del mundo
//            biome         - bioma activo
//  Salida  : tipo de bloque que corresponde a esa celda
//  Descripcion: implementa la estratificacion vertical del pipeline. De abajo
//  hacia arriba: roca madre, estrato profundo, cuerpo de piedra con vetas de
//  variantes y minerales, subsuelo y, por ultimo, la capa de superficie que
//  define el bioma.
// ----------------------------------------------------------------------------
static BlockId chooseStratum(int y, int surface, int gx, int gz,
                             uint32_t seed, const Biome& biome) {
    // --- Capa de superficie --------------------------------------------------
    if (y == surface)                 return biome.surface;
    if (y >= surface - 3)             return biome.subsurface;

    // --- Roca madre: piso irregular de una o dos celdas de espesor -----------
    if (y == 0)                       return BlockId::Bedrock;
    if (y == 1 && hashToUnitFloat(hashCoords(gx, gz, seed ^ 0xB3D20Cu)) < 0.55f)
        return BlockId::Bedrock;

    // --- Estrato profundo: el 30% inferior de la columna ---------------------
    if (y < surface * 3 / 10)         return BlockId::Deepslate;

    // --- Cuerpo de piedra con vetas ------------------------------------------
    // Un ruido 3D de frecuencia alta crea bolsones compactos en vez de puntos
    // sueltos, que es como se ven las vetas reales del juego.
    float vein = fbm3D(gx * 0.12f, y * 0.16f, gz * 0.12f, seed ^ 0x5EED77u, 3);
    if (vein > 0.78f)                 return BlockId::Andesite;
    if (vein < 0.20f)                 return BlockId::Granite;
    if (vein > 0.72f && vein <= 0.78f) return BlockId::Diorite;

    // Los minerales son mas escasos y se concentran en las capas bajas.
    float ore = hashToUnitFloat(hashCoords(gx, y, gz, seed ^ 0x0A0B0Cu));
    float depthFactor = 1.0f - static_cast<float>(y) / static_cast<float>(surface + 1);
    if (ore > 0.995f)                 return BlockId::CoalOre;
    if (ore > 0.990f && depthFactor > 0.5f) return BlockId::IronOre;
    if (ore < 0.004f)                 return BlockId::Gravel;

    return BlockId::Stone;
}

// Elige una cuadricula de chunks lo mas cercana posible a un cuadrado y con un
// chunk exacto por hilo. Para 12 hilos, por ejemplo, produce 4 x 3.
static void chooseChunkGrid(int threadCount, int& chunksX, int& chunksZ) {
    chunksX = static_cast<int>(std::sqrt(static_cast<double>(threadCount)));
    if (chunksX < 1) chunksX = 1;
    while (threadCount % chunksX != 0) --chunksX;
    chunksZ = threadCount / chunksX;

    // El mundo es cuadrado; se deja la dimension mayor sobre X solamente para
    // que la salida y el diagrama sean mas faciles de leer.
    if (chunksX < chunksZ) {
        int tmp = chunksX;
        chunksX = chunksZ;
        chunksZ = tmp;
    }
}

// ----------------------------------------------------------------------------
//  plantVegetation
//  Entradas: voxels  - reticula del mundo que se esta llenando
//            world   - dimensiones y bioma del mundo
//  Descripcion: coloca arboles (o cactus en el desierto) sobre las columnas
//  elegidas pseudoaleatoriamente. Divide las escrituras en chunks 2D con halo y
//  respeta un margen en los bordes para que las copas no queden cortadas.
// ----------------------------------------------------------------------------
static void plantVegetation(std::vector<BlockId>& voxels, const World& world) {
    const Biome& biome = *world.biome;
    const int margin = 3;

    // Cada hilo es propietario exclusivo de un chunk rectangular de destino.
    // Para reconstruir las copas que cruzan una frontera examina un halo de dos
    // celdas en X y Z, pero nunca escribe fuera del centro de su propio chunk.
    // No hacen falta atomicos y ningun arbol queda cortado.
    #pragma omp parallel
    {
        const int threadId    = omp_get_thread_num();
        const int threadCount = omp_get_num_threads();
        int chunksX = 1, chunksZ = 1;
        chooseChunkGrid(threadCount, chunksX, chunksZ);

        const int chunkX = threadId % chunksX;
        const int chunkZ = threadId / chunksX;
        const int ownedXBegin = world.side * chunkX / chunksX;
        const int ownedXEnd   = world.side * (chunkX + 1) / chunksX;
        const int ownedZBegin = world.side * chunkZ / chunksZ;
        const int ownedZEnd   = world.side * (chunkZ + 1) / chunksZ;

        int rootXBegin = ownedXBegin - kVegetationHalo;
        int rootXEnd   = ownedXEnd + kVegetationHalo;
        int rootZBegin = ownedZBegin - kVegetationHalo;
        int rootZEnd   = ownedZEnd + kVegetationHalo;
        if (rootXBegin < margin) rootXBegin = margin;
        if (rootXEnd > world.side - margin) rootXEnd = world.side - margin;
        if (rootZBegin < margin) rootZBegin = margin;
        if (rootZEnd > world.side - margin) rootZEnd = world.side - margin;

        for (int gz = rootZBegin; gz < rootZEnd; ++gz) {
            for (int gx = rootXBegin; gx < rootXEnd; ++gx) {
                uint32_t h = hashCoords(gx, gz, world.seed ^ 0x7EEE00u);
                if (hashToUnitFloat(h) >= biome.treeChance) continue;

                int base = world.heightMap[static_cast<size_t>(gz) * world.side + gx];

                // El cactus solo ocupa su columna y lo escribe el chunk que
                // contiene esa coordenada X,Z.
                if (biome.logType == BlockId::Cactus) {
                    if (gx >= ownedXBegin && gx < ownedXEnd &&
                        gz >= ownedZBegin && gz < ownedZEnd) {
                        int tall = 2 + static_cast<int>(hashUint(h) % 3u);
                        for (int k = 1; k <= tall; ++k) {
                            int y = base + k;
                            if (y >= world.height) break;
                            voxels[world.index(gx, y, gz)] = BlockId::Cactus;
                        }
                    }
                    continue;
                }

                // --- Arbol: tronco vertical mas copa de hojas -----------------
                int trunk = 4 + static_cast<int>(hashUint(h ^ 0x99u) % 3u);
                int top   = base + trunk;
                if (top + 2 >= world.height) continue;

                if (gx >= ownedXBegin && gx < ownedXEnd &&
                    gz >= ownedZBegin && gz < ownedZEnd) {
                    for (int k = 1; k <= trunk; ++k) {
                        voxels[world.index(gx, base + k, gz)] = biome.logType;
                    }
                }

                // Una copa puede cruzar dos fronteras, pero cada celda se
                // escribe solo cuando X y Z pertenecen al chunk actual.
                for (int level = -2; level <= 1; ++level) {
                    int y = top + level;
                    if (y <= base || y >= world.height) continue;
                    int radius = (level <= -1) ? 2 : 1;

                    for (int dz = -radius; dz <= radius; ++dz) {
                        int nz = gz + dz;
                        if (nz < ownedZBegin || nz >= ownedZEnd) continue;

                        for (int dx = -radius; dx <= radius; ++dx) {
                            if (dx == 0 && dz == 0 && level < 1) continue;

                            if (std::abs(dx) == radius && std::abs(dz) == radius) {
                                uint32_t cornerHash = hashCoords(gx + dx, gz + dz,
                                                                 world.seed ^ 0xC0FFEEu);
                                if (hashToUnitFloat(cornerHash) < 0.6f) continue;
                            }

                            int nx = gx + dx;
                            if (nx < ownedXBegin || nx >= ownedXEnd) continue;

                            size_t idx = world.index(nx, y, nz);
                            if (voxels[idx] == BlockId::Air) voxels[idx] = biome.leafType;
                        }
                    }
                }
            }
        }
    }
}

// ----------------------------------------------------------------------------
//  generateVoxels                                  <<< PUNTO CALIENTE 1 (O(N)) >>>
//  Entradas: world - mundo con side, height, seed y bioma ya definidos
//            relief - multiplicador de relieve
//  Salidas : world.heightMap y el arreglo de voxeles devuelto
//  Descripcion: recorre las side*side columnas y decide el tipo de cada celda.
//  En la version paralela este es el bucle que se reparte entre hilos, tal como
//  describe el pipeline: el terreno se divide segun la cantidad de hilos y cada
//  hilo llena su porcion a partir de la misma semilla compartida.
// ----------------------------------------------------------------------------
static std::vector<BlockId> generateVoxels(World& world, float relief) {
    const Biome& biome = *world.biome;

    // Paso 1: mapa de alturas de la superficie, una entrada por columna.
    world.heightMap.assign(static_cast<size_t>(world.side) * world.side, 0);
    int maxSurface = 1;
    #pragma omp parallel for collapse(2) schedule(static) reduction(max:maxSurface)
    for (int gz = 0; gz < world.side; ++gz) {
        for (int gx = 0; gx < world.side; ++gx) {
            int h = surfaceHeight(gx, gz, world.seed, relief, biome);
            world.heightMap[static_cast<size_t>(gz) * world.side + gx] = h;
            if (h > maxSurface) maxSurface = h;
        }
    }

    // La altura total reserva espacio para la vegetacion que crece por encima.
    world.height = maxSurface + 10;

    // Paso 2: llenado de la reticula tridimensional segun los estratos.
    std::vector<BlockId> voxels(static_cast<size_t>(world.side) * world.side * world.height,
                                BlockId::Air);
    #pragma omp parallel for collapse(2) schedule(static)
    for (int gz = 0; gz < world.side; ++gz) {
        for (int gx = 0; gx < world.side; ++gx) {
            int surface = world.heightMap[static_cast<size_t>(gz) * world.side + gx];
            for (int y = 0; y <= surface; ++y) {
                voxels[world.index(gx, y, gz)] =
                    chooseStratum(y, surface, gx, gz, world.seed, biome);
            }
        }
    }

    // Paso 3: vegetacion sobre la superficie.
    plantVegetation(voxels, world);
    return voxels;
}

// ----------------------------------------------------------------------------
//  buildBlockList
//  Entradas: voxels - reticula ya llenada
//            world  - mundo destino
//            cfg    - configuracion (tiempos de la animacion)
//  Salidas : world.blocks y world.occupancy quedan inicializados
//  Descripcion: convierte la reticula en la lista compacta de bloques que se
//  anima y se dibuja. El recorrido es Y-mayor, asi que la lista queda ordenada
//  por capas de abajo hacia arriba, que es el orden de construccion. A cada
//  bloque se le asigna el instante en que aparece y el instante en que se
//  desvanece, ambos derivados de su capa.
// ----------------------------------------------------------------------------
static void buildBlockList(const std::vector<BlockId>& voxels, World& world,
                           const AppConfig& cfg) {
    world.occupancy.assign(voxels.size(), 0);
    const float layers = static_cast<float>(world.height);
    const float dissolveStart = cfg.buildSeconds + cfg.holdSeconds;

    // La reticula ya usa el orden lineal Y, Z, X que necesita la lista final.
    // Cada hilo cuenta primero los bloques de un intervalo contiguo; una suma de
    // prefijos asigna despues un rango exclusivo y conserva exactamente el orden
    // de la version secuencial sin atomicos ni push_back concurrentes.
    const size_t voxelCount = voxels.size();
    std::vector<size_t> prefix(static_cast<size_t>(omp_get_max_threads()) + 1, 0);

    #pragma omp parallel
    {
        const int threadId    = omp_get_thread_num();
        const int threadCount = omp_get_num_threads();
        const size_t begin = voxelCount * static_cast<size_t>(threadId) /
                             static_cast<size_t>(threadCount);
        const size_t end   = voxelCount * static_cast<size_t>(threadId + 1) /
                             static_cast<size_t>(threadCount);

        size_t localCount = 0;
        for (size_t i = begin; i < end; ++i) {
            if (voxels[i] != BlockId::Air) ++localCount;
        }
        prefix[static_cast<size_t>(threadId) + 1] = localCount;

        #pragma omp barrier
        #pragma omp single
        {
            for (int t = 1; t <= threadCount; ++t) {
                prefix[static_cast<size_t>(t)] += prefix[static_cast<size_t>(t - 1)];
            }
            world.blocks.clear();
            world.blocks.resize(prefix[static_cast<size_t>(threadCount)]);
        }
        #pragma omp barrier

        const size_t layerArea = static_cast<size_t>(world.side) * world.side;
        size_t output = prefix[static_cast<size_t>(threadId)];

        for (size_t i = begin; i < end; ++i) {
            BlockId id = voxels[i];
            if (id == BlockId::Air) continue;

            int y = static_cast<int>(i / layerArea);
            size_t withinLayer = i % layerArea;
            int gz = static_cast<int>(withinLayer / static_cast<size_t>(world.side));
            int gx = static_cast<int>(withinLayer % static_cast<size_t>(world.side));
            float layerFraction = static_cast<float>(y) / layers;
            uint32_t h = hashCoords(gx, y, gz, world.seed ^ 0x51A7Eu);

            Block b;
            b.gx    = static_cast<int16_t>(gx);
            b.gy    = static_cast<int16_t>(y);
            b.gz    = static_cast<int16_t>(gz);
            b.id    = id;
            b.shade = static_cast<uint8_t>(h & 0xFFu);
            b.state = BlockState::Waiting;

            float jitter = hashToUnitFloat(hashUint(h)) * (cfg.buildSeconds / layers);
            b.revealAt = layerFraction * cfg.buildSeconds + jitter;

            float vanishJitter = hashToUnitFloat(hashUint(h ^ 0xABCDu)) *
                                 (cfg.dissolveSeconds / layers);
            b.vanishAt = dissolveStart +
                         (1.0f - layerFraction) * cfg.dissolveSeconds + vanishJitter;

            b.posY  = static_cast<float>(y) + kDropHeight;
            b.velY  = 0.0f;
            b.scale = 0.0f;

            world.blocks[output++] = b;
        }
    }
}

// ----------------------------------------------------------------------------
//  generateWorld
//  Entradas: cfg   - configuracion del programa
//            seed  - semilla del nuevo mundo
//  Salidas : world - mundo completamente generado y listo para animarse
//  Retorno : tiempo en milisegundos que tomo la generacion
//  Descripcion: orquesta la generacion completa. Primero elige el bioma, luego
//  deduce el lado de la reticula que aproxima los N bloques pedidos, y por
//  ultimo llena la reticula y arma la lista de bloques.
// ----------------------------------------------------------------------------
static double generateWorld(const AppConfig& cfg, uint32_t seed, World& world) {
    auto t0 = std::chrono::steady_clock::now();

    world.seed = seed;
    // La interfaz acepta cualquier indice no negativo; se normaliza antes de
    // usarlo tanto en el catalogo como en la tabla de texturas.
    world.biomeIndex = (cfg.biomeIndex >= 0)
                     ? cfg.biomeIndex % kBiomeCount
                     : static_cast<int>(hashUint(seed) % static_cast<uint32_t>(kBiomeCount));
    world.biome = &biomeAt(world.biomeIndex);

    // Lado de la reticula que produce aproximadamente N bloques.
    float perColumn = averageColumnBlocks(seed, cfg.reliefScale, *world.biome);
    if (perColumn < 1.0f) perColumn = 1.0f;
    int side = static_cast<int>(std::lround(std::sqrt(cfg.targetBlocks / perColumn)));
    world.side = side < 4 ? 4 : side;

    std::vector<BlockId> voxels = generateVoxels(world, cfg.reliefScale);
    buildBlockList(voxels, world, cfg);

    auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

// ============================================================================
//  ANIMACION Y CONSTRUCCION DEL BUFFER DE INSTANCIAS
// ============================================================================

// ----------------------------------------------------------------------------
//  updateWorld                                     <<< PUNTO CALIENTE 2 (O(N)) >>>
//  Entradas: world       - mundo a actualizar
//            elapsed     - segundos transcurridos desde que inicio el ciclo
//            dt          - duracion del fotograma en segundos
//  Salidas : liveBlocks  - cuantos bloques siguen sin desaparecer
//  Descripcion: avanza la maquina de estados y la fisica de CADA bloque. Los
//  bloques que aterrizan marcan su celda como ocupada y los que se desvanecen
//  la liberan; esa reticula de ocupacion es la que despues permite descartar
//  las caras interiores.
//
//  Dos bloques distintos nunca escriben la misma celda de occupancy. La barrera
//  implicita al terminar el parallel for garantiza que buildInstanceBuffer vea
//  la reticula completa antes de comenzar su propia region paralela.
// ----------------------------------------------------------------------------
static void updateWorld(World& world, float elapsed, float dt, size_t& liveBlocks) {
    size_t alive = 0;
    const long long blockCount = static_cast<long long>(world.blocks.size());

    #pragma omp parallel for schedule(static) reduction(+:alive)
    for (long long i = 0; i < blockCount; ++i) {
        Block& b = world.blocks[static_cast<size_t>(i)];
        const float targetY = static_cast<float>(b.gy);

        switch (b.state) {
            case BlockState::Waiting:
                if (elapsed >= b.revealAt) {
                    b.state = BlockState::Falling;
                    b.posY  = targetY + kDropHeight;
                    b.velY  = 0.0f;
                    b.scale = 0.0f;
                }
                break;

            case BlockState::Falling: {
                // Integracion de Euler de la gravedad.
                b.velY += kGravity * dt;
                b.posY -= b.velY * dt;

                // Crecimiento rapido del bloque al aparecer.
                b.scale += dt / kPopSeconds;
                if (b.scale > 1.0f) b.scale = 1.0f;

                if (b.posY <= targetY) {
                    b.posY  = targetY;
                    b.velY  = 0.0f;
                    b.scale = 1.0f;
                    b.state = BlockState::Placed;
                    world.occupancy[world.index(b.gx, b.gy, b.gz)] = 1;
                }
                break;
            }

            case BlockState::Placed:
                if (elapsed >= b.vanishAt) {
                    b.state = BlockState::Vanishing;
                    // Al liberar la celda, los bloques vecinos vuelven a
                    // mostrar la cara que estaba oculta.
                    world.occupancy[world.index(b.gx, b.gy, b.gz)] = 0;
                }
                break;

            case BlockState::Vanishing:
                b.scale -= dt / kShrinkSeconds;
                if (b.scale <= 0.0f) {
                    b.scale = 0.0f;
                    b.state = BlockState::Gone;
                }
                break;

            case BlockState::Gone:
                break;
        }

        if (b.state != BlockState::Gone) ++alive;
    }

    liveBlocks = alive;
}

// ----------------------------------------------------------------------------
//  buildInstanceBuffer                             <<< PUNTO CALIENTE 3 (O(N)) >>>
//  Entradas: world     - mundo ya actualizado en este fotograma
//            faceTex   - tabla de texturas empaquetadas por tipo de bloque
//            instances - arreglo destino, con capacidad para todos los bloques
//  Salida  : cantidad de instancias efectivamente escritas
//  Descripcion: descarta los bloques invisibles y empaqueta el resto en el
//  formato que consume la GPU. Un bloque se descarta si ya desaparecio o si
//  esta completamente rodeado por celdas ocupadas, porque en ese caso ninguna
//  de sus seis caras es observable. Este descarte es lo que mantiene los FPS
//  altos cuando N crece: en un mundo armado solo se dibuja su cascara.
//
//  Escribir en "instances" es una compactacion: cada hilo produce una cantidad
//  distinta. Se resuelve con conteos locales y una suma de prefijos, evitando
//  tanto el contador atomico como un orden de salida no determinista.
// ----------------------------------------------------------------------------
static bool blockIsVisible(const World& world, const Block& b) {
    if (b.state == BlockState::Gone || b.scale <= 0.0f) return false;

    // Los bloques que aun caen se ven desde cualquier lado. Solo los asentados
    // pueden descartarse por estar completamente rodeados.
    if (b.state == BlockState::Placed &&
        world.occupiedAt(b.gx + 1, b.gy, b.gz) &&
        world.occupiedAt(b.gx - 1, b.gy, b.gz) &&
        world.occupiedAt(b.gx, b.gy + 1, b.gz) &&
        world.occupiedAt(b.gx, b.gy - 1, b.gz) &&
        world.occupiedAt(b.gx, b.gy, b.gz + 1) &&
        world.occupiedAt(b.gx, b.gy, b.gz - 1)) {
        return false;
    }
    return true;
}

static size_t buildInstanceBuffer(const World& world, const uint32_t* faceTex,
                                  InstanceData* instances, uint8_t* visibleFlags) {
    const float offset = world.side * 0.5f - 0.5f;
    const size_t blockCount = world.blocks.size();
    std::vector<size_t> prefix(static_cast<size_t>(omp_get_max_threads()) + 1, 0);
    size_t visibleTotal = 0;

    // Primera pasada: cada hilo evalua la visibilidad de sus bloques, guarda el
    // resultado en visibleFlags y cuenta cuantos son visibles. La suma de
    // prefijos asigna rangos de salida exclusivos y deterministas. La segunda
    // pasada solo consulta la marca ya calculada, de modo que las seis lecturas
    // dispersas de la rejilla de ocupacion se hacen una sola vez por bloque y
    // por fotograma, en lugar de dos. Cada hilo escribe unicamente las
    // posiciones de visibleFlags que caen en su propio intervalo.
    #pragma omp parallel shared(visibleTotal)
    {
        const int threadId    = omp_get_thread_num();
        const int threadCount = omp_get_num_threads();
        const size_t begin = blockCount * static_cast<size_t>(threadId) /
                             static_cast<size_t>(threadCount);
        const size_t end   = blockCount * static_cast<size_t>(threadId + 1) /
                             static_cast<size_t>(threadCount);

        size_t localCount = 0;
        for (size_t i = begin; i < end; ++i) {
            const bool visible = blockIsVisible(world, world.blocks[i]);
            visibleFlags[i] = visible ? 1u : 0u;
            if (visible) ++localCount;
        }
        prefix[static_cast<size_t>(threadId) + 1] = localCount;

        #pragma omp barrier
        #pragma omp single
        {
            for (int t = 1; t <= threadCount; ++t) {
                prefix[static_cast<size_t>(t)] += prefix[static_cast<size_t>(t - 1)];
            }
            visibleTotal = prefix[static_cast<size_t>(threadCount)];
        }
        #pragma omp barrier

        size_t output = prefix[static_cast<size_t>(threadId)];
        for (size_t i = begin; i < end; ++i) {
            if (!visibleFlags[i]) continue;   // resultado memoizado en la pasada 1
            const Block& b = world.blocks[i];

            InstanceData& inst = instances[output++];
            inst.x       = static_cast<float>(b.gx) - offset;
            inst.y       = b.posY;
            inst.z       = static_cast<float>(b.gz) - offset;
            inst.faceTex = faceTex[static_cast<int>(b.id)] |
                           (static_cast<uint32_t>(b.shade) << 24);
            inst.scale   = b.scale;
        }
    }

    return visibleTotal;
}

struct NearShadowView {
    glm::mat4 lightViewProj{1.0f};
    glm::vec3 focus{0.0f};
    float radius = 0.0f;
};

// Centra el volumen de sombras en la posicion horizontal de la camara. Cuando
// una toma queda fuera del terreno se usa el punto mas cercano del borde; asi
// una camara exterior concentra la resolucion en la esquina o lado que ocupa,
// no en el centro que esta observando.
static NearShadowView calculateNearShadowView(const World& world,
                                               const AppConfig& cfg,
                                               const CameraView& camera,
                                               const SceneLighting& lighting) {
    const float worldHalf = std::max(1.0f, world.side * 0.5f - 1.0f);
    NearShadowView result{};
    result.focus.x = std::max(-worldHalf, std::min(worldHalf, camera.eye.x));
    result.focus.z = std::max(-worldHalf, std::min(worldHalf, camera.eye.z));
    result.focus.y = terrainTopAt(world, result.focus.x, result.focus.z) +
                     std::max(3.0f, world.height * 0.10f);
    result.radius = std::min(cfg.shadowDistance,
                             std::max(18.0f, world.side * 0.70f));

    const glm::vec3 lightDirection = glm::normalize(lighting.direction);
    const float lightDistance = result.radius * 2.8f + world.height;
    const glm::vec3 lightEye = result.focus + lightDirection * lightDistance;
    const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    const glm::vec3 lightUp = std::abs(glm::dot(lightDirection, worldUp)) > 0.92f
                            ? glm::vec3(0.0f, 0.0f, 1.0f)
                            : worldUp;
    const glm::mat4 lightView = glm::lookAt(lightEye, result.focus, lightUp);
    const glm::mat4 lightProjection = glm::ortho(
        -result.radius, result.radius, -result.radius, result.radius,
        0.5f, lightDistance + result.radius * 2.5f + world.height);
    result.lightViewProj = lightProjection * lightView;
    return result;
}

// Compactacion paralela estable de los bloques que pueden proyectar sombras en
// la zona local. Cada hilo escribe en un rango exclusivo calculado por prefijo.
static size_t buildNearbyShadowInstances(const InstanceData* source,
                                         size_t count,
                                         const glm::vec3& focus,
                                         float radius,
                                         std::vector<InstanceData>& output) {
    std::vector<size_t> prefix(static_cast<size_t>(omp_get_max_threads()) + 1, 0);
    const float paddedRadius = radius + 6.0f;
    const float radiusSquared = paddedRadius * paddedRadius;
    size_t nearbyTotal = 0;

    #pragma omp parallel shared(nearbyTotal, output)
    {
        const int threadId = omp_get_thread_num();
        const int threadCount = omp_get_num_threads();
        const size_t begin = count * static_cast<size_t>(threadId) /
                             static_cast<size_t>(threadCount);
        const size_t end = count * static_cast<size_t>(threadId + 1) /
                           static_cast<size_t>(threadCount);

        size_t localCount = 0;
        for (size_t i = begin; i < end; ++i) {
            const float dx = source[i].x - focus.x;
            const float dz = source[i].z - focus.z;
            if (dx * dx + dz * dz <= radiusSquared) ++localCount;
        }
        prefix[static_cast<size_t>(threadId) + 1] = localCount;

        #pragma omp barrier
        #pragma omp single
        {
            for (int t = 0; t < threadCount; ++t) {
                prefix[static_cast<size_t>(t) + 1] += prefix[static_cast<size_t>(t)];
            }
            nearbyTotal = prefix[static_cast<size_t>(threadCount)];
            output.resize(nearbyTotal);
        }
        #pragma omp barrier

        size_t writeIndex = prefix[static_cast<size_t>(threadId)];
        for (size_t i = begin; i < end; ++i) {
            const float dx = source[i].x - focus.x;
            const float dz = source[i].z - focus.z;
            if (dx * dx + dz * dz <= radiusSquared) {
                output[writeIndex++] = source[i];
            }
        }
    }

    return nearbyTotal;
}

// ============================================================================
//  PREPARACION DEL ATLAS
// ============================================================================

// ----------------------------------------------------------------------------
//  registerAllTextures
//  Entradas: atlas - atlas donde registrar las texturas
//  Salidas : faceTexTables - una tabla por bioma, indexada por BlockId, con las
//            tres capas del atlas ya empaquetadas
//  Retorno : true si todas las capas caben en el rango de 8 bits del formato
//  Descripcion: registra por adelantado las texturas de todos los tipos de
//  bloque en todos los biomas, para que cambiar de mundo no obligue a rehacer
//  el atlas. Las especificaciones repetidas comparten capa automaticamente.
// ----------------------------------------------------------------------------
static bool registerAllTextures(TextureAtlas& atlas,
                                std::vector<std::vector<uint32_t>>& faceTexTables,
                                std::string& error) {
    faceTexTables.assign(kBiomeCount, std::vector<uint32_t>(kBlockTypeCount, 0));

    for (int b = 0; b < kBiomeCount; ++b) {
        const Biome& biome = biomeAt(b);
        for (int t = 0; t < kBlockTypeCount; ++t) {
            BlockTextures tex = blockTextures(static_cast<BlockId>(t), biome);
            int top    = atlas.registerTexture(tex.top);
            int side   = atlas.registerTexture(tex.side);
            int bottom = atlas.registerTexture(tex.bottom);
            faceTexTables[b][t] = packFaceTex(top, side, bottom, 0);
        }
    }

    // El formato empaqueta cada indice de capa en 8 bits.
    if (atlas.layerCount() > 255) {
        error = "el atlas necesita " + std::to_string(atlas.layerCount()) +
                " capas y el formato solo admite 255";
        return false;
    }
    return true;
}

// Semilla usada al medir cuando el usuario no indica una: fija el mundo para
// que todas las repeticiones y ambas versiones trabajen sobre lo mismo.
static constexpr uint32_t kBenchmarkSeed = 20260901u;

// ----------------------------------------------------------------------------
//  elapsedMsAndReset
//  Entradas/Salidas: mark - marca de tiempo que se adelanta al instante actual
//  Retorno : milisegundos transcurridos desde esa marca
//  Descripcion: cronometra una etapa del fotograma y deja la marca lista para
//  la siguiente, sin repetir la misma expresion de std::chrono en cada etapa.
// ----------------------------------------------------------------------------
static double elapsedMsAndReset(std::chrono::steady_clock::time_point& mark) {
    const auto ahora = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(ahora - mark).count();
    mark = ahora;
    return ms;
}

// ============================================================================
//  PROGRAMA PRINCIPAL
// ============================================================================
int main(int argc, char** argv) {
    // --- 1. Captura y validacion de argumentos -------------------------------
    AppConfig cfg;
    std::string error;
    if (!parseArguments(argc, argv, cfg, error)) {
        std::fprintf(stderr, "Error: %s\n\n", error.c_str());
        printUsage(argv[0]);
        return EXIT_FAILURE;
    }
    if (cfg.showHelp) {
        printUsage(argv[0]);
        return EXIT_SUCCESS;
    }
    if (cfg.shadowMode == "near" && cfg.lightingMode != "cycle") {
        std::fprintf(stderr,
                     "Error: --shadows near requiere --lighting cycle.\n");
        return EXIT_FAILURE;
    }
    // Se desactiva el ajuste dinamico para que --threads sea reproducible en
    // las mediciones. Sin la opcion se usa la cantidad maxima del sistema.
    omp_set_dynamic(0);
    if (cfg.threads != 0) omp_set_num_threads(cfg.threads);
    const int openmpThreads = omp_get_max_threads();
    int chunkColumns = 1, chunkRows = 1;
    chooseChunkGrid(openmpThreads, chunkColumns, chunkRows);

    // --- Modo de medicion ----------------------------------------------------
    // Con --benchmark el programa deja de depender del reloj real, del monitor
    // y del azar: paso de tiempo fijo, sin sincronizacion vertical y con una
    // semilla conocida, para que ambas versiones midan el mismo trabajo.
    Benchmark bench;
    bench.start(cfg);
    if (bench.active()) {
        cfg.vsync = false;
        if (cfg.seed == 0) cfg.seed = kBenchmarkSeed;
    }

    // --- 2. Localizacion de las texturas -------------------------------------
    std::string assetsDir = findAssetsDirectory(cfg.assetsDir);
    if (assetsDir.empty()) {
        std::fprintf(stderr,
                     "Error: no se encontro la carpeta de texturas.\n"
                     "Ejecute el programa desde la raiz del proyecto o indique la ruta\n"
                     "con --assets <ruta a assets/textures>.\n");
        return EXIT_FAILURE;
    }

    // --- 3. Ventana y contexto de OpenGL -------------------------------------
    if (!glfwInit()) {
        std::fprintf(stderr, "Error: no se pudo inicializar GLFW.\n");
        return EXIT_FAILURE;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);  // requerido en macOS

    GLFWwindow* window = glfwCreateWindow(cfg.windowWidth, cfg.windowHeight,
                                          "Minecraft Screen Saver - Paralelo",
                                          nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "Error: no se pudo crear la ventana de %dx%d.\n",
                     cfg.windowWidth, cfg.windowHeight);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(cfg.vsync ? 1 : 0);

    glewExperimental = GL_TRUE;
    GLenum glewStatus = glewInit();
    if (glewStatus != GLEW_OK) {
        std::fprintf(stderr, "Error: no se pudo inicializar GLEW: %s\n",
                     glewGetErrorString(glewStatus));
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }
    glGetError();  // GLEW puede dejar un error espurio en el contexto core

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    // --- 4. Atlas de texturas y renderizador ---------------------------------
    TextureAtlas atlas;
    std::vector<std::vector<uint32_t>> faceTexTables;
    if (!registerAllTextures(atlas, faceTexTables, error)) {
        std::fprintf(stderr, "Error: %s\n", error.c_str());
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    auto atlasStart = std::chrono::steady_clock::now();
    if (!atlas.build(assetsDir, error) || !atlas.upload(error)) {
        std::fprintf(stderr, "Error al preparar las texturas: %s\n", error.c_str());
        atlas.destroy();
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }
    double atlasMs = std::chrono::duration<double, std::milli>(
                         std::chrono::steady_clock::now() - atlasStart).count();

    CubeRenderer renderer;
    if (!renderer.init(error)) {
        std::fprintf(stderr, "Error al preparar el renderizador: %s\n", error.c_str());
        atlas.destroy();
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    const bool dynamicLighting = cfg.lightingMode == "cycle";
    const bool shadowsEnabled = dynamicLighting && cfg.shadowMode == "near";
    SkyRenderer skyRenderer;
    if (dynamicLighting && !skyRenderer.init(error)) {
        std::fprintf(stderr, "Error al preparar el skybox: %s\n", error.c_str());
        renderer.destroy();
        atlas.destroy();
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    constexpr int kShadowMapResolution = 2048;
    ShadowMap shadowMap;
    if (shadowsEnabled &&
        (!shadowMap.init(kShadowMapResolution, error) ||
         !renderer.enableShadowPass(error))) {
        std::fprintf(stderr, "Error al preparar las sombras: %s\n", error.c_str());
        shadowMap.destroy();
        skyRenderer.destroy();
        renderer.destroy();
        atlas.destroy();
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    MobRenderer pigRenderer;
    MobRenderer cowRenderer;
    MobRenderer sheepRenderer;
    MobRenderer sheepWoolRenderer;
    if (cfg.mobCount > 0 &&
        (!pigRenderer.init(MobModel::Pig,
                           assetsDir + "/entity/pig/temperate_pig.png", error) ||
         !cowRenderer.init(MobModel::Cow,
                           assetsDir + "/entity/cow/temperate_cow.png", error) ||
         !sheepRenderer.init(MobModel::Sheep,
                             assetsDir + "/entity/sheep/sheep.png", error) ||
         !sheepWoolRenderer.init(MobModel::SheepWool,
                                 assetsDir + "/entity/sheep/sheep_wool.png", error) ||
         (shadowsEnabled &&
          (!pigRenderer.enableShadowPass(error) ||
           !cowRenderer.enableShadowPass(error) ||
           !sheepRenderer.enableShadowPass(error))))) {
        std::fprintf(stderr, "Error al preparar los mobs: %s\n", error.c_str());
        sheepWoolRenderer.destroy();
        sheepRenderer.destroy();
        cowRenderer.destroy();
        pigRenderer.destroy();
        shadowMap.destroy();
        skyRenderer.destroy();
        renderer.destroy();
        atlas.destroy();
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    std::printf("Texturas   : %s (%d capas, %.1f KB, %.1f ms)\n",
                assetsDir.c_str(), atlas.layerCount(),
                atlas.pixelBytes() / 1024.0, atlasMs);
    std::printf("OpenMP     : %d hilos | chunks %dx%d | halo %d\n",
                openmpThreads, chunkColumns, chunkRows, kVegetationHalo);
    if (cfg.cameraMode == "auto") {
        std::printf("Camara     : automatica | cambio cada %.1f s | %d encuadres\n",
                    cfg.cameraChangeSeconds, kAutoCameraShotCount);
    } else {
        std::printf("Camara     : orbita exterior\n");
    }
    std::printf("Mobs       : %d solicitados | cerdos, vacas y ovejas | IA secuencial%s\n",
                cfg.mobCount, cfg.mobCount == 0 ? " (desactivados)" : "");
    if (dynamicLighting) {
        std::printf("Iluminacion: ciclo dia/noche | %.1f s | skybox procedural\n",
                    cfg.dayCycleSeconds);
    } else {
        std::printf("Iluminacion: clasica (use --lighting cycle para activar el ciclo)\n");
    }
    if (shadowsEnabled) {
        std::printf("Sombras    : cercanas | radio %.1f bloques | mapa %dx%d\n",
                    cfg.shadowDistance, shadowMap.resolution(), shadowMap.resolution());
    } else {
        std::printf("Sombras    : desactivadas (use --shadows near con iluminacion cycle)\n");
    }
    if (bench.active()) {
        std::printf("Medicion   : %d fotogramas medidos, %d de calentamiento | "
                    "paso fijo 1/60 s | vsync desactivado | semilla %u\n",
                    cfg.benchFrames, cfg.benchWarmup, cfg.seed);
    }

    // --- 5. Generacion del primer mundo --------------------------------------
    // Semilla: la indicada por el usuario, o una sorteada por el sistema.
    std::random_device randomDevice;
    uint32_t seed = (cfg.seed != 0) ? cfg.seed : randomDevice();

    World world;
    double genMs = generateWorld(cfg, seed, world);
    bench.addGeneration(genMs);
    // Con la semilla fija todas las repeticiones producen el mismo mundo, asi
    // que el promedio mide unicamente el costo de generarlo.
    for (int rep = 1; bench.active() && rep < bench.generations(); ++rep) {
        bench.addGeneration(generateWorld(cfg, seed, world));
    }

    std::vector<InstanceData> instances(world.blocks.size());
    std::vector<InstanceData> shadowInstances;
    // Una marca de visibilidad por bloque, reutilizada en cada fotograma.
    std::vector<uint8_t> visibleFlags(world.blocks.size(), 0);
    std::vector<Mob> mobs;
    std::vector<MobInstanceData> pigInstances;
    std::vector<MobInstanceData> cowInstances;
    std::vector<MobInstanceData> sheepInstances;

    std::printf("Mundo      : semilla %u | bioma %s | reticula %dx%dx%d\n"
                "             bloques %zu (N pedido %ld) | generado en %.1f ms\n",
                world.seed, world.biome->name, world.side, world.height, world.side,
                world.blocks.size(), cfg.targetBlocks, genMs);
    std::printf("Controles  : ESC para salir, ESPACIO para generar otro mundo.\n");
    std::fflush(stdout);

    // --- 6. Bucle principal ---------------------------------------------------
    Phase  phase        = Phase::Building;
    double cycleStart   = bench.active() ? 0.0 : glfwGetTime();
    double lastTime     = cycleStart;
    double fpsTimer     = cycleStart;
    int    frameCount   = 0;
    int    displayedFps = 0;
    double phaseEndTime = 0.0;   // instante en que termina la fase actual
    size_t liveBlocks   = world.blocks.size();
    bool   spaceWasDown = false;  // estado previo de ESPACIO (deteccion de flanco)
    size_t drawnBlocks  = 0;
    double cpuMsAccum   = 0.0;   // tiempo de CPU acumulado del fotograma
    bool   mobsSpawned  = false; // aparecen cuando el terreno queda armado

    double benchClock = 0.0;      // reloj virtual usado solo al medir

    while (!glfwWindowShouldClose(window)) {
        const auto frameStart = std::chrono::steady_clock::now();
        FrameTiming timing;       // desglose de esta iteracion, en milisegundos

        // Al medir, el tiempo avanza en pasos fijos de 1/60 s: la simulacion
        // recorre exactamente los mismos estados en la version secuencial y en
        // la paralela sin importar cuanto tarde cada maquina en dibujar. Fuera
        // del modo de medicion se usa el reloj real de GLFW.
        double now;
        float  dt;
        if (bench.active()) {
            benchClock += Benchmark::kFixedDeltaSeconds;
            now = benchClock;
            dt  = static_cast<float>(Benchmark::kFixedDeltaSeconds);
        } else {
            now = glfwGetTime();
            dt  = static_cast<float>(now - lastTime);
            // Un fotograma muy largo (por ejemplo al arrastrar la ventana) haria
            // que los bloques atravesaran el suelo: se acota el paso de tiempo.
            if (dt > 0.05f) dt = 0.05f;
        }
        lastTime = now;

        glfwPollEvents();
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        // --- Maquina de estados del ciclo del protector de pantalla ----------
        float elapsed = static_cast<float>(now - cycleStart);

        // Solo el instante en que se presiona ESPACIO cuenta como peticion, no
        // cada fotograma en que la tecla siga hundida.
        // Al medir se ignora ESPACIO: generar un mundo nuevo fuera de tiempo
        // arruinaria la repetibilidad de la prueba.
        bool spaceIsDown = !bench.active() &&
                           (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS);
        bool regenerate  = spaceIsDown && !spaceWasDown;
        spaceWasDown = spaceIsDown;

        if (phase == Phase::Building && elapsed >= cfg.buildSeconds) {
            phase = Phase::Holding;
        } else if (phase == Phase::Holding && elapsed >= cfg.buildSeconds + cfg.holdSeconds) {
            phase = Phase::Dissolving;
        } else if (phase == Phase::Dissolving && liveBlocks == 0) {
            phase = Phase::Paused;
            phaseEndTime = now + cfg.pauseSeconds;
        } else if (phase == Phase::Paused && now >= phaseEndTime) {
            regenerate = true;
        }

        if (regenerate) {
            // Con una semilla fija el mundo se repite igual en cada ciclo, lo
            // que hace reproducibles las mediciones de tiempo.
            seed = (cfg.seed != 0) ? cfg.seed : randomDevice();
            genMs = generateWorld(cfg, seed, world);
            instances.assign(world.blocks.size(), InstanceData{});
            visibleFlags.assign(world.blocks.size(), 0);
            shadowInstances.clear();
            mobs.clear();
            pigInstances.clear();
            cowInstances.clear();
            sheepInstances.clear();
            mobsSpawned = false;
            liveBlocks = world.blocks.size();
            phase      = Phase::Building;
            cycleStart = bench.active() ? benchClock : glfwGetTime();
            lastTime   = cycleStart;

            std::printf("Mundo nuevo: semilla %u | bioma %s | reticula %dx%dx%d | "
                        "bloques %zu | %.1f ms\n",
                        world.seed, world.biome->name, world.side, world.height,
                        world.side, world.blocks.size(), genMs);
            std::fflush(stdout);
            continue;
        }

        // --- Trabajo de CPU proporcional a N ---------------------------------
        const auto cpuStart = std::chrono::steady_clock::now();
        auto etapa = cpuStart;    // marca movil que cronometra etapa por etapa

        updateWorld(world, elapsed, dt, liveBlocks);
        timing.updateMs = elapsedMsAndReset(etapa);

        if (phase == Phase::Holding && cfg.mobCount > 0) {
            if (!mobsSpawned) {
                mobs = spawnMobs(world, cfg.mobCount);
                mobsSpawned = true;
                std::printf("Mobs activos: %zu de %d solicitados\n",
                            mobs.size(), cfg.mobCount);
                std::fflush(stdout);
            }
            updateMobsSequential(mobs, world, dt);
            buildMobInstances(mobs, world, pigInstances, cowInstances, sheepInstances);
        } else {
            pigInstances.clear();
            cowInstances.clear();
            sheepInstances.clear();
        }
        timing.mobsMs = elapsedMsAndReset(etapa);

        drawnBlocks = buildInstanceBuffer(world, faceTexTables[world.biomeIndex].data(),
                                          instances.data(), visibleFlags.data());

        timing.buildMs = elapsedMsAndReset(etapa);

        // --- Camara ----------------------------------------------------------
        // El modo orbit conserva la toma original. El modo auto alterna entre
        // encuadres exteriores e interiores con una transicion elevada.
        CameraView camera = calculateCamera(world, cfg, now);

        DayNightEnvironment environment{};
        environment.phaseName = "clasica";
        if (dynamicLighting) {
            environment = calculateDayNight(world, cfg, now, camera.eye);
        }

        // La camara y el estado de iluminacion cuestan lo mismo con cualquier N,
        // asi que no forman parte de ninguna etapa cronometrada.
        etapa = std::chrono::steady_clock::now();
        NearShadowView shadowView{};
        size_t shadowBlockCount = 0;
        if (shadowsEnabled) {
            shadowView = calculateNearShadowView(world, cfg, camera,
                                                 environment.lighting);
            shadowBlockCount = buildNearbyShadowInstances(
                instances.data(), drawnBlocks, shadowView.focus,
                shadowView.radius, shadowInstances);
            environment.lighting.shadows = true;
            environment.lighting.lightViewProj = shadowView.lightViewProj;
            environment.lighting.shadowTexture = shadowMap.textureId();
            environment.lighting.shadowStrength = 0.72f;
            environment.lighting.shadowCenter = shadowView.focus;
            environment.lighting.shadowRadius = shadowView.radius;
        }

        timing.shadowMs = elapsedMsAndReset(etapa);
        timing.cpuMs = std::chrono::duration<double, std::milli>(
                           std::chrono::steady_clock::now() - cpuStart).count();
        cpuMsAccum += timing.cpuMs;

        int fbWidth = 0, fbHeight = 0;
        glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
        if (fbHeight == 0) fbHeight = 1;
        glViewport(0, 0, fbWidth, fbHeight);

        glm::mat4 projection = glm::perspective(
            glm::radians(camera.fovDegrees),
            static_cast<float>(fbWidth) / static_cast<float>(fbHeight),
            0.5f, world.side * 6.0f + 200.0f);
        glm::mat4 view     = glm::lookAt(camera.eye, camera.target, camera.up);
        glm::mat4 viewProj = projection * view;

        // --- Dibujo -----------------------------------------------------------
        if (shadowsEnabled) {
            shadowMap.beginDepthPass();
            renderer.drawDepth(shadowInstances.data(), shadowBlockCount,
                               shadowView.lightViewProj);
            pigRenderer.drawDepth(pigInstances.data(), pigInstances.size(),
                                  shadowView.lightViewProj);
            cowRenderer.drawDepth(cowInstances.data(), cowInstances.size(),
                                  shadowView.lightViewProj);
            sheepRenderer.drawDepth(sheepInstances.data(), sheepInstances.size(),
                                    shadowView.lightViewProj);
            shadowMap.endDepthPass(fbWidth, fbHeight);
        }

        if (dynamicLighting) {
            glClearColor(environment.sky.zenithColor.r,
                         environment.sky.zenithColor.g,
                         environment.sky.zenithColor.b, 1.0f);
        } else {
            const glm::vec3 skyColor = rgbColor(world.biome->skyColor);
            glClearColor(skyColor.r, skyColor.g, skyColor.b, 1.0f);
        }
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (dynamicLighting) skyRenderer.draw(projection, view, environment.sky);
        renderer.draw(instances.data(), drawnBlocks, viewProj, atlas.textureId(),
                      environment.lighting);
        pigRenderer.draw(pigInstances.data(), pigInstances.size(), viewProj,
                         environment.lighting);
        cowRenderer.draw(cowInstances.data(), cowInstances.size(), viewProj,
                         environment.lighting);
        sheepRenderer.draw(sheepInstances.data(), sheepInstances.size(), viewProj,
                           environment.lighting);
        sheepWoolRenderer.draw(sheepInstances.data(), sheepInstances.size(), viewProj,
                               environment.lighting);
        glfwSwapBuffers(window);

        // --- Registro de la medicion -----------------------------------------
        // glFinish espera a que la GPU termine el fotograma: sin esa espera el
        // driver podria encolar trabajo y el tiempo medido saldria optimista.
        if (bench.active()) {
            glFinish();
            timing.frameMs = std::chrono::duration<double, std::milli>(
                                 std::chrono::steady_clock::now() - frameStart).count();
            timing.phase = phase == Phase::Building   ? 'B' :
                           phase == Phase::Holding    ? 'H' :
                           phase == Phase::Dissolving ? 'D' : 'P';
            bench.addFrame(timing, drawnBlocks);
            if (bench.complete()) glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        // --- Indicador de FPS en el titulo de la ventana ----------------------
        ++frameCount;
        if (now - fpsTimer >= 1.0) {
            displayedFps = frameCount;
            const char* phaseName =
                phase == Phase::Building   ? "construyendo" :
                phase == Phase::Holding    ? "sostenido"    :
                phase == Phase::Dissolving ? "desarmando"   : "reiniciando";

            char title[256];
            std::snprintf(title, sizeof(title),
                          "Minecraft Screen Saver - Paralelo | %d hilos | FPS %d | %s | cam %s | luz %s | "
                          "bloques %zu / dibujados %zu | mobs %zu | CPU %.2f ms | %s",
                          openmpThreads, displayedFps, world.biome->name,
                          camera.name, environment.phaseName,
                          world.blocks.size(), drawnBlocks,
                          pigInstances.size() + cowInstances.size() + sheepInstances.size(),
                          cpuMsAccum / frameCount, phaseName);
            glfwSetWindowTitle(window, title);

            frameCount = 0;
            cpuMsAccum = 0.0;
            fpsTimer   = now;
        }
    }

    // --- Informe de la medicion ----------------------------------------------
    int exitCode = EXIT_SUCCESS;
    if (bench.active()) {
        BenchmarkInfo info;
        info.version      = "paralelo";
        info.threads      = openmpThreads;
        info.targetBlocks = cfg.targetBlocks;
        info.worldBlocks  = world.blocks.size();
        info.seed         = world.seed;
        info.biome        = world.biome->name;

        std::string benchError;
        if (!bench.report(info, benchError)) {
            std::fprintf(stderr, "Error: %s\n", benchError.c_str());
            exitCode = EXIT_FAILURE;
        }
    }

    // --- 7. Liberacion ordenada de recursos ----------------------------------
    sheepWoolRenderer.destroy();
    sheepRenderer.destroy();
    cowRenderer.destroy();
    pigRenderer.destroy();
    shadowMap.destroy();
    skyRenderer.destroy();
    renderer.destroy();
    atlas.destroy();
    glfwDestroyWindow(window);
    glfwTerminate();
    return exitCode;
}
