// ============================================================================
//  app_config.hpp - Captura y validacion de los argumentos de linea de comandos
// ----------------------------------------------------------------------------
//  Concentra toda la programacion defensiva de la entrada del programa: ningun
//  valor de configuracion esta escrito de forma fija en el resto del codigo,
//  todos provienen de aqui con un valor por omision documentado y un rango
//  valido verificado antes de arrancar.
// ============================================================================
#pragma once

#include <cstdint>
#include <string>

namespace mcss {

struct AppConfig {
    // --- Parametro obligatorio ---------------------------------------------
    long targetBlocks = 0;        // N: cantidad objetivo de bloques a renderizar

    // --- Ventana -------------------------------------------------------------
    int windowWidth  = 1280;      // ancho del lienzo en pixeles (minimo 640)
    int windowHeight = 720;       // alto del lienzo en pixeles (minimo 480)

    // --- Mundo ---------------------------------------------------------------
    uint32_t seed       = 0;      // semilla del mundo; 0 = se sortea al arrancar
    float    reliefScale = 1.0f;  // multiplicador global del relieve del terreno
    int      biomeIndex = -1;     // bioma forzado; -1 = lo elige la semilla

    // --- Tiempos del ciclo del protector de pantalla (segundos) --------------
    float buildSeconds    = 6.0f; // duracion de la fase de construccion
    float holdSeconds     = 4.0f; // tiempo que el mundo permanece armado
    float dissolveSeconds = 3.0f; // duracion del desarmado por capas
    float pauseSeconds    = 0.8f; // pausa en negro antes del siguiente mundo

    // --- Otros ---------------------------------------------------------------
    std::string assetsDir;        // carpeta de texturas; vacia = busqueda automatica
    bool        vsync    = true;  // sincronizacion vertical (desactivar para medir)
    int         threads  = 0;     // hilos de OpenMP; 0 = los que decida el sistema
    std::string cameraMode = "orbit"; // orbit = camara original, auto = encuadres automaticos
    float       cameraChangeSeconds = 10.0f; // intervalo entre encuadres automaticos
    bool        showHelp = false; // el usuario pidio la ayuda
};

// ----------------------------------------------------------------------------
//  parseArguments
//  Entradas: argc, argv - argumentos tal como los recibe main
//            programName - nombre a mostrar en los mensajes de uso
//  Salidas : config - configuracion resultante (solo valida si retorna true)
//            error  - descripcion del problema encontrado si retorna false
//  Retorno : true si los argumentos son validos o si se pidio la ayuda
//  Descripcion: reconoce N como primer argumento posicional y el resto como
//  opciones "--nombre valor". Rechaza valores no numericos, fuera de rango,
//  opciones desconocidas y opciones sin su valor correspondiente.
// ----------------------------------------------------------------------------
bool parseArguments(int argc, char** argv, AppConfig& config, std::string& error);

// ----------------------------------------------------------------------------
//  printUsage
//  Entradas: programName - nombre del ejecutable
//  Descripcion: imprime en la salida estandar la lista completa de argumentos,
//  sus rangos validos y sus valores por omision.
// ----------------------------------------------------------------------------
void printUsage(const char* programName);

} // namespace mcss
