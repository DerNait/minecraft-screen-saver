// ============================================================================
//  app_config.cpp - Implementacion del analisis de argumentos
// ============================================================================
#include "app_config.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace mcss {
namespace {

// ----------------------------------------------------------------------------
//  parseLong / parseFloat
//  Convierten una cadena a numero verificando que TODO el texto sea consumido.
//  Rechazan entradas como "12abc", "" o " " que strtol aceptaria parcialmente.
// ----------------------------------------------------------------------------
bool parseLong(const char* text, long& out) {
    if (text == nullptr || *text == '\0') return false;
    errno = 0;
    char* end = nullptr;
    long value = std::strtol(text, &end, 10);
    if (errno == ERANGE || end == text || *end != '\0') return false;
    out = value;
    return true;
}

bool parseFloat(const char* text, float& out) {
    if (text == nullptr || *text == '\0') return false;
    errno = 0;
    char* end = nullptr;
    double value = std::strtod(text, &end);
    if (errno == ERANGE || end == text || *end != '\0') return false;
    out = static_cast<float>(value);
    return true;
}

// Construye un mensaje de error uniforme para un valor fuera de rango.
std::string outOfRange(const char* option, const char* received, const char* expected) {
    return std::string("valor invalido para ") + option + ": '" + received +
           "'. Se esperaba " + expected + ".";
}

} // namespace

void printUsage(const char* programName) {
    std::printf(
        "Minecraft Screen Saver - Computacion Paralela y Distribuida (UVG)\n"
        "\n"
        "Uso:\n"
        "  %s <N> [opciones]\n"
        "\n"
        "Argumento obligatorio:\n"
        "  N                     Cantidad objetivo de bloques a renderizar.\n"
        "                        Entero entre 64 y 20000000.\n"
        "\n"
        "Opciones:\n"
        "  --width  <px>         Ancho de la ventana.        (min 640,  def. 1280)\n"
        "  --height <px>         Alto de la ventana.         (min 480,  def. 720)\n"
        "  --seed <n>            Semilla del mundo.          (0 = aleatoria, def. 0)\n"
        "  --relief <f>          Escala del relieve.         (0.2 - 4.0, def. 1.0)\n"
        "  --biome <n>           Fuerza un bioma por indice. (-1 = por semilla)\n"
        "  --build <s>           Duracion de la construccion.(0.5 - 120, def. 6.0)\n"
        "  --hold <s>            Tiempo con el mundo armado. (0.0 - 120, def. 4.0)\n"
        "  --dissolve <s>        Duracion del desarmado.     (0.5 - 120, def. 3.0)\n"
        "  --pause <s>           Pausa entre mundos.         (0.0 - 60,  def. 0.8)\n"
        "  --assets <ruta>       Carpeta con las texturas .png.\n"
        "  --no-vsync            Desactiva la sincronizacion vertical (para medir).\n"
        "  --threads <n>         Hilos de OpenMP.            (1 - 256, solo paralelo)\n"
        "  --camera <modo>       orbit | auto.               (def. orbit)\n"
        "  --camera-change <s>   Segundos entre encuadres.   (2 - 120, def. 10)\n"
        "  --mobs <n>            Cantidad de animales.      (0 - 5000, def. 0)\n"
        "  --lighting <modo>     classic | cycle.            (def. classic)\n"
        "  --day-cycle <s>       Ciclo total: 70%% dia, 30%% noche. (10 - 600, def. 60)\n"
        "  --shadows <modo>      off | near.                 (def. off, requiere cycle)\n"
        "  --shadow-distance <n> Radio local de sombras.     (16 - 128, def. 48)\n"
        "  --help                Muestra esta ayuda.\n"
        "\n"
        "Modo de medicion (para el calculo de speedup y eficiencia):\n"
        "  --benchmark <f>       Mide f fotogramas y termina.  (30 - 200000)\n"
        "  --bench-warmup <f>    Fotogramas descartados antes. (0-100000, def. 120)\n"
        "  --bench-gen <n>       Generaciones cronometradas.   (1 - 100, def. 5)\n"
        "  --bench-csv <ruta>    Detalle por fotograma en un CSV.\n"
        "  --bench-tag <texto>   Etiqueta libre copiada al informe.\n"
        "\n"
        "Ejemplos:\n"
        "  %s 50000\n"
        "  %s 250000 --width 1600 --height 900 --seed 1234\n"
        "  %s 500000 --no-vsync --relief 1.5\n"
        "  %s 500000 --benchmark 300 --build 1 --hold 60 --seed 20260901\n",
        programName, programName, programName, programName, programName);
}

