// ============================================================================
//  benchmark.cpp - Implementacion del modo de medicion
// ============================================================================
#include "benchmark.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>

namespace mcss {
namespace {

// ----------------------------------------------------------------------------
//  Estadisticos basicos sobre una lista de tiempos. Todas devuelven 0 cuando la
//  lista esta vacia para que el informe nunca imprima un valor invalido.
// ----------------------------------------------------------------------------
double promedio(const std::vector<double>& v) {
    if (v.empty()) return 0.0;
    return std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size());
}

double minimo(const std::vector<double>& v) {
    if (v.empty()) return 0.0;
    return *std::min_element(v.begin(), v.end());
}

double maximo(const std::vector<double>& v) {
    if (v.empty()) return 0.0;
    return *std::max_element(v.begin(), v.end());
}

// Percentil por interpolacion lineal sobre una copia ordenada.
double percentil(std::vector<double> v, double fraccion) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const double posicion = fraccion * static_cast<double>(v.size() - 1);
    const size_t bajo = static_cast<size_t>(std::floor(posicion));
    const size_t alto = static_cast<size_t>(std::ceil(posicion));
    const double peso = posicion - static_cast<double>(bajo);
    return v[bajo] * (1.0 - peso) + v[alto] * peso;
}

// Desviacion estandar muestral: indica que tan estable fue la medicion.
double desviacion(const std::vector<double>& v) {
    if (v.size() < 2) return 0.0;
    const double media = promedio(v);
    double suma = 0.0;
    for (double x : v) suma += (x - media) * (x - media);
    return std::sqrt(suma / static_cast<double>(v.size() - 1));
}

// Extrae una columna del desglose por fotograma como lista suelta.
std::vector<double> columna(const std::vector<FrameTiming>& muestras,
                            double FrameTiming::*campo) {
    std::vector<double> salida;
    salida.reserve(muestras.size());
    for (const FrameTiming& m : muestras) salida.push_back(m.*campo);
    return salida;
}

// Nombre legible de una fase del ciclo del protector de pantalla.
const char* nombreFase(char codigo) {
    switch (codigo) {
        case 'B': return "construyendo";
        case 'H': return "sostenido";
        case 'D': return "desarmando";
        default:  return "pausa";
    }
}

} // namespace

void Benchmark::start(const AppConfig& cfg) {
    frames_  = cfg.benchFrames;
    warmup_  = cfg.benchWarmup;
    genReps_ = cfg.benchGenReps;
    csvPath_ = cfg.benchCsv;
    tag_     = cfg.benchTag;
    skipped_ = 0;
    samples_.clear();
    generations_.clear();
    drawnAccum_ = 0.0;
    if (frames_ > 0) samples_.reserve(static_cast<size_t>(frames_));
}

void Benchmark::addGeneration(double milliseconds) {
    if (frames_ <= 0) return;
    generations_.push_back(milliseconds);
}

void Benchmark::addFrame(const FrameTiming& timing, size_t drawnBlocks) {
    if (frames_ <= 0) return;
    // Los primeros fotogramas incluyen la subida de buffers a la GPU y el
    // arranque del planificador de OpenMP: se descartan.
    if (skipped_ < warmup_) {
        ++skipped_;
        return;
    }
    if (samples_.size() >= static_cast<size_t>(frames_)) return;
    samples_.push_back(timing);
    drawnAccum_ += static_cast<double>(drawnBlocks);
}

