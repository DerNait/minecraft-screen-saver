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

## Mobs e IA básica

La primera tanda de mobs agrega cerdos, vacas y ovejas con las texturas
originales de `assets/textures/entity/`. Se renderizan como modelos cuadrúpedos
instanciados, separados del buffer de bloques del terreno. Los zombies se
conservan como referencia interna, pero no forman parte de la población activa.

Los animales aparecen cuando termina la construcción del mundo. Cada uno puede
esperar o caminar, elige su dirección mediante una secuencia pseudoaleatoria
reproducible y evita bordes, desniveles mayores a un bloque, árboles y cactus.

Cada animal recorre cuatro fases de vida —`Appearing`, `Present`, `Vanishing` y
`Gone`— con la misma idea que los bloques del terreno. No aparece ni desaparece
de golpe: crece desde el suelo de escala 0 a 1 en 0.45 s y, cuando el terreno
empieza a desarmarse, encoge hasta desvanecerse en 0.35 s. La escala se aplica en
el vertex shader desde los pies del modelo, de modo que el animal parece brotar
del terreno y hundirse en él. Cada uno lleva además un retraso propio —hasta
0.9 s al llegar y 0.7 s al retirarse— para que la manada entera no se materialice
ni se borre en el mismo instante. Mientras crece o encoge el animal no camina:
solo cambia de tamaño. Los que ya se fueron dejan de generar instancia y no se
envían a la GPU.

Cada animal tiene cabeza, cuerpo y cuatro patas independientes. Como en los
modelos de Minecraft, las cuatro patas pueden reutilizar la misma región de la
textura; las patas que corresponden al lado opuesto se reflejan geométricamente
en vez de depender de otra zona del PNG. La oveja se dibuja en dos capas: cuerpo
base y lana ligeramente expandida.

