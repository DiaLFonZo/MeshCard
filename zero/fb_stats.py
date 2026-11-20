import collections
d = open("/dev/fb0", "rb").read(640 * 170)
c = collections.Counter(d[i] | d[i + 1] << 8 for i in range(0, len(d), 2))
print(c.most_common(8))
cols = sorted(set((i // 2) % 320 for i in range(0, len(d), 2) if d[i] | d[i + 1]))
print("x range", cols[:3], cols[-3:] if cols else None, len(cols))
