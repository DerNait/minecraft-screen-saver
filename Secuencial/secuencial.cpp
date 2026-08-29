// ============================================================================
//  secuencial.cpp - Minecraft Screen Saver, VERSION SECUENCIAL
//  Computacion Paralela y Distribuida - Universidad del Valle de Guatemala
// ----------------------------------------------------------------------------
//  Protector de pantalla que genera un mundo de bloques estilo Minecraft a
//  partir de una semilla aleatoria, lo construye capa por capa de abajo hacia
//  arriba, lo sostiene unos segundos, lo desarma de arriba hacia abajo y vuelve
//  a empezar con un mundo nuevo.
//
//  ESTA VERSION EJECUTA TODO EN UN SOLO HILO. Su proposito es servir de linea
//  base medible para la version paralela. Las tres funciones marcadas mas abajo
//  como "PUNTO CALIENTE" son las que concentran el trabajo proporcional a N y
//  son, por lo tanto, las candidatas a paralelizar con OpenMP:
//
//    1. generateVoxels()        - O(N)  una vez por mundo
//    2. updateWorld()           - O(N)  en cada fotograma
//    3. buildInstanceBuffer()   - O(N)  en cada fotograma
//
//  Elemento de fisica/trigonometria requerido por el enunciado:
//    - Cada bloque cae con aceleracion constante (integracion de Euler de la
//      gravedad) hasta asentarse en su posicion final.
//    - La camara orbita el mundo describiendo una circunferencia mediante
//      funciones trigonometricas (seno y coseno).
// ============================================================================

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <omp.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#include "app_config.hpp"
#include "block_catalog.hpp"
#include "noise.hpp"
#include "render.hpp"
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

