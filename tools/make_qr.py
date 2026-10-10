#!/usr/bin/env python3
"""Generates assets/qr-vitashell.png: a QR code VitaShell can scan (d-pad Left + O)
to download and install the latest ABSvita.vpk release. Needs qrcode and Pillow."""
import qrcode
from PIL import Image, ImageDraw, ImageFont

URL = "https://github.com/DarthTigerson/ABSvita/releases/latest/download/ABSvita.vpk"

qr = qrcode.QRCode(error_correction=qrcode.constants.ERROR_CORRECT_M, box_size=8, border=3)
qr.add_data(URL)
qr.make(fit=True)
code = qr.make_image(fill_color=(15, 17, 21), back_color=(255, 255, 255)).convert("RGB")
w, h = code.size
out = Image.new("RGB", (w, h + 56), (255, 255, 255))
out.paste(code, (0, 0))
d = ImageDraw.Draw(out)
f = ImageFont.truetype("assets/fonts/Manrope-700.ttf", 22)
s = ImageFont.truetype("assets/fonts/Manrope-500.ttf", 14)
d.text((w // 2, h + 6), "ABSvita", font=f, fill=(15, 17, 21), anchor="mt")
d.text((w // 2, h + 34), "VitaShell → Left + ○ → scan", font=s, fill=(92, 99, 112), anchor="mt")
out.save("assets/qr-vitashell.png")
print("wrote assets/qr-vitashell.png for", URL)
