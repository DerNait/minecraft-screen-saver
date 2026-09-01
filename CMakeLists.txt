# ============================================================================
#  Minecraft Screen Saver - Computacion Paralela y Distribuida (UVG)
#  Construye dos ejecutables que comparten la misma infraestructura:
#    secuencial : linea base de un solo hilo
#    paralelo   : misma simulacion repartida entre hilos con OpenMP
# ============================================================================
cmake_minimum_required(VERSION 3.20)
project(MinecraftScreenSaver CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Sin un tipo de compilacion explicito CMake no aplica ninguna optimizacion, lo
# que falsearia por completo las mediciones de tiempo. Release usa -O3.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Tipo de compilacion" FORCE)
endif()

find_package(glfw3 CONFIG REQUIRED)   # ventana y contexto de OpenGL
find_package(GLEW REQUIRED)           # carga de las extensiones de OpenGL
find_package(glm CONFIG REQUIRED)     # algebra lineal (matrices de camara)
find_package(OpenGL REQUIRED)
# En macOS el compilador de Apple no trae OpenMP integrado: se instala aparte
# con "brew install libomp". CMake no lo encuentra solo, asi que se le indican
# las rutas antes de buscarlo. En Linux y en MSYS2/Windows este bloque no se
# ejecuta porque gcc ya incluye soporte de OpenMP.
if(APPLE AND NOT DEFINED OpenMP_CXX_FLAGS)
    execute_process(COMMAND brew --prefix libomp
                    OUTPUT_VARIABLE LIBOMP_PREFIX
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_QUIET)
    if(LIBOMP_PREFIX AND EXISTS "${LIBOMP_PREFIX}/include/omp.h")
        set(OpenMP_CXX_FLAGS "-Xclang -fopenmp -I${LIBOMP_PREFIX}/include")
        set(OpenMP_CXX_LIB_NAMES "omp")
        set(OpenMP_omp_LIBRARY "${LIBOMP_PREFIX}/lib/libomp.dylib")
    endif()
endif()

find_package(OpenMP REQUIRED)         # paralelismo de memoria compartida
find_package(ZLIB REQUIRED)           # descompresion DEFLATE de los PNG

# ---------------------------------------------------------------------------
#  Infraestructura compartida por ambas versiones. Deliberadamente NO contiene
#  los bucles que se paralelizan: esos viven en secuencial.cpp y paralelo.cpp,
#  para que la diferencia entre ambos programas sea exactamente lo que se
#  paralelizo.
# ---------------------------------------------------------------------------
add_library(mcss_common STATIC
    common/png_loader.cpp
    common/block_catalog.cpp
    common/texture_atlas.cpp
    common/render.cpp
    common/mob_renderer.cpp
    common/app_config.cpp
)
target_include_directories(mcss_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/common)
target_link_libraries(mcss_common PUBLIC
    glfw GLEW::GLEW OpenGL::GL glm::glm ZLIB::ZLIB OpenMP::OpenMP_CXX)

add_executable(secuencial Secuencial/secuencial.cpp)
target_link_libraries(secuencial PRIVATE mcss_common)

add_executable(paralelo Paralelo/paralelo.cpp)
target_link_libraries(paralelo PRIVATE mcss_common)