// ----------------------------------------------------------------------------
//  plantVegetation
//  Entradas: voxels  - reticula del mundo que se esta llenando
//            world   - dimensiones y bioma del mundo
//            relief  - multiplicador de relieve
//  Descripcion: coloca arboles (o cactus en el desierto) sobre las columnas
//  elegidas pseudoaleatoriamente. Escribe directamente sobre la reticula, y
//  respeta un margen en los bordes para que las copas no queden cortadas.
// ----------------------------------------------------------------------------
static void plantVegetation(std::vector<BlockId>& voxels, const World& world) {
    const Biome& biome = *world.biome;
    const int margin = 3;

    for (int gz = margin; gz < world.side - margin; ++gz) {
        for (int gx = margin; gx < world.side - margin; ++gx) {
            uint32_t h = hashCoords(gx, gz, world.seed ^ 0x7EEE00u);
            if (hashToUnitFloat(h) >= biome.treeChance) continue;

            int base = world.heightMap[static_cast<size_t>(gz) * world.side + gx];

            // El cactus del desierto es una simple columna de 2 a 4 bloques.
            if (biome.logType == BlockId::Cactus) {
                int tall = 2 + static_cast<int>(hashUint(h) % 3u);
                for (int k = 1; k <= tall; ++k) {
                    int y = base + k;
                    if (y >= world.height) break;
                    voxels[world.index(gx, y, gz)] = BlockId::Cactus;
                }
                continue;
            }

            // --- Arbol: tronco vertical mas copa de hojas ---------------------
            int trunk = 4 + static_cast<int>(hashUint(h ^ 0x99u) % 3u);
            int top   = base + trunk;
            if (top + 2 >= world.height) continue;

            for (int k = 1; k <= trunk; ++k) {
                voxels[world.index(gx, base + k, gz)] = biome.logType;
            }

            // La copa se arma en cuatro niveles: dos anchos y dos angostos, con
            // las esquinas recortadas al azar para que no parezca un cubo.
            for (int level = -2; level <= 1; ++level) {
                int y = top + level;
                if (y <= base || y >= world.height) continue;
                int radius = (level <= -1) ? 2 : 1;

                for (int dz = -radius; dz <= radius; ++dz) {
                    for (int dx = -radius; dx <= radius; ++dx) {
                        if (dx == 0 && dz == 0 && level < 1) continue;  // no tapar el tronco

                        // Recorte pseudoaleatorio de las esquinas de la copa.
                        if (std::abs(dx) == radius && std::abs(dz) == radius) {
                            uint32_t cornerHash = hashCoords(gx + dx, gz + dz,
                                                             world.seed ^ 0xC0FFEEu);
                            if (hashToUnitFloat(cornerHash) < 0.6f) continue;
                        }

                        int nx = gx + dx;
                        int nz = gz + dz;
                        if (nx < 0 || nx >= world.side || nz < 0 || nz >= world.side) continue;

                        size_t idx = world.index(nx, y, nz);
                        if (voxels[idx] == BlockId::Air) voxels[idx] = biome.leafType;
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
    // Conteo previo para reservar exactamente la memoria necesaria.
    size_t total = 0;
    for (BlockId id : voxels) {
        if (id != BlockId::Air) ++total;
    }

    world.blocks.clear();
    world.blocks.reserve(total);
    world.occupancy.assign(voxels.size(), 0);

    const float layers = static_cast<float>(world.height);
    const float dissolveStart = cfg.buildSeconds + cfg.holdSeconds;

    for (int y = 0; y < world.height; ++y) {
        // Fraccion vertical de esta capa dentro del mundo: 0 abajo, 1 arriba.
        float layerFraction = static_cast<float>(y) / layers;

        for (int gz = 0; gz < world.side; ++gz) {
            for (int gx = 0; gx < world.side; ++gx) {
                BlockId id = voxels[world.index(gx, y, gz)];
                if (id == BlockId::Air) continue;

                uint32_t h = hashCoords(gx, y, gz, world.seed ^ 0x51A7Eu);

                Block b;
                b.gx    = static_cast<int16_t>(gx);
                b.gy    = static_cast<int16_t>(y);
                b.gz    = static_cast<int16_t>(gz);
                b.id    = id;
                b.shade = static_cast<uint8_t>(h & 0xFFu);
                b.state = BlockState::Waiting;

                // Aparicion: la capa marca el momento base y un pequeno desfase
                // pseudoaleatorio evita que todos los bloques caigan a la vez.
                float jitter = hashToUnitFloat(hashUint(h)) * (cfg.buildSeconds / layers);
                b.revealAt = layerFraction * cfg.buildSeconds + jitter;

                // Desvanecimiento: se recorre en sentido inverso, de arriba
                // hacia abajo, tal como indica el pipeline.
                float vanishJitter = hashToUnitFloat(hashUint(h ^ 0xABCDu)) *
                                     (cfg.dissolveSeconds / layers);
                b.vanishAt = dissolveStart +
                             (1.0f - layerFraction) * cfg.dissolveSeconds + vanishJitter;

                b.posY  = static_cast<float>(y) + kDropHeight;
                b.velY  = 0.0f;
                b.scale = 0.0f;

                world.blocks.push_back(b);
            }
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
    world.biomeIndex = (cfg.biomeIndex >= 0)
                     ? cfg.biomeIndex
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
//  Nota para la version paralela: dos bloques distintos nunca escriben la misma
//  celda de occupancy, por lo que el bucle se puede repartir entre hilos, pero
//  hace falta una barrera antes de leer occupancy en buildInstanceBuffer.
// ----------------------------------------------------------------------------
static void updateWorld(World& world, float elapsed, float dt, size_t& liveBlocks) {
    size_t alive = 0;

    for (Block& b : world.blocks) {
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
//  Nota para la version paralela: escribir en "instances" es una compactacion
//  (cada hilo produce una cantidad distinta de elementos), asi que requerira
//  un conteo por hilo y una suma de prefijos, o bien un contador atomico.
// ----------------------------------------------------------------------------
static size_t buildInstanceBuffer(const World& world, const uint32_t* faceTex,
                                  InstanceData* instances) {
    // Desplazamiento que centra el mundo en el origen para que la camara orbite
    // alrededor de su centro geometrico.
    const float offset = world.side * 0.5f - 0.5f;
    size_t count = 0;

    for (const Block& b : world.blocks) {
        if (b.state == BlockState::Gone || b.scale <= 0.0f) continue;

        // Descarte de bloques totalmente rodeados: solo se aplica a los que ya
        // estan asentados, porque los que aun caen se ven desde cualquier lado.
        if (b.state == BlockState::Placed) {
            if (world.occupiedAt(b.gx + 1, b.gy, b.gz) &&
                world.occupiedAt(b.gx - 1, b.gy, b.gz) &&
                world.occupiedAt(b.gx, b.gy + 1, b.gz) &&
                world.occupiedAt(b.gx, b.gy - 1, b.gz) &&
                world.occupiedAt(b.gx, b.gy, b.gz + 1) &&
                world.occupiedAt(b.gx, b.gy, b.gz - 1)) {
                continue;
            }
        }

        InstanceData& inst = instances[count++];
        inst.x       = static_cast<float>(b.gx) - offset;
        inst.y       = b.posY;
        inst.z       = static_cast<float>(b.gz) - offset;
        inst.faceTex = faceTex[static_cast<int>(b.id)] | (static_cast<uint32_t>(b.shade) << 24);
        inst.scale   = b.scale;
    }

    return count;
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
    if (cfg.threads != 0) {
        std::fprintf(stderr,
                     "Aviso: --threads no tiene efecto en la version secuencial.\n");
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
                                          "Minecraft Screen Saver - Secuencial",
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

    std::printf("Texturas   : %s (%d capas, %.1f KB, %.1f ms)\n",
                assetsDir.c_str(), atlas.layerCount(),
                atlas.pixelBytes() / 1024.0, atlasMs);

    // --- 5. Generacion del primer mundo --------------------------------------
    // Semilla: la indicada por el usuario, o una sorteada por el sistema.
    std::random_device randomDevice;
    uint32_t seed = (cfg.seed != 0) ? cfg.seed : randomDevice();

    World world;
    double genMs = generateWorld(cfg, seed, world);

    std::vector<InstanceData> instances(world.blocks.size());

    std::printf("Mundo      : semilla %u | bioma %s | reticula %dx%dx%d\n"
                "             bloques %zu (N pedido %ld) | generado en %.1f ms\n",
                world.seed, world.biome->name, world.side, world.height, world.side,
                world.blocks.size(), cfg.targetBlocks, genMs);
    std::printf("Controles  : ESC para salir, ESPACIO para generar otro mundo.\n");
    std::fflush(stdout);

    // --- 6. Bucle principal ---------------------------------------------------
    Phase  phase        = Phase::Building;
    double cycleStart   = glfwGetTime();
    double lastTime     = cycleStart;
    double fpsTimer     = cycleStart;
    int    frameCount   = 0;
    int    displayedFps = 0;
    double phaseEndTime = 0.0;   // instante en que termina la fase actual
    size_t liveBlocks   = world.blocks.size();
    bool   spaceWasDown = false;  // estado previo de ESPACIO (deteccion de flanco)
    size_t drawnBlocks  = 0;
    double cpuMsAccum   = 0.0;   // tiempo de CPU acumulado del fotograma

    while (!glfwWindowShouldClose(window)) {
        double now = glfwGetTime();
        float  dt  = static_cast<float>(now - lastTime);
        lastTime = now;

        // Un fotograma muy largo (por ejemplo al arrastrar la ventana) haria
        // que los bloques atravesaran el suelo: se acota el paso de tiempo.
        if (dt > 0.05f) dt = 0.05f;

        glfwPollEvents();
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        // --- Maquina de estados del ciclo del protector de pantalla ----------
        float elapsed = static_cast<float>(now - cycleStart);

        // Solo el instante en que se presiona ESPACIO cuenta como peticion, no
        // cada fotograma en que la tecla siga hundida.
        bool spaceIsDown = (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS);
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
            liveBlocks = world.blocks.size();
            phase      = Phase::Building;
            cycleStart = glfwGetTime();
            lastTime   = cycleStart;

            std::printf("Mundo nuevo: semilla %u | bioma %s | reticula %dx%dx%d | "
                        "bloques %zu | %.1f ms\n",
                        world.seed, world.biome->name, world.side, world.height,
                        world.side, world.blocks.size(), genMs);
            std::fflush(stdout);
            continue;
        }

        // --- Trabajo de CPU proporcional a N ---------------------------------
        auto cpuStart = std::chrono::steady_clock::now();

        updateWorld(world, elapsed, dt, liveBlocks);
        drawnBlocks = buildInstanceBuffer(world, faceTexTables[world.biomeIndex].data(),
                                          instances.data());

        cpuMsAccum += std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - cpuStart).count();

        // --- Camara: orbita circular alrededor del centro del mundo ----------
        // La posicion se obtiene con funciones trigonometricas sobre el angulo
        // que avanza con el tiempo.
        int fbWidth = 0, fbHeight = 0;
        glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
        if (fbHeight == 0) fbHeight = 1;
        glViewport(0, 0, fbWidth, fbHeight);

        float orbitRadius = world.side * 1.05f + 14.0f;
        float orbitHeight = world.height * 0.85f + world.side * 0.30f;
        float angle       = static_cast<float>(now) * 0.15f;

        glm::vec3 eye(std::cos(angle) * orbitRadius,
                      orbitHeight,
                      std::sin(angle) * orbitRadius);
        glm::vec3 center(0.0f, world.height * 0.30f, 0.0f);

        glm::mat4 projection = glm::perspective(
            glm::radians(55.0f),
            static_cast<float>(fbWidth) / static_cast<float>(fbHeight),
            0.5f, orbitRadius * 4.0f + 200.0f);
        glm::mat4 view     = glm::lookAt(eye, center, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 viewProj = projection * view;

        // --- Dibujo -----------------------------------------------------------
        uint32_t sky = world.biome->skyColor;
        glClearColor(((sky >> 16) & 0xFF) / 255.0f,
                     ((sky >> 8)  & 0xFF) / 255.0f,
                     ( sky        & 0xFF) / 255.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        renderer.draw(instances.data(), drawnBlocks, viewProj, atlas.textureId());
        glfwSwapBuffers(window);

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
                          "Minecraft Screen Saver - Secuencial | FPS %d | %s | "
                          "bloques %zu / dibujados %zu | CPU %.2f ms | %s",
                          displayedFps, world.biome->name, world.blocks.size(),
                          drawnBlocks, cpuMsAccum / frameCount, phaseName);
            glfwSetWindowTitle(window, title);

            frameCount = 0;
            cpuMsAccum = 0.0;
            fpsTimer   = now;
        }
    }

    // --- 7. Liberacion ordenada de recursos ----------------------------------
    renderer.destroy();
    atlas.destroy();
    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}
