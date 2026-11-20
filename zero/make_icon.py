"""Draw the Meshtastic mark (a '/' and a 'Λ' in Meshtastic green) as a 100x100 launcher icon."""
import sys

from PIL import Image, ImageDraw

S = 8  # supersampling for smooth edges
GREEN = (103, 234, 148, 255)  # Meshtastic green #67EA94
W = 13 * S


def p(x, y):
    return (x * S, y * S)


img = Image.new("RGBA", (100 * S, 100 * S), (0, 0, 0, 0))
d = ImageDraw.Draw(img)
for a, b in [((10, 74), (36, 30)), ((38, 74), (63, 30)), ((63, 30), (89, 74))]:
    d.line([p(*a), p(*b)], fill=GREEN, width=W)
    for pt in (a, b):  # round caps
        x, y = p(*pt)
        d.ellipse([x - W / 2, y - W / 2, x + W / 2, y + W / 2], fill=GREEN)
img.resize((100, 100), Image.LANCZOS).save(sys.argv[1])
