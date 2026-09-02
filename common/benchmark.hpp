// ============================================================================
//  benchmark.hpp - Modo de medicion compartido por ambas versiones
// ----------------------------------------------------------------------------
//  El enunciado pide calcular el tiempo que le toma al programa sostener N
//  elementos sin perder fotogramas, y a partir de ahi el speedup y la
//  eficiencia. Para que la comparacion entre la version secuencial y la
//  paralela sea justa, ambas comparten EXACTAMENTE este mismo cronometro:
//
//    * Reloj virtual de paso fijo (1/60 s). El avance de la simulacion deja de
//      depender de que tan rapido corra la maquina, asi que las dos versiones
//      recorren la misma secuencia de estados y hacen el mismo trabajo.
//    * Semilla fija. El mundo generado es identico en cada repeticion.
//    * Sin sincronizacion vertical, para que el monitor no imponga un techo
//      artificial de 60 FPS.
//    * Fotogramas de calentamiento descartados, de modo que la medicion no
//      incluya la subida de texturas ni los primeros fotogramas del driver.
//
//  Al terminar imprime un informe en formato "clave=valor" que el script
//  metricas/medir_y_graficar.py lee para armar los CSV y las graficas.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "app_config.hpp"

namespace mcss {

// ----------------------------------------------------------------------------
//  FrameTiming - desglose en milisegundos del costo de UN fotograma.
//  updateMs .. shadowMs son las etapas que la version paralela reparte con
//  OpenMP; cpuMs es todo el trabajo de CPU del fotograma y frameMs es la
//  iteracion completa incluyendo el dibujo y el intercambio de buffers.
// ----------------------------------------------------------------------------
struct FrameTiming {
    double updateMs = 0.0;  // fisica y maquina de estados de los N bloques
    double mobsMs   = 0.0;  // IA de los animales y armado de sus instancias
    double buildMs  = 0.0;  // descarte de invisibles y empaquetado de instancias
    double shadowMs = 0.0;  // compactacion de los bloques que proyectan sombra
    double cpuMs    = 0.0;  // total de CPU del fotograma (incluye lo anterior)
    double frameMs  = 0.0;  // iteracion completa: CPU + envio a la GPU
    char   phase    = 'H';  // B construyendo, H sostenido, D desarmando, P pausa
};

// ----------------------------------------------------------------------------
//  BenchmarkInfo - contexto del mundo medido que se copia tal cual al informe.
// ----------------------------------------------------------------------------
struct BenchmarkInfo {
    const char* version      = "";   // "secuencial" o "paralelo"
    int         threads      = 1;    // hilos de OpenMP realmente usados
    long        targetBlocks = 0;    // N pedido por el usuario
    size_t      worldBlocks  = 0;    // bloques que realmente tiene el mundo
    uint32_t    seed         = 0;    // semilla efectiva
    const char* biome        = "";   // nombre del bioma generado
};

// ----------------------------------------------------------------------------
//  Benchmark - acumulador de muestras y generador del informe.
// ----------------------------------------------------------------------------
class Benchmark {
public:
    // Paso de tiempo virtual: 60 fotogramas por segundo de simulacion.
    static constexpr double kFixedDeltaSeconds = 1.0 / 60.0;

    // ------------------------------------------------------------------------
    //  start
    //  Entradas: cfg - configuracion ya validada del programa
    //  Descripcion: copia los parametros de medicion. Si cfg.benchFrames es 0
    //  el objeto queda inactivo y el programa se comporta como siempre.
    // ------------------------------------------------------------------------
    void start(const AppConfig& cfg);

    bool   active()      const { return frames_ > 0; }
    int    generations() const { return genReps_; }
    // ¿ya se recolectaron todas las muestras pedidas?
    bool   complete()    const { return frames_ > 0 && samples_.size() >= static_cast<size_t>(frames_); }
    size_t measured()    const { return samples_.size(); }

    // Cronometraje de una generacion completa del mundo (milisegundos).
    void addGeneration(double milliseconds);

    // ------------------------------------------------------------------------
    //  addFrame
    //  Entradas: timing - desglose del fotograma
    //            drawnBlocks - bloques que sobrevivieron al descarte
    //  Descripcion: descarta en silencio los fotogramas de calentamiento y
    //  guarda el resto hasta completar la cantidad pedida.
    // ------------------------------------------------------------------------
    void addFrame(const FrameTiming& timing, size_t drawnBlocks);

    // ------------------------------------------------------------------------
    //  report
    //  Entradas: info - contexto del mundo medido
    //  Salidas : error - motivo si el CSV por fotograma no se pudo escribir
    //  Retorno : false solo si fallo la escritura del CSV opcional
    //  Descripcion: imprime el informe "clave=valor" en la salida estandar y,
    //  si se pidio --bench-csv, guarda ademas el detalle fotograma a fotograma.
    // ------------------------------------------------------------------------
    bool report(const BenchmarkInfo& info, std::string& error) const;

private:
    int                      frames_   = 0;   // fotogramas a medir
    int                      warmup_   = 0;   // fotogramas a descartar
    int                      genReps_  = 0;   // generaciones a cronometrar
    int                      skipped_  = 0;   // calentamiento ya consumido
    std::string              csvPath_;        // destino del detalle por fotograma
    std::string              tag_;            // etiqueta libre del usuario
    std::vector<FrameTiming> samples_;        // una entrada por fotograma medido
    std::vector<double>      generations_;    // milisegundos de cada generacion
    double                   drawnAccum_ = 0.0; // suma de bloques dibujados
};

} // namespace mcss