bool Benchmark::report(const BenchmarkInfo& info, std::string& error) const {
    error.clear();

    const std::vector<double> update = columna(samples_, &FrameTiming::updateMs);
    const std::vector<double> mobs   = columna(samples_, &FrameTiming::mobsMs);
    const std::vector<double> build  = columna(samples_, &FrameTiming::buildMs);
    const std::vector<double> shadow = columna(samples_, &FrameTiming::shadowMs);
    const std::vector<double> cpu    = columna(samples_, &FrameTiming::cpuMs);
    const std::vector<double> frame  = columna(samples_, &FrameTiming::frameMs);

    // Fase en la que transcurrio la mayor parte de la medicion.
    int conteo[4] = {0, 0, 0, 0};
    for (const FrameTiming& m : samples_) {
        switch (m.phase) {
            case 'B': ++conteo[0]; break;
            case 'H': ++conteo[1]; break;
            case 'D': ++conteo[2]; break;
            default:  ++conteo[3]; break;
        }
    }
    const int mejor = static_cast<int>(std::max_element(conteo, conteo + 4) - conteo);
    const char codigos[4] = {'B', 'H', 'D', 'P'};

    const double frameProm = promedio(frame);
    const double frameMax  = maximo(frame);
    const double totalSeg  = std::accumulate(frame.begin(), frame.end(), 0.0) / 1000.0;
    const double fpsProm   = frameProm > 0.0 ? 1000.0 / frameProm : 0.0;
    const double fpsMin    = frameMax  > 0.0 ? 1000.0 / frameMax  : 0.0;
    const double dibujados = samples_.empty()
                           ? 0.0 : drawnAccum_ / static_cast<double>(samples_.size());

    // Fotogramas que no alcanzaron los 60 FPS que pide el enunciado.
    size_t lentos = 0;
    for (double ms : frame) {
        if (ms > 1000.0 / 60.0) ++lentos;
    }

    std::printf("==== BENCHMARK ====\n");
    std::printf("version=%s\n",                  info.version);
    std::printf("etiqueta=%s\n",                 tag_.c_str());
    std::printf("hilos=%d\n",                    info.threads);
    std::printf("n_solicitado=%ld\n",            info.targetBlocks);
    std::printf("bloques_generados=%zu\n",       info.worldBlocks);
    std::printf("bloques_dibujados_prom=%.1f\n", dibujados);
    std::printf("semilla=%u\n",                  info.seed);
    std::printf("bioma=%s\n",                    info.biome);
    std::printf("frames_calentamiento=%d\n",     warmup_);
    std::printf("frames_medidos=%zu\n",          samples_.size());
    std::printf("fase_predominante=%s\n",        nombreFase(codigos[mejor]));
    std::printf("gen_muestras=%zu\n",            generations_.size());
    std::printf("gen_ms_prom=%.4f\n",            promedio(generations_));
    std::printf("gen_ms_min=%.4f\n",             minimo(generations_));
    std::printf("gen_ms_max=%.4f\n",             maximo(generations_));
    std::printf("update_ms_prom=%.4f\n",         promedio(update));
    std::printf("mobs_ms_prom=%.4f\n",           promedio(mobs));
    std::printf("build_ms_prom=%.4f\n",          promedio(build));
    std::printf("sombras_ms_prom=%.4f\n",        promedio(shadow));
    std::printf("cpu_ms_prom=%.4f\n",            promedio(cpu));
    std::printf("cpu_ms_min=%.4f\n",             minimo(cpu));
    std::printf("cpu_ms_max=%.4f\n",             maximo(cpu));
    std::printf("cpu_ms_desv=%.4f\n",            desviacion(cpu));
    std::printf("frame_ms_prom=%.4f\n",          frameProm);
    std::printf("frame_ms_min=%.4f\n",           minimo(frame));
    std::printf("frame_ms_max=%.4f\n",           frameMax);
    std::printf("frame_ms_p95=%.4f\n",           percentil(frame, 0.95));
    std::printf("fps_prom=%.4f\n",               fpsProm);
    std::printf("fps_min=%.4f\n",                fpsMin);
    std::printf("frames_bajo_60fps=%zu\n",       lentos);
    std::printf("tiempo_medido_s=%.6f\n",        totalSeg);
    std::printf("==== FIN BENCHMARK ====\n");
    std::fflush(stdout);

    if (csvPath_.empty()) return true;

    // Detalle fotograma a fotograma: permite graficar la estabilidad de los FPS
    // y adjuntar la evidencia cruda en la bitacora de pruebas del informe.
    std::FILE* archivo = std::fopen(csvPath_.c_str(), "w");
    if (archivo == nullptr) {
        error = "no se pudo escribir el CSV por fotograma en " + csvPath_;
        return false;
    }
    std::fprintf(archivo,
                 "version,hilos,n_solicitado,frame,fase,"
                 "update_ms,mobs_ms,build_ms,sombras_ms,cpu_ms,frame_ms,fps\n");
    for (size_t i = 0; i < samples_.size(); ++i) {
        const FrameTiming& m = samples_[i];
        std::fprintf(archivo,
                     "%s,%d,%ld,%zu,%s,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.3f\n",
                     info.version, info.threads, info.targetBlocks, i + 1,
                     nombreFase(m.phase), m.updateMs, m.mobsMs, m.buildMs,
                     m.shadowMs, m.cpuMs, m.frameMs,
                     m.frameMs > 0.0 ? 1000.0 / m.frameMs : 0.0);
    }
    std::fclose(archivo);
    return true;
}

} // namespace mcss