bool parseArguments(int argc, char** argv, AppConfig& config, std::string& error) {
    error.clear();

    // Primera pasada: detectar --help en cualquier posicion para poder mostrar
    // la ayuda aunque el resto de los argumentos este mal escrito.
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            config.showHelp = true;
            return true;
        }
    }

    if (argc < 2) {
        error = "falta el argumento obligatorio N (cantidad de bloques a renderizar).";
        return false;
    }

    // --- Argumento posicional N ---------------------------------------------
    long n = 0;
    if (!parseLong(argv[1], n)) {
        error = outOfRange("N", argv[1], "un numero entero");
        return false;
    }
    if (n < 64 || n > 20000000L) {
        error = outOfRange("N", argv[1], "un entero entre 64 y 20000000");
        return false;
    }
    config.targetBlocks = n;

    // --- Opciones con nombre --------------------------------------------------
    for (int i = 2; i < argc; ++i) {
        const char* opt = argv[i];

        // Opcion sin valor asociado.
        if (std::strcmp(opt, "--no-vsync") == 0) {
            config.vsync = false;
            continue;
        }

        if (opt[0] != '-') {
            error = std::string("argumento inesperado: '") + opt +
                    "'. Solo se admite un valor posicional (N).";
            return false;
        }

        // El resto de las opciones requieren un valor a continuacion.
        if (i + 1 >= argc) {
            error = std::string("la opcion '") + opt + "' requiere un valor.";
            return false;
        }
        const char* value = argv[++i];

        if (std::strcmp(opt, "--width") == 0) {
            long v = 0;
            if (!parseLong(value, v) || v < 640 || v > 16384) {
                error = outOfRange("--width", value, "un entero entre 640 y 16384");
                return false;
            }
            config.windowWidth = static_cast<int>(v);

        } else if (std::strcmp(opt, "--height") == 0) {
            long v = 0;
            if (!parseLong(value, v) || v < 480 || v > 16384) {
                error = outOfRange("--height", value, "un entero entre 480 y 16384");
                return false;
            }
            config.windowHeight = static_cast<int>(v);

        } else if (std::strcmp(opt, "--seed") == 0) {
            long v = 0;
            if (!parseLong(value, v) || v < 0) {
                error = outOfRange("--seed", value, "un entero no negativo");
                return false;
            }
            config.seed = static_cast<uint32_t>(v);

        } else if (std::strcmp(opt, "--relief") == 0) {
            float v = 0.0f;
            if (!parseFloat(value, v) || v < 0.2f || v > 4.0f) {
                error = outOfRange("--relief", value, "un numero entre 0.2 y 4.0");
                return false;
            }
            config.reliefScale = v;

        } else if (std::strcmp(opt, "--biome") == 0) {
            long v = 0;
            if (!parseLong(value, v) || v < -1 || v > 64) {
                error = outOfRange("--biome", value, "un entero entre -1 y 64");
                return false;
            }
            config.biomeIndex = static_cast<int>(v);

        } else if (std::strcmp(opt, "--build") == 0) {
            float v = 0.0f;
            if (!parseFloat(value, v) || v < 0.5f || v > 120.0f) {
                error = outOfRange("--build", value, "un numero entre 0.5 y 120");
                return false;
            }
            config.buildSeconds = v;

        } else if (std::strcmp(opt, "--hold") == 0) {
            float v = 0.0f;
            if (!parseFloat(value, v) || v < 0.0f || v > 120.0f) {
                error = outOfRange("--hold", value, "un numero entre 0 y 120");
                return false;
            }
            config.holdSeconds = v;

        } else if (std::strcmp(opt, "--dissolve") == 0) {
            float v = 0.0f;
            if (!parseFloat(value, v) || v < 0.5f || v > 120.0f) {
                error = outOfRange("--dissolve", value, "un numero entre 0.5 y 120");
                return false;
            }
            config.dissolveSeconds = v;

        } else if (std::strcmp(opt, "--pause") == 0) {
            float v = 0.0f;
            if (!parseFloat(value, v) || v < 0.0f || v > 60.0f) {
                error = outOfRange("--pause", value, "un numero entre 0 y 60");
                return false;
            }
            config.pauseSeconds = v;

        } else if (std::strcmp(opt, "--threads") == 0) {
            long v = 0;
            if (!parseLong(value, v) || v < 1 || v > 256) {
                error = outOfRange("--threads", value, "un entero entre 1 y 256");
                return false;
            }
            config.threads = static_cast<int>(v);

        } else if (std::strcmp(opt, "--camera") == 0) {
            if (std::strcmp(value, "orbit") != 0 && std::strcmp(value, "auto") != 0) {
                error = outOfRange("--camera", value, "'orbit' o 'auto'");
                return false;
            }
            config.cameraMode = value;

        } else if (std::strcmp(opt, "--camera-change") == 0) {
            float v = 0.0f;
            if (!parseFloat(value, v) || v < 2.0f || v > 120.0f) {
                error = outOfRange("--camera-change", value, "un numero entre 2 y 120");
                return false;
            }
            config.cameraChangeSeconds = v;

        } else if (std::strcmp(opt, "--mobs") == 0) {
            long v = 0;
            if (!parseLong(value, v) || v < 0 || v > 5000) {
                error = outOfRange("--mobs", value, "un entero entre 0 y 5000");
                return false;
            }
            config.mobCount = static_cast<int>(v);

        } else if (std::strcmp(opt, "--lighting") == 0) {
            if (std::strcmp(value, "classic") != 0 &&
                std::strcmp(value, "cycle") != 0) {
                error = outOfRange("--lighting", value, "'classic' o 'cycle'");
                return false;
            }
            config.lightingMode = value;

        } else if (std::strcmp(opt, "--day-cycle") == 0) {
            float v = 0.0f;
            if (!parseFloat(value, v) || v < 10.0f || v > 600.0f) {
                error = outOfRange("--day-cycle", value,
                                   "un numero entre 10 y 600");
                return false;
            }
            config.dayCycleSeconds = v;

        } else if (std::strcmp(opt, "--shadows") == 0) {
            if (std::strcmp(value, "off") != 0 &&
                std::strcmp(value, "near") != 0) {
                error = outOfRange("--shadows", value, "'off' o 'near'");
                return false;
            }
            config.shadowMode = value;

        } else if (std::strcmp(opt, "--shadow-distance") == 0) {
            float v = 0.0f;
            if (!parseFloat(value, v) || v < 16.0f || v > 128.0f) {
                error = outOfRange("--shadow-distance", value,
                                   "un numero entre 16 y 128");
                return false;
            }
            config.shadowDistance = v;

        } else if (std::strcmp(opt, "--benchmark") == 0) {
            long v = 0;
            if (!parseLong(value, v) || v < 30 || v > 200000) {
                error = outOfRange("--benchmark", value,
                                   "un entero entre 30 y 200000");
                return false;
            }
            config.benchFrames = static_cast<int>(v);

        } else if (std::strcmp(opt, "--bench-warmup") == 0) {
            long v = 0;
            if (!parseLong(value, v) || v < 0 || v > 100000) {
                error = outOfRange("--bench-warmup", value,
                                   "un entero entre 0 y 100000");
                return false;
            }
            config.benchWarmup = static_cast<int>(v);

        } else if (std::strcmp(opt, "--bench-gen") == 0) {
            long v = 0;
            if (!parseLong(value, v) || v < 1 || v > 100) {
                error = outOfRange("--bench-gen", value,
                                   "un entero entre 1 y 100");
                return false;
            }
            config.benchGenReps = static_cast<int>(v);

        } else if (std::strcmp(opt, "--bench-csv") == 0) {
            if (value[0] == '\0') {
                error = "la opcion '--bench-csv' requiere una ruta no vacia.";
                return false;
            }
            config.benchCsv = value;

        } else if (std::strcmp(opt, "--bench-tag") == 0) {
            config.benchTag = value;

        } else if (std::strcmp(opt, "--assets") == 0) {
            config.assetsDir = value;

        } else {
            error = std::string("opcion desconocida: '") + opt +
                    "'. Use --help para ver las opciones disponibles.";
            return false;
        }
    }

    return true;
}

} // namespace mcss
