# games/racer/tools/palm-gen.py: a palm built from nothing but maths, as a glb, run in Blender:
#   flatpak run org.blender.Blender -b --python games/racer/tools/palm-gen.py -- <leaf image> <bark image> <out.glb> [seed]
# A curved ringed trunk and a crown of drooping fronds whose leaflets are real geometry (no alpha cut-outs), about 14 m
# tall with its foot at the origin. Absolute paths: the flatpak runs in its own sandbox. Blender's Python only.
import math
import random
import sys
import bpy

argv = sys.argv[sys.argv.index("--") + 1:]
leaf_img, bark_img, out = argv[0], argv[1], argv[2]
random.seed(int(argv[3]) if len(argv) > 3 else 7)

bpy.ops.wm.read_factory_settings(use_empty=True)


def material(name, path, rough):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = next(n for n in m.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    t = m.node_tree.nodes.new("ShaderNodeTexImage")
    t.image = bpy.data.images.load(path)
    m.node_tree.links.new(t.outputs[0], b.inputs[0])
    b.inputs["Roughness"].default_value = rough
    b.inputs["Metallic"].default_value = 0
    return m


def mesh_object(name, verts, faces, uvs, mat):
    me = bpy.data.meshes.new(name)
    me.from_pydata(verts, [], faces)
    me.update()
    layer = me.uv_layers.new()
    for poly in me.polygons:
        for li in poly.loop_indices:
            layer.data[li].uv = uvs[me.loops[li].vertex_index]
    me.materials.append(mat)
    ob = bpy.data.objects.new(name, me)
    bpy.context.collection.objects.link(ob)
    return ob


HEIGHT = 11.5
lean = (random.uniform(0.8, 1.6), random.uniform(-0.6, 0.6))


def trunk_at(t):
    return (lean[0] * t * t, lean[1] * t * t, HEIGHT * t)


# the trunk: rings of 8, wide at the foot, a little swollen at the crown
RINGS, SIDES = 12, 8
tv, tf, tu = [], [], []
for i in range(RINGS + 1):
    t = i / RINGS
    c = trunk_at(t)
    r = 0.2 + 0.2 * (1 - t) ** 3 + 0.03 * math.sin(t * 40)
    for j in range(SIDES + 1):
        a = 2 * math.pi * j / SIDES
        tv.append((c[0] + r * math.cos(a), c[1] + r * math.sin(a), c[2]))
        tu.append((j / SIDES, t * 5))
for i in range(RINGS):
    for j in range(SIDES):
        p = i * (SIDES + 1) + j
        tf.append((p, p + 1, p + SIDES + 2, p + SIDES + 1))
mesh_object("Trunk", tv, tf, tu, material("Bark", bark_img, 0.9))

# the fronds: a spine that arcs out and droops, leaflets in pairs along it
lv, lf, lu = [], [], []
top = trunk_at(1)


def quad(a, b, c, d, u):
    n = len(lv)
    lv.extend([a, b, c, d])
    lf.append((n, n + 1, n + 2, n + 3))
    lu.extend([(u, 0.1), (u + 0.25, 0.1), (u + 0.25, 0.9), (u, 0.9)])


FRONDS = 16
for f in range(FRONDS):
    yaw = f * 2.399963 + random.uniform(-0.15, 0.15)  # the golden angle
    rise = 0.95 - 1.5 * (f / (FRONDS - 1)) + random.uniform(-0.1, 0.1)  # upright young fronds to old drooping ones
    length = random.uniform(3.9, 5.0)
    dx, dy = math.cos(yaw), math.sin(yaw)
    SEG = 11
    spine = []
    for s in range(SEG + 1):
        k = s / SEG
        out_ = length * k * math.cos(rise * (1 - k) - 0.9 * k * k)
        up = length * k * (math.sin(rise) - 0.75 * k)
        spine.append((top[0] + dx * out_, top[1] + dy * out_, top[2] + up))
    for s in range(SEG):
        a, b = spine[s], spine[s + 1]
        k = (s + 0.5) / SEG
        w = 1.25 * math.sin(math.pi * min(1, k * 1.05 + 0.06)) ** 0.6  # leaflet length along the frond
        droop = 0.32 + 0.5 * k
        for side in (-1, 1):
            sx, sy = -dy * side, dx * side
            tip = (a[0] + sx * w + (b[0] - a[0]) * 1.1, a[1] + sy * w + (b[1] - a[1]) * 1.1, a[2] - droop * w)
            mid = (a[0] + sx * w * 0.5 + (b[0] - a[0]) * 0.55, a[1] + sy * w * 0.5 + (b[1] - a[1]) * 0.55, a[2] - droop * w * 0.3)
            quad(a, b, mid, tip, random.randint(0, 3) * 0.25)
ob = mesh_object("Fronds", lv, lf, lu, material("Frond", leaf_img, 0.7))
ob.data.materials[0].use_backface_culling = False

bpy.ops.export_scene.gltf(filepath=out, export_format="GLB", export_yup=True, export_image_format="JPEG", export_image_quality=82)
print(f"palm-gen: {len(tf)} trunk quads, {len(lf)} leaflet quads -> {out}")
