// ============================================================================
//  block_catalog.hpp - Catalogo de tipos de bloque y de biomas
// ----------------------------------------------------------------------------
//  Define que textura le corresponde a cada cara de cada tipo de bloque, y como
//  los biomas cambian esas texturas y sus tintes. Es una tabla de datos pura:
//  no toca OpenGL ni el estado del mundo, de modo que puede consultarse desde
//  cualquier hilo sin sincronizacion.
// ============================================================================
#pragma once

#include <cstdint>

namespace mcss {

// ----------------------------------------------------------------------------
//  BlockId - identificador compacto del tipo de bloque.
//  Se guarda como uint8_t porque el mundo puede tener millones de bloques y
//  cada byte ahorrado se multiplica por N.
// ----------------------------------------------------------------------------
enum class BlockId : uint8_t {
    Air = 0,       // ausencia de bloque (no se renderiza)
    Bedrock,       // roca madre: piso indestructible del mundo
    Deepslate,     // estrato profundo
    Stone,         // estrato principal de piedra
    Andesite,      // vetas de variantes de piedra
    Granite,
    Diorite,
    Gravel,
    CoalOre,       // vetas de mineral, dispersas por ruido 3D
    IronOre,
    Dirt,          // estrato de tierra bajo la superficie
    CoarseDirt,
    GrassBlock,    // superficie con pasto (cara superior tenida por bioma)
    Podzol,
    Snow,          // superficie nevada
    Sand,          // superficie de desierto
    Sandstone,     // subsuelo de desierto
    RedSand,
    OakLog,        // troncos y hojas de los arboles
    OakLeaves,
    BirchLog,
    BirchLeaves,
    SpruceLog,
    SpruceLeaves,
    Cactus,
    Count          // centinela: cantidad total de tipos definidos
};

// Constante de conveniencia con la cantidad de tipos de bloque.
constexpr int kBlockTypeCount = static_cast<int>(BlockId::Count);

// ----------------------------------------------------------------------------
//  TextureSpec - describe como se construye la textura de UNA cara.
//  base    : nombre del archivo dentro de assets/textures (sin la extension)
//  overlay : textura opcional que se compone encima de la base; si es nullptr
//            no se aplica ninguna
//  tint    : color 0xRRGGBB que multiplica al overlay cuando existe, o a la
//            base cuando no hay overlay. 0xFFFFFF significa "sin tinte".
//  El tinte se aplica en CPU al construir el atlas, no en el shader: asi el
//  fragment shader se mantiene trivial y no hay que pasar color por instancia.
// ----------------------------------------------------------------------------
struct TextureSpec {
    const char* base    = nullptr;
    const char* overlay = nullptr;
    uint32_t    tint    = 0xFFFFFF;
};

// Texturas de las tres caras distinguibles de un cubo.
// Las cuatro caras laterales comparten la misma textura.
struct BlockTextures {
    TextureSpec top;
    TextureSpec side;
    TextureSpec bottom;
};

// ----------------------------------------------------------------------------
//  Biome - conjunto de reglas que le da personalidad visual a un mundo.
//  Cada mundo nuevo elige un bioma pseudoaleatoriamente a partir de su semilla,
//  lo que cumple el requisito de desplegar varios colores generados al azar.
// ----------------------------------------------------------------------------
struct Biome {
    const char* name;          // nombre legible, se muestra en el titulo
    uint32_t    grassTint;     // tinte del pasto (cara superior y overlay lateral)
    uint32_t    foliageTint;   // tinte de las hojas de los arboles
    uint32_t    skyColor;      // color de fondo del cielo, 0xRRGGBB
    BlockId     surface;       // bloque de la capa mas alta de cada columna
    BlockId     subsurface;    // bloque de las capas inmediatamente inferiores
    BlockId     logType;       // tronco usado por la vegetacion
    BlockId     leafType;      // hojas usadas por la vegetacion
    float       treeChance;    // probabilidad de vegetacion por columna [0,1]
    float       reliefScale;   // multiplicador del relieve del terreno
};

// Cantidad de biomas definidos en el catalogo.
constexpr int kBiomeCount = 5;

// ----------------------------------------------------------------------------
//  biomeAt
//  Entradas: index - indice del bioma (se normaliza con modulo, nunca falla)
//  Salida  : referencia constante al bioma correspondiente
// ----------------------------------------------------------------------------
const Biome& biomeAt(int index);

// ----------------------------------------------------------------------------
//  blockTextures
//  Entradas: id    - tipo de bloque a consultar
//            biome - bioma activo, del que se toman los tintes
//  Salida  : las tres TextureSpec (superior, lateral, inferior) del bloque
//  Descripcion: resuelve la tabla de texturas aplicando los tintes del bioma a
//  los bloques que lo requieren (pasto y hojas usan texturas en escala de
//  grises que solo cobran color al ser tenidas).
// ----------------------------------------------------------------------------
BlockTextures blockTextures(BlockId id, const Biome& biome);

// ----------------------------------------------------------------------------
//  isSolid
//  Entradas: id - tipo de bloque
//  Salida  : true si el bloque ocupa volumen (todo menos Air)
// ----------------------------------------------------------------------------
inline bool isSolid(BlockId id) { return id != BlockId::Air; }

} // namespace mcss
