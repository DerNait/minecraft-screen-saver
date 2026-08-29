// ============================================================================
//  block_catalog.cpp - Tablas de bloques y biomas
// ============================================================================
#include "block_catalog.hpp"

namespace mcss {
namespace {

// Tintes tomados de la paleta clasica de Minecraft para cada tipo de vegetacion.
constexpr uint32_t TINT_NONE = 0xFFFFFF;

// Tabla de biomas. El orden define el indice que la semilla selecciona.
const Biome kBiomes[kBiomeCount] = {
    // nombre        pasto     hojas     cielo     superficie          subsuelo             tronco               hojas                arboles relieve
    {  "Llanura",    0x91BD59, 0x77AB2F, 0x87CEEB, BlockId::GrassBlock, BlockId::Dirt,      BlockId::OakLog,     BlockId::OakLeaves,    0.010f, 0.70f },
    {  "Bosque",     0x79C05A, 0x59AE30, 0x88CCEE, BlockId::GrassBlock, BlockId::Dirt,      BlockId::OakLog,     BlockId::OakLeaves,    0.055f, 1.00f },
    {  "Abedules",   0x88BB67, 0x6FAD34, 0x8FD0F0, BlockId::GrassBlock, BlockId::Dirt,      BlockId::BirchLog,   BlockId::BirchLeaves,  0.045f, 0.85f },
    {  "Desierto",   0xBFB755, 0xAEA42A, 0x9FD4F5, BlockId::Sand,       BlockId::Sandstone, BlockId::Cactus,     BlockId::Cactus,       0.012f, 0.55f },
    {  "Taiga",      0x80B497, 0x60A17B, 0xA8CBE0, BlockId::Snow,       BlockId::Dirt,      BlockId::SpruceLog,  BlockId::SpruceLeaves, 0.050f, 1.25f },
};

// Construye una TextureSpec de una sola textura sin overlay ni tinte.
constexpr TextureSpec plain(const char* name) {
    return TextureSpec{name, nullptr, TINT_NONE};
}

// Construye una TextureSpec tenida (la textura base se multiplica por el tinte).
constexpr TextureSpec tinted(const char* name, uint32_t tint) {
    return TextureSpec{name, nullptr, tint};
}

// Devuelve las texturas de un bloque cuyas seis caras son iguales.
BlockTextures uniform(const char* name) {
    TextureSpec s = plain(name);
    return BlockTextures{s, s, s};
}

// Devuelve las texturas de un bloque tipo "columna" (tapa/base iguales, lados
// distintos), como los troncos de arbol.
BlockTextures pillar(const char* capName, const char* sideName) {
    return BlockTextures{plain(capName), plain(sideName), plain(capName)};
}

// Devuelve las texturas de un bloque de seis caras iguales, todas tenidas con
// el mismo color (caso de las hojas, cuya textura viene en escala de grises).
BlockTextures uniform_tinted(const char* name, uint32_t tint) {
    TextureSpec s = tinted(name, tint);
    return BlockTextures{s, s, s};
}

} // namespace

const Biome& biomeAt(int index) {
    // Normalizacion defensiva: cualquier indice queda dentro del rango valido.
    int safe = index % kBiomeCount;
    if (safe < 0) safe += kBiomeCount;
    return kBiomes[safe];
}

BlockTextures blockTextures(BlockId id, const Biome& biome) {
    switch (id) {
        // --- Estratos profundos ---------------------------------------------
        case BlockId::Bedrock:      return uniform("bedrock");
        case BlockId::Deepslate:    return pillar("deepslate_top", "deepslate");
        case BlockId::Stone:        return uniform("stone");
        case BlockId::Andesite:     return uniform("andesite");
        case BlockId::Granite:      return uniform("granite");
        case BlockId::Diorite:      return uniform("diorite");
        case BlockId::Gravel:       return uniform("gravel");
        case BlockId::CoalOre:      return uniform("coal_ore");
        case BlockId::IronOre:      return uniform("iron_ore");

        // --- Subsuelo y superficie ------------------------------------------
        case BlockId::Dirt:         return uniform("dirt");
        case BlockId::CoarseDirt:   return uniform("coarse_dirt");

        case BlockId::GrassBlock:
            // La cara superior es una textura en escala de grises que se tine con
            // el color del bioma. La cara lateral es tierra con un overlay de
            // pasto tenido, igual que en el juego original.
            return BlockTextures{
                tinted("grass_block_top", biome.grassTint),
                TextureSpec{"grass_block_side", "grass_block_side_overlay", biome.grassTint},
                plain("dirt")
            };

        case BlockId::Podzol:       return BlockTextures{plain("podzol_top"), plain("podzol_side"), plain("dirt")};
        case BlockId::Snow:         return uniform("snow");

        case BlockId::Sand:         return uniform("sand");
        case BlockId::Sandstone:    return BlockTextures{plain("sandstone_top"), plain("sandstone"), plain("sandstone_bottom")};
        case BlockId::RedSand:      return uniform("red_sand");

        // --- Vegetacion -------------------------------------------------------
        case BlockId::OakLog:       return pillar("oak_log_top", "oak_log");
        case BlockId::BirchLog:     return pillar("birch_log_top", "birch_log");
        case BlockId::SpruceLog:    return pillar("spruce_log_top", "spruce_log");

        // Las hojas tambien son texturas grises que reciben el tinte del bioma.
        case BlockId::OakLeaves:    return uniform_tinted("oak_leaves", biome.foliageTint);
        case BlockId::BirchLeaves:  return uniform_tinted("birch_leaves", biome.foliageTint);
        case BlockId::SpruceLeaves: return uniform_tinted("spruce_leaves", biome.foliageTint);

        case BlockId::Cactus:       return BlockTextures{plain("cactus_top"), plain("cactus_side"), plain("cactus_bottom")};

        // Air y cualquier valor inesperado caen aqui: se devuelve piedra para
        // que un error de logica sea visible en pantalla en lugar de provocar
        // un acceso invalido.
        case BlockId::Air:
        default:                    return uniform("stone");
    }
}

} // namespace mcss