Las proporciones y coordenadas UV toman como referencia los modelos oficiales
de Mojang para [cerdo](https://github.com/Mojang/bedrock-samples/blob/main/resource_pack/models/entity/pig.v3.geo.json),
[vaca](https://github.com/Mojang/bedrock-samples/blob/main/resource_pack/models/entity/cow.v2.geo.json)
y [oveja](https://github.com/Mojang/bedrock-samples/blob/main/resource_pack/models/entity/sheep.geo.json).

```bash
./build/paralelo.exe 500000 --mobs 20 --hold 20
./build/paralelo.exe 500000 --threads 12 --camera auto --mobs 100 --hold 30
```

`--mobs` acepta de 0 a 5000 entidades. Su valor predeterminado es 0 para que las
mediciones existentes del terreno no cambien. En esta primera tanda la IA se
actualiza secuencialmente; el siguiente paso será separar percepción, propuesta
y resolución para comparar su actualización secuencial y paralela.

## Modelo del cactus

El cactus es el único bloque del catálogo cuyo modelo **no es un cubo completo**.
En el juego original sus cuatro caras laterales están metidas un píxel (1/16)
hacia adentro y solo la cara superior y la inferior conservan el tamaño del
bloque, tal como define
[`block/cactus.json`](https://github.com/InventivetalentDev/minecraft-assets/blob/1.20.1/assets/minecraft/models/block/cactus.json):

```json
{ "from": [0, 0, 0],  "to": [16, 16, 16], "faces": { "down": ..., "up": ... } }
{ "from": [0, 0, 1],  "to": [16, 16, 15], "faces": { "north": ..., "south": ... } }
{ "from": [1, 0, 0],  "to": [15, 16, 16], "faces": { "west": ...,  "east": ... } }
```

Ese desplazamiento no es decorativo: las texturas `cactus_side`, `cactus_top` y
`cactus_bottom` traen un borde transparente de un píxel, y el modelo está armado
para que ese borde caiga exactamente donde empieza la cara perpendicular. La
parte opaca de cada cara lateral termina en el mismo plano donde nace la cara
vecina, y el contorno opaco de la cara superior coincide con las cuatro caras
laterales ya desplazadas. Todo cierra.

Dibujado como cubo completo, en cambio, ese borde transparente queda sobre la
arista del bloque, el `discard` del fragment shader lo elimina y se ve un hueco
vertical en cada esquina del cactus. Por eso `CubeRenderer::setInsetSideLayer()`
recibe la capa del atlas de `cactus_side` y el vertex shader desplaza esas cuatro
caras 1/16 hacia adentro. Es el único caso especial de geometría del
renderizador; el resto de los bloques siguen siendo cubos completos.

## Iluminación y ciclo de día y noche

La versión paralela incluye un modo visual experimental que reemplaza el
sombreado fijo por iluminación direccional dinámica. El sol recorre el cielo y
modifica de forma continua la luz difusa, la luz ambiental, el color de la
neblina y el tono del horizonte. Durante la noche la luna aporta una luz azul
suave para que el terreno y los animales sigan siendo visibles.

El cielo se dibuja como un skybox procedural con gradiente atmosférico, brillo
del sol y la luna, estrellas y nubes cuadradas en movimiento. No necesita
texturas adicionales. El mismo estado de iluminación se envía a los shaders del
terreno y de los mobs para que ambos reaccionen al ciclo de forma consistente.

```bash
./build/paralelo.exe 500000 --lighting cycle
./build/paralelo.exe 500000 --lighting cycle --day-cycle 30
./build/paralelo.exe 500000 --threads 12 --camera auto --mobs 30 --hold 60 --lighting cycle
./build/paralelo.exe 500000 --lighting cycle --shadows near
./build/paralelo.exe 500000 --lighting cycle --shadows near --shadow-distance 64
```

`--day-cycle` acepta de 10 a 600 segundos y representa la duración de un ciclo
completo. El 70 % de ese tiempo corresponde al recorrido diurno y el 30 % al
nocturno; por ejemplo, un ciclo de 60 segundos usa aproximadamente 42 segundos
para el día y 18 para la noche. El modo predeterminado continúa siendo
`--lighting classic`, que no dibuja el skybox y conserva el sombreado anterior
para poder repetir los benchmarks existentes.

Las sombras cercanas se activan con `--shadows near` y requieren el modo
`--lighting cycle`. Se usa un mapa de profundidad de `2048 x 2048` centrado en
la posición horizontal de la cámara. Si la cámara está fuera del terreno, el
centro se limita al punto más cercano del borde; por ejemplo, una toma exterior
desde una esquina concentra las sombras en esa esquina y no en el centro del
mundo. `--shadow-distance` controla un radio de 16 a 128 bloques y su valor
predeterminado es 48. Aumentar el radio permite que más objetos proyecten
sombra, pero reparte la misma resolución sobre una superficie mayor y puede
reducir la nitidez. En el borde del radio las sombras desaparecen gradualmente.

Antes del pase de profundidad, la versión paralela compacta con OpenMP los
bloques cercanos que pueden proyectar sombra. De esta manera el terreno lejano
no se vuelve a enviar completo a la GPU. Árboles, relieve, cerdos, vacas y
ovejas participan en el mapa; el shader aplica un filtro PCF de nueve muestras
para suavizar ligeramente los bordes pixelados.

El cálculo visual del ciclo no se paraleliza con OpenMP: sus colores se obtienen
una vez por fotograma en el hilo principal y el trabajo por píxel se ejecuta en
los shaders de la GPU. OpenGL permanece fuera de las regiones paralelas, igual
que en el resto del pipeline. La selección y compactación de los bloques
cercanos para las sombras sí se reparte entre los hilos, pero la creación del
mapa de profundidad continúa ejecutándose en la GPU desde el hilo principal.

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

## Mediciones: speedup, eficiencia y FPS

Ambos ejecutables comparten un modo de medicion (`--benchmark`) que sustituye el
comportamiento interactivo del protector de pantalla por una prueba repetible:

- **Paso de tiempo fijo de 1/60 s.** La simulacion avanza siempre lo mismo, sin
  importar que tan rapido corra la maquina; asi la version secuencial y la
  paralela ejecutan exactamente el mismo trabajo.
- **Semilla fija** (`20260901` si no se indica otra), de modo que el mundo
  generado sea identico en cada repeticion y entre versiones.
- **Sin sincronizacion vertical**, para que el monitor no imponga un techo
  artificial de 60 FPS.
- **Fotogramas de calentamiento descartados**, para que la subida de texturas a
  la GPU y el arranque del planificador de OpenMP no contaminen los promedios.
- **`glFinish()` antes de cerrar el cronometro** de cada fotograma, para que el
  tiempo medido incluya el trabajo real de la GPU y no solo el encolado.

Al terminar, el programa imprime un informe `clave=valor` con el tiempo de
generacion del mundo, el desglose por etapa (`update`, `build`, sombras), el
tiempo de CPU y de fotograma completo, los FPS promedio y minimos, y cuantos
fotogramas quedaron por debajo de los 60 FPS que pide el enunciado.

```bash
./build/secuencial.exe 2000000 --benchmark 300 --bench-warmup 120 --build 1 --hold 120
./build/paralelo.exe   2000000 --benchmark 300 --threads 12 --build 1 --hold 120
./build/paralelo.exe   2000000 --benchmark 300 --bench-csv detalle.csv
```

### Script de metricas y graficas

`metricas/medir_y_graficar.py` compila el proyecto, repite cada prueba, guarda
los CSV y dibuja las graficas comparativas:

```bash
python metricas/medir_y_graficar.py
python metricas/medir_y_graficar.py --n 500000 --hilos 1 2 4 8 --repeticiones 10
python metricas/medir_y_graficar.py --sin-compilar --sin-detalle
```

Opciones principales: `--n` (valores de N a probar), `--hilos`, `--repeticiones`
(10 por omision, el minimo que pide la bitacora de pruebas), `--frames`,
`--calentamiento`, `--generaciones`, `--semilla`, `--ancho`, `--alto`,
`--sin-compilar`, `--sin-detalle` y `--solo-graficas` (redibuja desde
`resumen.csv` sin volver a medir). Requiere `matplotlib`
(`python -m pip install matplotlib`).

Todo queda en `metricas/resultados/`:

| Archivo | Contenido |
| --- | --- |
| `mediciones_crudas.csv` | Una fila por ejecucion, con todas las repeticiones. |
| `resumen.csv` | Promedios, speedup y eficiencia por combinacion de N e hilos. |
| `detalle_secuencial.csv`, `detalle_paralelo.csv` | Tiempos fotograma a fotograma. |
| `speedup_vs_hilos.png` | Speedup medido contra el speedup ideal. |
| `eficiencia_vs_hilos.png` | Eficiencia (speedup / hilos) por configuracion. |
| `tiempo_cpu_vs_hilos.png` | Tiempo de CPU por fotograma contra la linea base. |
| `fps_vs_hilos.png` | FPS alcanzados frente al minimo de 60 FPS. |
| `etapas_por_fotograma.png` | Desglose `update` / `build` / CPU total. |
| `generacion_vs_hilos.png` | Costo de generar un mundo completo. |
| `speedup_vs_n.png` | Como escala el speedup al crecer N. |
| `fps_por_fotograma.png` | Estabilidad del ritmo durante la medicion. |

### Resultados obtenidos

Equipo de prueba: Intel Core i5-12400 (6 nucleos fisicos, 12 hilos logicos), ventana de
1280x720, 10 repeticiones de 300 fotogramas por configuracion, semilla 20260901.
Los valores completos estan en `metricas/resultados/resumen.csv`.

| N | Bloques | CPU secuencial | CPU paralelo (12 h) | Speedup | Eficiencia | FPS secuencial | FPS paralelo |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 100 000 | 95 874 | 0.51 ms | 0.41 ms | 1.25x | 10.4 % | 1113 | 1235 |
| 500 000 | 482 184 | 2.99 ms | 1.19 ms | 2.52x | 21.0 % | 232 | 393 |
| 2 000 000 | 1 918 719 | 12.23 ms | 4.48 ms | 2.73x | 22.7 % | 53 | 117 |

El speedup se satura en 8 hilos (2.73x, 34.2 % de eficiencia con N = 2 000 000).
Pasar a 12 no aporta nada porque el equipo tiene 6 nucleos fisicos: los hilos
adicionales son hilos logicos SMT que comparten las mismas unidades de ejecucion.

El caso decisivo es N = 2 000 000: la version secuencial promedia 53 FPS y deja
264 de 300 fotogramas por debajo de los 60 FPS, mientras que la paralela sostiene
117 FPS y menos de un fotograma por corrida por debajo del limite. Es decir, la
paralelizacion no solo acelera el calculo: es lo que permite mantener la
experiencia de usuario que pide el enunciado con esa cantidad de elementos.

Con N = 100 000 la mejora es marginal: el trabajo por fotograma (0.5 ms) es tan
pequeno que el costo de abrir y cerrar las regiones paralelas se come casi toda
la ganancia. Ese efecto se ve tambien en la fila de 1 hilo del `resumen.csv`,
donde la version paralela resulta entre 21 % y 27 % mas lenta que la secuencial:
es el sobrecosto puro de OpenMP, sin ningun hilo extra que lo compense.

La generacion del mundo, en cambio, escala mejor que el bucle por fotograma
(3.85x con 12 hilos y N = 2 000 000) porque es un bloque de trabajo grande y
contiguo, sin sincronizacion con la GPU de por medio.

### Como leer los numeros

Se reportan tres speedups distintos porque miden cosas distintas:

- **Speedup de CPU** (`speedup_cpu`): sobre el trabajo por fotograma que OpenMP
  reparte. Es el que corresponde a la parte paralelizada del programa.
- **Speedup de generacion** (`speedup_generacion`): sobre la construccion
  completa del mundo, el bucle con mayor fraccion paralelizable.
- **Speedup de fotograma** (`speedup_frame`): sobre la iteracion completa. Es
  siempre menor porque incluye el dibujo en la GPU, que es identico en las dos
  versiones y actua como la fraccion serial de la ley de Amdahl.

La eficiencia se calcula como `speedup / hilos`. Cae al agregar hilos porque la
parte no paralelizable (dibujo, presentacion, sincronizacion) crece en peso
relativo, y porque los hilos logicos de SMT no aportan un nucleo completo.

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

## Parametros de linea de comandos

Los dos ejecutables aceptan exactamente las mismas opciones. Ningun valor esta
escrito de forma fija en el codigo: todos se leen aqui, con un rango verificado
antes de arrancar. Si un valor no es numerico, queda fuera de rango, falta el
valor de una opcion o la opcion no existe, el programa explica el error, imprime
la ayuda completa y termina sin abrir la ventana.

```bash
./build/paralelo.exe --help          # lista completa con rangos y omisiones
```

### Argumento obligatorio

| Parametro | Rango | Descripcion |
| --- | --- | --- |
| `N` | 64 a 20 000 000 | Cantidad objetivo de bloques a renderizar. Es el unico argumento posicional y va siempre primero. El programa deduce de N el lado de la reticula, asi que la cantidad final de bloques es la aproximacion mas cercana (por ejemplo, N = 500 000 genera 482 184 bloques con la semilla de prueba). |

### Ventana

| Parametro | Rango | Omision | Descripcion |
| --- | --- | --- | --- |
| `--width <px>` | 640 a 16384 | 1280 | Ancho del lienzo en pixeles. El minimo respeta el tamano de canvas que pide el enunciado. |
| `--height <px>` | 480 a 16384 | 720 | Alto del lienzo en pixeles. |
| `--no-vsync` | sin valor | vsync activo | Desactiva la sincronizacion vertical. Sin esta opcion el monitor limita el programa a su frecuencia de refresco y los FPS medidos no pasan de ~60. El modo `--benchmark` la activa solo. |

### Mundo

| Parametro | Rango | Omision | Descripcion |
| --- | --- | --- | --- |
| `--seed <n>` | entero >= 0 | 0 | Semilla del mundo. Con 0 se sortea una nueva en cada arranque y en cada ciclo; con un valor fijo el terreno, el bioma y la vegetacion se repiten identicos, que es lo que hace reproducibles las mediciones. |
| `--relief <f>` | 0.2 a 4.0 | 1.0 | Multiplicador global del relieve. Valores bajos aplanan el terreno; valores altos exageran montanas y valles. |
| `--biome <n>` | -1 a 64 | -1 | Fuerza un bioma por indice (se normaliza contra la cantidad de biomas disponibles). Con -1 lo elige la semilla. |

### Tiempos del ciclo del protector de pantalla

El ciclo es: construir el terreno, sostenerlo, desarmarlo y pausar antes del
mundo siguiente.

| Parametro | Rango | Omision | Descripcion |
| --- | --- | --- | --- |
| `--build <s>` | 0.5 a 120 | 6.0 | Segundos que tarda el terreno en armarse capa por capa. |
| `--hold <s>` | 0 a 120 | 4.0 | Segundos que el mundo permanece completo. Es la fase donde conviene medir, porque todos los bloques estan vivos. |
| `--dissolve <s>` | 0.5 a 120 | 3.0 | Segundos que tarda el desarmado, de arriba hacia abajo. |
| `--pause <s>` | 0 a 60 | 0.8 | Pausa en negro antes de generar el mundo siguiente. |

### Camara, iluminacion y mobs

| Parametro | Rango | Omision | Descripcion |
| --- | --- | --- | --- |
| `--camera <modo>` | `orbit` o `auto` | `orbit` | `orbit` mantiene la orbita exterior original. `auto` alterna entre cinco encuadres (orbita exterior, panoramica interior, orbita interior, diagonal elevada y vista cenital) con transiciones elevadas. |
| `--camera-change <s>` | 2 a 120 | 10.0 | Segundos entre cambios de encuadre. Solo tiene efecto con `--camera auto`. |
| `--mobs <n>` | 0 a 5000 | 0 | Cantidad de animales (cerdos, vacas y ovejas) que aparecen cuando el mundo queda armado. Su IA es secuencial en ambas versiones; el valor 0 deja el benchmark de terreno sin contaminar. |
| `--lighting <modo>` | `classic` o `cycle` | `classic` | `classic` conserva el sombreado fijo por cara. `cycle` activa el ciclo de dia y noche con skybox procedural, sol, luna, estrellas y nubes. |
| `--day-cycle <s>` | 10 a 600 | 60.0 | Duracion de un dia completo en modo `cycle`: 70 % de recorrido diurno y 30 % nocturno. |
| `--shadows <modo>` | `off` o `near` | `off` | `near` agrega un mapa de sombras de 2048 x 2048 centrado en la camara. **Requiere `--lighting cycle`**; combinarlo con `classic` es un error y el programa lo rechaza. |
| `--shadow-distance <n>` | 16 a 128 | 48.0 | Radio en bloques alrededor de la vista que proyecta sombra. Un radio mayor cubre mas terreno pero reparte la misma resolucion sobre mas superficie, asi que las sombras pierden nitidez. |

### Ejecucion

| Parametro | Rango | Omision | Descripcion |
| --- | --- | --- | --- |
| `--threads <n>` | 1 a 256 | los del sistema | Cantidad de hilos de OpenMP. Solo tiene efecto en `paralelo`; en `secuencial` el programa avisa por la salida de error y lo ignora. Se desactiva el ajuste dinamico de OpenMP para que el valor pedido sea el que realmente se use. |
| `--assets <ruta>` | ruta valida | busqueda automatica | Carpeta con las texturas `.png`. Sin esta opcion el programa busca `assets/textures` a partir del directorio actual. |
| `--help`, `-h` | sin valor | - | Imprime la lista completa de opciones con sus rangos y termina. Se detecta en cualquier posicion, incluso si el resto de los argumentos esta mal escrito. |

### Modo de medicion

Estas opciones son las que producen los numeros de speedup y eficiencia. Ver la
seccion [Mediciones](#mediciones-speedup-eficiencia-y-fps) para el detalle de
como funcionan.

| Parametro | Rango | Omision | Descripcion |
| --- | --- | --- | --- |
| `--benchmark <f>` | 30 a 200 000 | 0 (desactivado) | Cronometra `f` fotogramas y termina imprimiendo el informe `clave=valor`. Al activarse fija el paso de tiempo en 1/60 s, desactiva la sincronizacion vertical, ignora la tecla ESPACIO y usa la semilla 20260901 si no se indico otra. |
| `--bench-warmup <f>` | 0 a 100 000 | 120 | Fotogramas descartados antes de empezar a medir. Con el valor por omision y `--build 1` la medicion cae completa en la fase sostenida. |
| `--bench-gen <n>` | 1 a 100 | 5 | Generaciones completas del mundo que se cronometran antes del bucle principal. Todas usan la misma semilla, asi que el promedio mide solo el costo de generar. |
| `--bench-csv <ruta>` | ruta no vacia | no se escribe | Guarda un CSV con el desglose de cada fotograma medido (fase, update, build, sombras, CPU, fotograma completo y FPS). |
| `--bench-tag <texto>` | texto libre | vacio | Etiqueta que se copia tal cual al informe. Sirve para identificar la corrida cuando se automatizan muchas. |

### Ejemplos

```bash
# Protector de pantalla normal
./build/paralelo.exe 500000

# Mundo grande, todas las funciones visuales, 12 hilos
./build/paralelo.exe 2000000 --threads 12 --camera auto --mobs 30 --lighting cycle --shadows near --hold 30

# Terreno plano y reproducible, sin vsync, para inspeccionar a ojo
./build/secuencial.exe 300000 --seed 1234 --relief 0.4 --no-vsync

# Medicion de 300 fotogramas en fase sostenida, con detalle por fotograma
./build/paralelo.exe 2000000 --benchmark 300 --bench-warmup 120 --build 1 --hold 120 --threads 8 --bench-csv detalle.csv
```
