#!/usr/bin/env python3
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Builds EngineData_ALL_Texts.zip (<lang>.txt with "id=text" lines, the format of BMW's PluginsCoded apps, as used by
# NBT-AAD) and EngineData_ALL_Images.zip (<imageId>.png). Needs Pillow for the icon.
#   text 1 = menu row / screen title        image 56001 = 63x54 row icon
import math
import os
import zipfile

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))

# Language file names are the ones NBT-AAD ships (BMW's language codes); ASCII only, like its text DB.
LANGS = {
    "de": ("Deutsch", "Motordaten"),
    "en-UK": ("English (UK)", "Engine data"),
    "en-US": ("English (US)", "Engine data"),
    "es-ES": ("Espanol", "Datos del motor"),
    "es-US": ("Espanol (US)", "Datos del motor"),
    "fr-FR": ("Francais", "Donnees moteur"),
    "fr-CA": ("Francais (CA)", "Donnees moteur"),
    "it": ("Italiano", "Dati motore"),
    "pt": ("Portugues", "Dados do motor"),
    "ru": ("Russian", "Engine data"),
    "pl": ("Polski", "Dane silnika"),
}


def make_icon(path):
    """63x54 RGBA row icon: a round gauge with a needle, in the near-white NBT-AAD's icon uses."""
    w, h = 63, 54
    scale = 4
    im = Image.new("RGBA", (w * scale, h * scale), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    ink = (250, 250, 250, 248)
    cx, cy, r = 31.5 * scale, 29 * scale, 22 * scale
    d.arc([cx - r, cy - r, cx + r, cy + r], start=150, end=390, fill=ink, width=3 * scale)
    for k in range(7):                                   # tick marks
        a = math.radians(150 + k * 40)
        x0, y0 = cx + math.cos(a) * (r - 4 * scale), cy + math.sin(a) * (r - 4 * scale)
        x1, y1 = cx + math.cos(a) * (r - 9 * scale), cy + math.sin(a) * (r - 9 * scale)
        d.line([x0, y0, x1, y1], fill=ink, width=2 * scale)
    a = math.radians(300)                                # needle
    d.line([cx, cy, cx + math.cos(a) * (r - 8 * scale), cy + math.sin(a) * (r - 8 * scale)], fill=ink, width=3 * scale)
    d.ellipse([cx - 3.5 * scale, cy - 3.5 * scale, cx + 3.5 * scale, cy + 3.5 * scale], fill=ink)
    im.resize((w, h), Image.LANCZOS).save(path)


def main():
    with zipfile.ZipFile(os.path.join(HERE, "EngineData_ALL_Texts.zip"), "w", zipfile.ZIP_DEFLATED) as z:
        for lang, (langname, title) in LANGS.items():
            body = "100000=%s\n1=%s\n" % (langname, title)
            z.writestr(lang + ".txt", body.encode("utf-8"))
    icon = os.path.join(HERE, "res", "row_icon_63x54.png")
    os.makedirs(os.path.dirname(icon), exist_ok=True)
    make_icon(icon)
    with zipfile.ZipFile(os.path.join(HERE, "EngineData_ALL_Images.zip"), "w", zipfile.ZIP_DEFLATED) as z:
        z.write(icon, "56001.png")
    print("wrote EngineData_ALL_Texts.zip, EngineData_ALL_Images.zip")


if __name__ == "__main__":
    main()
