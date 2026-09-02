"""Mide el screensaver secuencial y el paralelo, y genera los CSV y las graficas.

Uso tipico (desde la carpeta "Proyecto 1"):

    python metricas/medir_y_graficar.py
    python metricas/medir_y_graficar.py --n 200000 1000000 --repeticiones 10
    python metricas/medir_y_graficar.py --sin-compilar --hilos 1 2 4 8

Cada ejecucion lanza el programa en modo --benchmark: paso de tiempo fijo de
1/60 s, semilla fija, sin sincronizacion vertical y con fotogramas de
calentamiento descartados. Ambas versiones recorren asi el mismo trabajo, que es
lo unico que hace comparables los tiempos.
"""

from __future__ import annotations

import argparse
import csv
import os
import platform
import statistics
import subprocess
import sys
from pathlib import Path

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
except ImportError as error:
    raise SystemExit(
        "Falta matplotlib. Instalalo con: python -m pip install matplotlib"
    ) from error


RAIZ = Path(__file__).resolve().parent.parent
CONSTRUCCION = RAIZ / "build"
RESULTADOS = Path(__file__).resolve().parent / "resultados"

INICIO_INFORME = "==== BENCHMARK ===="
FIN_INFORME = "==== FIN BENCHMARK ===="

# --- Paleta ------------------------------------------------------------------
# Tres tonos categoricos validados (azul, naranja, aqua) mas tinta neutra para
# el texto y gris para las lineas de referencia, que nunca compiten con los
# datos. El mismo color identifica siempre a la misma serie en todas las
# graficas: azul = secuencial, naranja = paralelo, aqua = tercera serie.
AZUL = "#2a78d6"
NARANJA = "#eb6834"
AQUA = "#1baf7a"
GRIS = "#8a8985"
TINTA = "#0b0b0b"
TINTA_SUAVE = "#52514e"
SUPERFICIE = "#fcfcfb"

# Colores por valor de N cuando varias curvas comparten una grafica.
COLORES_SERIE = [AZUL, NARANJA, AQUA]


def ejecutable(nombre: str) -> Path:
    """Ruta del binario compilado, con la extension que corresponda al sistema."""
    sufijo = ".exe" if platform.system() == "Windows" else ""
    return CONSTRUCCION / (nombre + sufijo)


