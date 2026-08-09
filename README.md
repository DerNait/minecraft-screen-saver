# Minecraft Screen Saver — Computación Paralela y Distribuida

Screensaver que genera un terreno estilo Minecraft, bloque a bloque y de forma
pseudoaleatoria. Una vez armado el terreno completo, se sostiene unos segundos,
se desarma, y tras una pausa se genera un terreno nuevo al azar. Ver el pipeline
completo en `docs/Pipeline Minecraft Screen saver.png`.

- `Secuencial/secuencial.cpp` — un solo hilo construye/actualiza todo el terreno.
- `Paralelo/paralelo.cpp` — la misma base, preparada para dividir el terreno
  entre hilos con OpenMP en un avance posterior.

## Avance 1 — ubicación en el código

- **Variable en memoria que almacena los elementos a renderizar**:
  `std::vector<Block> terrain;` en `Secuencial/secuencial.cpp:30`. Cada `Block`
  es un bloque/voxel del terreno (posición actual, posición final, color y
  estado de física).
- **Función de render de un elemento**: `renderTerrain()` en
  `Secuencial/secuencial.cpp:182`. Recorre `terrain` y dibuja todos los
  bloques con instanced rendering (un solo draw call).
- **Función de actualización (cómo determina su siguiente ubicación)**:
  `updateBlock()` en `Secuencial/secuencial.cpp:67`. Aplica una integración
  de gravedad sobre la posición actual del bloque hasta que llega a su
  posición final en el terreno.

## Dependencias

Requiere MSYS2 con el toolchain **UCRT64** (gcc/g++ con soporte OpenMP incluido)
y las siguientes librerías gráficas:

```
pacman -S --needed \
  mingw-w64-ucrt-x86_64-glfw \
  mingw-w64-ucrt-x86_64-glew \
  mingw-w64-ucrt-x86_64-glm
```

## Compilar

Desde una shell **MSYS2 UCRT64** (o agregando `C:\msys64\ucrt64\bin` al `PATH`
para que CMake encuentre `glfw3`, `GLEW`, `glm`, OpenMP y el compilador correcto):

```bash
cd "Proyecto 1"
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
```

Esto genera `build/secuencial.exe` y `build/paralelo.exe`.

## Ejecutar

```bash
./build/secuencial.exe <N>
./build/paralelo.exe <N>
```

`N` = cantidad de bloques del terreno a generar (entero positivo, obligatorio).
El FPS actual se muestra en el título de la ventana.
