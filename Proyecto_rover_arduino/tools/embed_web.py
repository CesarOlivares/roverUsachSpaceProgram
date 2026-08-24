#!/usr/bin/env python3
"""Genera web_pages.h con los archivos del panel embebidos en PROGMEM.

Uso:
    python embed_web.py [carpeta_panel] [-o salida.h]

Por defecto lee ../../Proyecto_rover_panel/rover_web_gcs y escribe
../rover_arduino/web_pages.h (rutas relativas a este script).
Ejecutar cada vez que se edite index.html, app.js o style.css.
"""

import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_SRC = os.path.normpath(os.path.join(HERE, "..", "..", "Proyecto_rover_panel", "rover_web_gcs"))
DEFAULT_OUT = os.path.normpath(os.path.join(HERE, "..", "rover_arduino", "web_pages.h"))

FILES = [
    ("index.html", "INDEX_HTML"),
    ("app.js", "APP_JS"),
    ("style.css", "STYLE_CSS"),
]


def emit_array(out, name, data):
    out.write(f"const uint8_t {name}[] PROGMEM = {{\n")
    for i in range(0, len(data), 16):
        chunk = ", ".join(f"0x{b:02x}" for b in data[i:i + 16])
        out.write(f"  {chunk},\n")
    out.write("};\n")
    out.write(f"const size_t {name}_LEN = {len(data)};\n\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("src", nargs="?", default=DEFAULT_SRC, help="carpeta con los archivos del panel")
    ap.add_argument("-o", "--out", default=DEFAULT_OUT, help="header de salida")
    args = ap.parse_args()

    for fname, _ in FILES:
        path = os.path.join(args.src, fname)
        if not os.path.isfile(path):
            sys.exit(f"No existe {path}")

    with open(args.out, "w", newline="\n") as out:
        out.write("// GENERADO por tools/embed_web.py - NO EDITAR A MANO.\n")
        out.write("// Regenerar tras cada cambio en el panel (index.html, app.js, style.css).\n")
        out.write("#ifndef WEB_PAGES_H\n#define WEB_PAGES_H\n\n#include <Arduino.h>\n\n")
        total = 0
        for fname, symbol in FILES:
            with open(os.path.join(args.src, fname), "rb") as f:
                data = f.read()
            total += len(data)
            print(f"  {fname}: {len(data)} bytes -> {symbol}")
            emit_array(out, symbol, data)
        out.write("#endif // WEB_PAGES_H\n")

    print(f"OK: {args.out} ({total} bytes de panel embebidos)")


if __name__ == "__main__":
    main()