def compilar() -> None:
    """Configura y compila el proyecto en modo Release antes de medir."""
    print("Compilando en modo Release...")
    generador = ["-G", "MinGW Makefiles"] if platform.system() == "Windows" else []
    subprocess.run(
        ["cmake", "-S", str(RAIZ), "-B", str(CONSTRUCCION),
         "-DCMAKE_BUILD_TYPE=Release"] + generador,
        check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    subprocess.run(
        ["cmake", "--build", str(CONSTRUCCION)],
        check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)


def leer_informe(salida: str) -> dict[str, str]:
    """Extrae el bloque clave=valor que imprime el programa al terminar de medir."""
    lineas = salida.splitlines()
    if INICIO_INFORME not in lineas:
        raise RuntimeError("el programa no imprimio el informe de medicion:\n" + salida)
    inicio = lineas.index(INICIO_INFORME) + 1
    informe: dict[str, str] = {}
    for linea in lineas[inicio:]:
        if linea == FIN_INFORME:
            break
        clave, _, valor = linea.partition("=")
        informe[clave] = valor
    return informe


def medir(version: str, n: int, hilos: int, opciones: argparse.Namespace,
          csv_detalle: Path | None = None) -> dict[str, str]:
    """Lanza una ejecucion en modo medicion y devuelve su informe ya interpretado."""
    comando = [
        str(ejecutable(version)), str(n),
        "--benchmark", str(opciones.frames),
        "--bench-warmup", str(opciones.calentamiento),
        "--bench-gen", str(opciones.generaciones),
        "--seed", str(opciones.semilla),
        "--build", str(opciones.construccion),
        "--hold", str(opciones.sostenido),
        "--width", str(opciones.ancho),
        "--height", str(opciones.alto),
        "--bench-tag", version + "-" + str(hilos) + "h",
    ]
    if version == "paralelo":
        comando += ["--threads", str(hilos)]
    if csv_detalle is not None:
        comando += ["--bench-csv", str(csv_detalle)]

    resultado = subprocess.run(comando, cwd=str(RAIZ), text=True,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if resultado.returncode != 0:
        raise RuntimeError(
            "fallo la ejecucion de " + version + " (codigo " +
            str(resultado.returncode) + "):\n" + resultado.stderr)
    return leer_informe(resultado.stdout)


def promedio_de(informes: list[dict[str, str]], clave: str) -> float:
    """Promedio de una metrica a lo largo de todas las repeticiones de una prueba."""
    return statistics.fmean(float(informe[clave]) for informe in informes)


def desviacion_de(informes: list[dict[str, str]], clave: str) -> float:
    """Desviacion estandar de una metrica; 0 si solo hubo una repeticion."""
    valores = [float(informe[clave]) for informe in informes]
    return statistics.stdev(valores) if len(valores) > 1 else 0.0


# --- Columnas del informe que se guardan tal cual en el CSV crudo -------------
METRICAS = [
    "bloques_generados", "bloques_dibujados_prom", "frames_medidos",
    "fase_predominante", "gen_ms_prom", "update_ms_prom", "build_ms_prom",
    "cpu_ms_prom", "cpu_ms_desv", "frame_ms_prom", "frame_ms_p95",
    "fps_prom", "fps_min", "frames_bajo_60fps",
]


def recolectar(opciones: argparse.Namespace):
    """Ejecuta todas las pruebas y devuelve las filas crudas y los informes agrupados."""
    crudas: list[dict[str, object]] = []
    secuenciales: dict[int, list[dict[str, str]]] = {}
    paralelos: dict[tuple[int, int], list[dict[str, str]]] = {}

    total = len(opciones.n) * opciones.repeticiones * (1 + len(opciones.hilos))
    hecho = 0

    for n in opciones.n:
        for version, hilos in [("secuencial", 1)] + [("paralelo", h) for h in opciones.hilos]:
            informes: list[dict[str, str]] = []
            for repeticion in range(1, opciones.repeticiones + 1):
                hecho += 1
                print("[" + str(hecho) + "/" + str(total) + "] N=" + str(n) +
                      " " + version + " " + str(hilos) + " hilo(s), repeticion " +
                      str(repeticion), flush=True)
                informe = medir(version, n, hilos, opciones)
                informes.append(informe)

                fila: dict[str, object] = {
                    "version": version, "hilos": hilos, "n_solicitado": n,
                    "repeticion": repeticion,
                }
                for clave in METRICAS:
                    fila[clave] = informe[clave]
                crudas.append(fila)

            if version == "secuencial":
                secuenciales[n] = informes
            else:
                paralelos[(n, hilos)] = informes

    return crudas, secuenciales, paralelos


def resumir(opciones, secuenciales, paralelos) -> list[dict[str, object]]:
    """Calcula speedup y eficiencia de cada combinacion de N y cantidad de hilos.

    El speedup es T_secuencial / T_paralelo y la eficiencia es speedup / hilos.
    Se reporta por separado el trabajo de CPU repartido con OpenMP, la generacion
    del mundo y el fotograma completo (que incluye el dibujo en la GPU, igual en
    ambas versiones y por lo tanto un limite de Amdahl para la mejora visible).
    """
    filas: list[dict[str, object]] = []
    for n in opciones.n:
        base = secuenciales[n]
        cpu_seq = promedio_de(base, "cpu_ms_prom")
        gen_seq = promedio_de(base, "gen_ms_prom")
        frame_seq = promedio_de(base, "frame_ms_prom")
        fps_seq = promedio_de(base, "fps_prom")

        for hilos in opciones.hilos:
            par = paralelos[(n, hilos)]
            cpu_par = promedio_de(par, "cpu_ms_prom")
            gen_par = promedio_de(par, "gen_ms_prom")
            frame_par = promedio_de(par, "frame_ms_prom")
            fps_par = promedio_de(par, "fps_prom")
            speedup = cpu_seq / cpu_par if cpu_par > 0 else 0.0

            filas.append({
                "n_solicitado": n,
                "bloques_generados": base[0]["bloques_generados"],
                "hilos": hilos,
                "cpu_secuencial_ms": round(cpu_seq, 4),
                "cpu_paralelo_ms": round(cpu_par, 4),
                "cpu_paralelo_desv_ms": round(desviacion_de(par, "cpu_ms_prom"), 4),
                "speedup_cpu": round(speedup, 4),
                "eficiencia_pct": round(speedup / hilos * 100.0, 2),
                "gen_secuencial_ms": round(gen_seq, 4),
                "gen_paralelo_ms": round(gen_par, 4),
                "speedup_generacion": round(gen_seq / gen_par, 4) if gen_par > 0 else 0.0,
                "frame_secuencial_ms": round(frame_seq, 4),
                "frame_paralelo_ms": round(frame_par, 4),
                "speedup_frame": round(frame_seq / frame_par, 4) if frame_par > 0 else 0.0,
                "fps_secuencial": round(fps_seq, 2),
                "fps_paralelo": round(fps_par, 2),
                "update_secuencial_ms": round(promedio_de(base, "update_ms_prom"), 4),
                "update_paralelo_ms": round(promedio_de(par, "update_ms_prom"), 4),
                "build_secuencial_ms": round(promedio_de(base, "build_ms_prom"), 4),
                "build_paralelo_ms": round(promedio_de(par, "build_ms_prom"), 4),
            })
    return filas


def guardar_csv(ruta: Path, filas: list[dict[str, object]]) -> None:
    """Escribe una lista de diccionarios homogeneos como CSV."""
    if not filas:
        return
    with ruta.open("w", newline="", encoding="utf-8") as archivo:
        escritor = csv.DictWriter(archivo, fieldnames=list(filas[0].keys()))
        escritor.writeheader()
        escritor.writerows(filas)
    print("  " + ruta.name)


# ============================================================================
#  GRAFICAS
# ============================================================================

def preparar_ejes(ax, titulo: str, etiqueta_x: str, etiqueta_y: str) -> None:
    """Deja los ejes con rejilla discreta y tinta neutra: los datos mandan."""
    ax.set_title(titulo, color=TINTA, fontsize=12, pad=12, loc="left")
    ax.set_xlabel(etiqueta_x, color=TINTA_SUAVE, fontsize=10)
    ax.set_ylabel(etiqueta_y, color=TINTA_SUAVE, fontsize=10)
    ax.grid(True, color="#e3e2dd", linewidth=0.8)
    ax.set_axisbelow(True)
    ax.tick_params(colors=TINTA_SUAVE, labelsize=9)
    for lado in ("top", "right"):
        ax.spines[lado].set_visible(False)
    for lado in ("left", "bottom"):
        ax.spines[lado].set_color("#c9c8c2")


def nueva_figura(ancho: float, alto: float, columnas: int = 1):
    """Crea una figura con el fondo claro comun a todas las graficas."""
    figura, ejes = plt.subplots(1, columnas, figsize=(ancho, alto))
    figura.patch.set_facecolor(SUPERFICIE)
    ejes = [ejes] if columnas == 1 else list(ejes)
    for ax in ejes:
        ax.set_facecolor(SUPERFICIE)
    return figura, ejes


def guardar(figura, nombre: str) -> None:
    figura.tight_layout()
    figura.savefig(RESULTADOS / nombre, dpi=170, facecolor=SUPERFICIE)
    plt.close(figura)
    print("  " + nombre)


def por_n(resumen, n):
    """Filas del resumen que corresponden a un valor de N, ordenadas por hilos."""
    return sorted([f for f in resumen if f["n_solicitado"] == n], key=lambda f: f["hilos"])


def etiqueta_final(ax, x, y, texto: str, color: str) -> None:
    """Rotula solo el ultimo punto de la serie, no todos."""
    ax.annotate(texto, xy=(x, y), xytext=(8, 0), textcoords="offset points",
                color=TINTA, fontsize=9, va="center")


def etiquetas_finales(ax, puntos) -> None:
    """Rotula el ultimo punto de varias series separando las etiquetas que chocan.

    Recibe tuplas (x, y, texto) y desplaza verticalmente las que quedarian
    encimadas, de modo que el rotulo siga junto a su curva pero se pueda leer.
    """
    inferior, superior = ax.get_ylim()
    separacion = (superior - inferior) * 0.05
    altura_previa = None
    for x, y, texto in sorted(puntos, key=lambda punto: punto[1]):
        altura = y
        if altura_previa is not None and altura - altura_previa < separacion:
            altura = altura_previa + separacion
        altura_previa = altura
        ax.annotate(texto, xy=(x, altura), xytext=(8, 0), textcoords="offset points",
                    color=TINTA, fontsize=9, va="center")


def grafica_speedup(resumen, valores_n) -> None:
    """Speedup del trabajo repartido con OpenMP frente al speedup ideal."""
    figura, (ax,) = nueva_figura(8.4, 5.2)
    hilos = sorted({f["hilos"] for f in resumen})

    ax.plot(hilos, hilos, linestyle="--", linewidth=1.6, color=GRIS,
            label="Speedup ideal (lineal)", zorder=1)
    rotulos = []
    for indice, n in enumerate(valores_n):
        filas = por_n(resumen, n)
        x = [f["hilos"] for f in filas]
        y = [f["speedup_cpu"] for f in filas]
        color = COLORES_SERIE[indice % len(COLORES_SERIE)]
        ax.plot(x, y, marker="o", markersize=6, linewidth=2, color=color,
                label="N = " + format(n, ",").replace(",", " "), zorder=2)
        rotulos.append((x[-1], y[-1], format(y[-1], ".2f") + "x"))

    preparar_ejes(ax, "Speedup del trabajo de CPU por fotograma",
                  "Hilos de OpenMP", "Speedup (T secuencial / T paralelo)")
    ax.set_xticks(hilos)
    ax.set_xlim(min(hilos) - 0.4, max(hilos) + 1.4)
    ax.set_ylim(0, max(hilos) * 1.05)
    ax.legend(frameon=False, labelcolor=TINTA_SUAVE, fontsize=9)
    etiquetas_finales(ax, rotulos)
    guardar(figura, "speedup_vs_hilos.png")


def grafica_eficiencia(resumen, valores_n) -> None:
    """Eficiencia = speedup / hilos. Muestra cuanto rinde cada hilo agregado."""
    figura, (ax,) = nueva_figura(8.4, 5.2)
    hilos = sorted({f["hilos"] for f in resumen})

    ax.axhline(100.0, linestyle="--", linewidth=1.6, color=GRIS,
               label="Eficiencia ideal (100 %)", zorder=1)
    rotulos = []
    for indice, n in enumerate(valores_n):
        filas = por_n(resumen, n)
        x = [f["hilos"] for f in filas]
        y = [f["eficiencia_pct"] for f in filas]
        color = COLORES_SERIE[indice % len(COLORES_SERIE)]
        ax.plot(x, y, marker="o", markersize=6, linewidth=2, color=color,
                label="N = " + format(n, ",").replace(",", " "), zorder=2)
        rotulos.append((x[-1], y[-1], format(y[-1], ".0f") + " %"))

    preparar_ejes(ax, "Eficiencia paralela", "Hilos de OpenMP", "Eficiencia (%)")
    ax.set_xticks(hilos)
    ax.set_xlim(min(hilos) - 0.4, max(hilos) + 1.4)
    ax.set_ylim(0, 115)
    ax.legend(frameon=False, labelcolor=TINTA_SUAVE, fontsize=9)
    etiquetas_finales(ax, rotulos)
    guardar(figura, "eficiencia_vs_hilos.png")


def grafica_tiempos(resumen, valores_n) -> None:
    """Tiempo de CPU por fotograma: curva paralela contra la linea base secuencial."""
    figura, ejes = nueva_figura(5.0 * len(valores_n), 5.0, len(valores_n))
    for ax, n in zip(ejes, valores_n):
        filas = por_n(resumen, n)
        x = [f["hilos"] for f in filas]
        ax.axhline(filas[0]["cpu_secuencial_ms"], linestyle="--", linewidth=1.8,
                   color=AZUL, label="Secuencial (1 hilo)")
        ax.plot(x, [f["cpu_paralelo_ms"] for f in filas], marker="o", markersize=6,
                linewidth=2, color=NARANJA, label="Paralelo (OpenMP)")
        preparar_ejes(ax, "N = " + format(n, ",").replace(",", " "),
                      "Hilos de OpenMP", "CPU por fotograma (ms)")
        ax.set_xticks(x)
        ax.set_ylim(0, max(f["cpu_paralelo_ms"] for f in filas) * 1.30)
        ax.legend(frameon=False, labelcolor=TINTA_SUAVE, fontsize=9)
    guardar(figura, "tiempo_cpu_vs_hilos.png")


def grafica_fps(resumen, valores_n) -> None:
    """FPS alcanzados. La linea de 60 FPS es el minimo que pide el enunciado."""
    figura, ejes = nueva_figura(5.0 * len(valores_n), 5.0, len(valores_n))
    for ax, n in zip(ejes, valores_n):
        filas = por_n(resumen, n)
        x = [f["hilos"] for f in filas]
        ax.axhline(60, linestyle=":", linewidth=1.8, color=GRIS,
                   label="Minimo pedido (60 FPS)")
        ax.axhline(filas[0]["fps_secuencial"], linestyle="--", linewidth=1.8,
                   color=AZUL, label="Secuencial (1 hilo)")
        ax.plot(x, [f["fps_paralelo"] for f in filas], marker="o", markersize=6,
                linewidth=2, color=NARANJA, label="Paralelo (OpenMP)")
        techo = max([f["fps_paralelo"] for f in filas] + [filas[0]["fps_secuencial"]])
        preparar_ejes(ax, "N = " + format(n, ",").replace(",", " "),
                      "Hilos de OpenMP", "Fotogramas por segundo")
        ax.set_xticks(x)
        ax.set_ylim(0, techo * 1.38)
        ax.legend(frameon=False, labelcolor=TINTA_SUAVE, fontsize=9, loc="upper left")
    guardar(figura, "fps_vs_hilos.png")


def grafica_etapas(resumen, valores_n, hilos_max: int) -> None:
    """Desglose del fotograma: donde esta el trabajo que OpenMP alcanza a repartir.

    Solo incluye costos por fotograma. La generacion del mundo ocurre una vez por
    mundo y es dos ordenes de magnitud mas cara, asi que tiene su propia grafica.
    """
    figura, ejes = nueva_figura(5.4 * len(valores_n), 5.0, len(valores_n))
    etapas = ["Fisica de los\nbloques (update)", "Armado del\nbuffer (build)",
              "CPU total\npor fotograma"]
    posiciones = range(len(etapas))

    for ax, n in zip(ejes, valores_n):
        fila = [f for f in por_n(resumen, n) if f["hilos"] == hilos_max][0]
        secuencial = [fila["update_secuencial_ms"], fila["build_secuencial_ms"],
                      fila["cpu_secuencial_ms"]]
        paralelo = [fila["update_paralelo_ms"], fila["build_paralelo_ms"],
                    fila["cpu_paralelo_ms"]]

        ax.bar([p - 0.19 for p in posiciones], secuencial, width=0.34,
               color=AZUL, label="Secuencial", zorder=2)
        ax.bar([p + 0.19 for p in posiciones], paralelo, width=0.34,
               color=NARANJA, label="Paralelo (" + str(hilos_max) + " hilos)", zorder=2)
        for p, alto in zip(posiciones, secuencial):
            ax.annotate(format(alto, ".2f"), xy=(p - 0.19, alto), xytext=(0, 4),
                        textcoords="offset points", ha="center", fontsize=9, color=TINTA)
        for p, alto in zip(posiciones, paralelo):
            ax.annotate(format(alto, ".2f"), xy=(p + 0.19, alto), xytext=(0, 4),
                        textcoords="offset points", ha="center", fontsize=9, color=TINTA)

        preparar_ejes(ax, "N = " + format(n, ",").replace(",", " "), "", "Milisegundos")
        ax.set_xticks(list(posiciones))
        ax.set_xticklabels(etapas, fontsize=9)
        ax.grid(axis="x", visible=False)
        ax.legend(frameon=False, labelcolor=TINTA_SUAVE, fontsize=9)
    guardar(figura, "etapas_por_fotograma.png")


def grafica_generacion(resumen, valores_n) -> None:
    """Costo de generar un mundo completo: el bucle mas paralelizable del programa."""
    figura, ejes = nueva_figura(5.0 * len(valores_n), 5.0, len(valores_n))
    for ax, n in zip(ejes, valores_n):
        filas = por_n(resumen, n)
        x = [f["hilos"] for f in filas]
        ax.axhline(filas[0]["gen_secuencial_ms"], linestyle="--", linewidth=1.8,
                   color=AZUL, label="Secuencial (1 hilo)")
        y = [f["gen_paralelo_ms"] for f in filas]
        ax.plot(x, y, marker="o", markersize=6, linewidth=2, color=NARANJA,
                label="Paralelo (OpenMP)")
        etiqueta_final(ax, x[-1], y[-1],
                       format(filas[-1]["speedup_generacion"], ".2f") + "x", NARANJA)
        preparar_ejes(ax, "N = " + format(n, ",").replace(",", " "),
                      "Hilos de OpenMP", "Generacion del mundo (ms)")
        ax.set_xticks(x)
        ax.set_xlim(min(x) - 0.4, max(x) + 1.6)
        ax.set_ylim(0, max(y + [filas[0]["gen_secuencial_ms"]]) * 1.28)
        ax.legend(frameon=False, labelcolor=TINTA_SUAVE, fontsize=9)
    guardar(figura, "generacion_vs_hilos.png")


def grafica_escalabilidad(resumen, valores_n, hilos_max: int) -> None:
    """Como cambia el speedup al crecer N con la misma cantidad de hilos."""
    if len(valores_n) < 2:
        return
    figura, (ax,) = nueva_figura(8.4, 5.2)
    x = list(valores_n)
    y = [[f for f in por_n(resumen, n) if f["hilos"] == hilos_max][0]["speedup_cpu"]
         for n in valores_n]
    ax.plot(x, y, marker="o", markersize=7, linewidth=2, color=NARANJA)
    for xi, yi in zip(x, y):
        ax.annotate(format(yi, ".2f") + "x", xy=(xi, yi), xytext=(0, 8),
                    textcoords="offset points", ha="center", color=TINTA, fontsize=9)
    preparar_ejes(ax, "Speedup con " + str(hilos_max) + " hilos al crecer la carga",
                  "N (bloques solicitados)", "Speedup del trabajo de CPU")
    ax.set_xscale("log")
    ax.set_xticks(x)
    ax.set_xticklabels([format(n, ",").replace(",", " ") for n in x])
    # Margen a los lados para que el primer y el ultimo rotulo no se recorten.
    ax.set_xlim(min(x) * 0.72, max(x) * 1.42)
    ax.set_ylim(0, max(y) * 1.15)
    guardar(figura, "speedup_vs_n.png")


def grafica_detalle(hilos_max: int, n: int) -> None:
    """FPS fotograma a fotograma: muestra si el ritmo se sostiene o se cae."""
    detalle_sec = RESULTADOS / "detalle_secuencial.csv"
    detalle_par = RESULTADOS / "detalle_paralelo.csv"
    if not (detalle_sec.exists() and detalle_par.exists()):
        return

    def leer(ruta: Path):
        with ruta.open(encoding="utf-8") as archivo:
            filas = list(csv.DictReader(archivo))
        return ([int(f["frame"]) for f in filas], [float(f["fps"]) for f in filas])

    figura, (ax,) = nueva_figura(9.0, 5.0)
    x_sec, y_sec = leer(detalle_sec)
    x_par, y_par = leer(detalle_par)
    ax.axhline(60, linestyle=":", linewidth=1.8, color=GRIS,
               label="Minimo pedido (60 FPS)")
    ax.plot(x_sec, y_sec, linewidth=1.6, color=AZUL, label="Secuencial")
    ax.plot(x_par, y_par, linewidth=1.6, color=NARANJA,
            label="Paralelo (" + str(hilos_max) + " hilos)")
    preparar_ejes(ax, "FPS fotograma a fotograma con N = " +
                  format(n, ",").replace(",", " "),
                  "Fotograma medido", "Fotogramas por segundo")
    ax.set_ylim(0, max(y_sec + y_par) * 1.30)
    ax.legend(frameon=False, labelcolor=TINTA_SUAVE, fontsize=9, loc="upper left")
    guardar(figura, "fps_por_fotograma.png")


def medir_detalle(opciones, hilos_max: int, n: int) -> None:
    """Ejecucion extra de cada version guardando el tiempo de cada fotograma."""
    print("Ejecucion adicional con detalle por fotograma...")
    medir("secuencial", n, 1, opciones, RESULTADOS / "detalle_secuencial.csv")
    medir("paralelo", n, hilos_max, opciones, RESULTADOS / "detalle_paralelo.csv")


def cargar_resumen() -> list[dict[str, object]]:
    """Relee resumen.csv para poder redibujar las graficas sin volver a medir."""
    ruta = RESULTADOS / "resumen.csv"
    if not ruta.exists():
        raise SystemExit("No existe " + str(ruta) + ". Ejecute una medicion primero.")
    filas: list[dict[str, object]] = []
    with ruta.open(encoding="utf-8") as archivo:
        for cruda in csv.DictReader(archivo):
            fila: dict[str, object] = {}
            for clave, valor in cruda.items():
                if clave in ("n_solicitado", "bloques_generados", "hilos"):
                    fila[clave] = int(valor)
                else:
                    fila[clave] = float(valor)
            filas.append(fila)
    return filas


def hilos_predeterminados() -> list[int]:
    """Potencias de dos hasta la cantidad de nucleos, incluyendo el total."""
    nucleos = os.cpu_count() or 4
    lista = [h for h in (1, 2, 4, 8, 16, 32, 64) if h <= nucleos]
    if nucleos not in lista:
        lista.append(nucleos)
    return lista


def analizar_argumentos() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Mide el screensaver secuencial y el paralelo, y grafica "
                    "speedup, eficiencia, tiempos y FPS.")
    parser.add_argument("--n", type=int, nargs="+", default=[100000, 500000, 2000000],
                        help="Valores de N a probar (bloques solicitados).")
    parser.add_argument("--hilos", type=int, nargs="+", default=hilos_predeterminados(),
                        help="Cantidades de hilos de OpenMP a medir.")
    parser.add_argument("--repeticiones", type=int, default=10,
                        help="Ejecuciones por configuracion (minimo 10 en la bitacora).")
    parser.add_argument("--frames", type=int, default=300,
                        help="Fotogramas cronometrados por ejecucion.")
    parser.add_argument("--calentamiento", type=int, default=120,
                        help="Fotogramas descartados antes de empezar a medir.")
    parser.add_argument("--generaciones", type=int, default=5,
                        help="Generaciones del mundo cronometradas por ejecucion.")
    parser.add_argument("--semilla", type=int, default=20260901,
                        help="Semilla fija del mundo, igual para ambas versiones.")
    parser.add_argument("--construccion", type=float, default=1.0,
                        help="Segundos de la fase de armado del mundo.")
    parser.add_argument("--sostenido", type=float, default=120.0,
                        help="Segundos con el mundo completo, donde se mide.")
    parser.add_argument("--ancho", type=int, default=1280, help="Ancho de la ventana.")
    parser.add_argument("--alto", type=int, default=720, help="Alto de la ventana.")
    parser.add_argument("--sin-compilar", dest="compilar", action="store_false",
                        help="Usa los binarios ya compilados en build/.")
    parser.add_argument("--sin-detalle", dest="detalle", action="store_false",
                        help="Omite la ejecucion extra con CSV por fotograma.")
    parser.add_argument("--solo-graficas", action="store_true",
                        help="No mide: redibuja las graficas desde resumen.csv.")
    opciones = parser.parse_args()

    if any(n < 64 for n in opciones.n):
        raise SystemExit("Cada N debe ser al menos 64, igual que en el programa.")
    if any(h < 1 for h in opciones.hilos):
        raise SystemExit("Cada cantidad de hilos debe ser mayor que cero.")
    if opciones.repeticiones < 1:
        raise SystemExit("Se necesita al menos una repeticion por configuracion.")
    if opciones.frames < 30:
        raise SystemExit("Se necesitan al menos 30 fotogramas por ejecucion.")
    if opciones.semilla < 1:
        raise SystemExit("La semilla debe ser un entero positivo para ser reproducible.")

    opciones.n = sorted(set(opciones.n))
    opciones.hilos = sorted(set(opciones.hilos))
    return opciones


def dibujar_todo(resumen, valores_n, hilos_max: int) -> None:
    """Genera todas las graficas a partir del resumen ya calculado."""
    grafica_speedup(resumen, valores_n)
    grafica_eficiencia(resumen, valores_n)
    grafica_tiempos(resumen, valores_n)
    grafica_fps(resumen, valores_n)
    grafica_etapas(resumen, valores_n, hilos_max)
    grafica_generacion(resumen, valores_n)
    grafica_escalabilidad(resumen, valores_n, hilos_max)
    grafica_detalle(hilos_max, max(valores_n))


def imprimir_tabla(resumen: list[dict[str, object]]) -> None:
    """Resumen legible en la consola, util para copiarlo a la bitacora."""
    print("")
    print("      N    hilos   CPU sec (ms)   CPU par (ms)   speedup   eficiencia   FPS par")
    print("  " + "-" * 76)
    for fila in resumen:
        print("  {:>7}   {:>4}   {:>12.3f}   {:>12.3f}   {:>7.2f}x   {:>9.1f}%   {:>7.1f}".format(
            fila["n_solicitado"], fila["hilos"], fila["cpu_secuencial_ms"],
            fila["cpu_paralelo_ms"], fila["speedup_cpu"], fila["eficiencia_pct"],
            fila["fps_paralelo"]))


def main() -> int:
    opciones = analizar_argumentos()

    if opciones.solo_graficas:
        resumen = cargar_resumen()
        valores_n = sorted({int(f["n_solicitado"]) for f in resumen})
        hilos_max = max(int(f["hilos"]) for f in resumen)
        print("Redibujando desde metricas/resultados/resumen.csv")
        dibujar_todo(resumen, valores_n, hilos_max)
        imprimir_tabla(resumen)
        return 0

    if opciones.compilar:
        compilar()
    for nombre in ("secuencial", "paralelo"):
        if not ejecutable(nombre).exists():
            raise SystemExit("Falta " + str(ejecutable(nombre)) +
                             ". Compile el proyecto o quite --sin-compilar.")

    RESULTADOS.mkdir(parents=True, exist_ok=True)
    print("Equipo     : " + platform.processor() + " | " +
          str(os.cpu_count()) + " hilos logicos")
    print("Pruebas    : N = " + str(opciones.n) + " | hilos = " + str(opciones.hilos) +
          " | " + str(opciones.repeticiones) + " repeticiones de " +
          str(opciones.frames) + " fotogramas")
    print("")

    crudas, secuenciales, paralelos = recolectar(opciones)
    resumen = resumir(opciones, secuenciales, paralelos)

    print("")
    print("Archivos generados en metricas/resultados/")
    guardar_csv(RESULTADOS / "mediciones_crudas.csv", crudas)
    guardar_csv(RESULTADOS / "resumen.csv", resumen)

    hilos_max = max(opciones.hilos)
    if opciones.detalle:
        medir_detalle(opciones, hilos_max, max(opciones.n))
    dibujar_todo(resumen, opciones.n, hilos_max)

    imprimir_tabla(resumen)
    return 0


if __name__ == "__main__":
    sys.exit(main())
