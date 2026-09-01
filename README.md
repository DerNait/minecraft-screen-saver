# Minecraft Screen Saver — Computación Paralela y Distribuida

Screensaver que genera un terreno estilo Minecraft, bloque a bloque y de forma
pseudoaleatoria. Una vez armado el terreno completo, se sostiene unos segundos,
se desarma, y tras una pausa se genera un terreno nuevo al azar. Ver el pipeline
completo en `docs/Pipeline Minecraft Screen saver.png`.

- `Secuencial/secuencial.cpp` — un solo hilo construye/actualiza todo el terreno.
- `Paralelo/paralelo.cpp` — la misma simulación, con generación y actualización
  repartidas entre hilos mediante OpenMP.

## Implementación paralela y halo de vegetación

La implementación mantiene la idea del pipeline original: el plano horizontal
del mundo (`X-Z`) se reparte entre los hilos. El mapa de alturas y los estratos
se calculan en paralelo con una distribución estática. Para la vegetación se
forma explícitamente una cuadrícula 2D con un chunk rectangular por hilo. Por
ejemplo, 12 hilos producen una cuadrícula de `4 x 3` chunks.

Los árboles necesitan un tratamiento adicional porque una copa puede comenzar
en un chunk y ocupar celdas de los chunks vecinos. Cada hilo extiende dos celdas
su región de búsqueda en las cuatro direcciones. Esta extensión es el **halo** y
su tamaño corresponde al radio horizontal máximo de las copas actuales.

La regla de acceso es la siguiente:

- El hilo examina raíces de árboles dentro de su chunk y de su halo.
- El hilo escribe troncos, hojas o cactus únicamente en las celdas que
  pertenecen a su propio chunk.
- Las celdas del halo solo se leen para reconstruir la parte de una copa que
  entra al chunk; nunca se escriben como territorio vecino.

Así, los árboles no quedan cortados en las fronteras y cada voxel tiene un solo
hilo escritor. Esto evita carreras de datos sin usar regiones `critical`,
operaciones `atomic` ni bloqueos. La pequeña repetición de cálculos en los
bordes es el costo del halo y no cambia el resultado generado para una misma
semilla.

## Cámara de la versión paralela

La cámara orbital original continúa siendo el comportamiento predeterminado.
La versión paralela también puede alternar automáticamente entre cinco
encuadres: órbita exterior, panorámica interior, órbita interior, diagonal
elevada y vista cenital. Las cámaras interiores móviles mantienen una altura
fija durante cada mundo, independiente del relieve que atraviesan. Los cambios
de encuadre usan una transición suave con elevación para no atravesar el
terreno. El mirador interior estático se conserva en el código, pero está fuera
de la secuencia automática para poder reactivarlo más adelante.

```bash
./build/paralelo.exe 500000 --camera auto
./build/paralelo.exe 500000 --camera auto --camera-change 8
./build/paralelo.exe 500000 --camera orbit
```

`--camera-change` acepta intervalos de 2 a 120 segundos y solo afecta al modo
`auto`. Estas opciones se consideran experimentales de la versión paralela; no
modifican el comportamiento visual de la versión secuencial.

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
