# games/racer/tools/plant-split.py: the plant clumps of a scanned set (a Poly Haven glTF, CC0) as one glb each, run in
# Blender:
#   flatpak run org.blender.Blender -b --python games/racer/tools/plant-split.py -- <in.gltf> <out prefix> <triangles> [n]
# Each mesh object of the file is one clump: it is decimated to about <triangles> (leaves stay cut out by their alpha
# map), its origin set at the middle of its base and exported as <out prefix><i>.glb with its textures. [n]: only the
# first n clumps, largest first. Absolute paths: the flatpak runs in its own sandbox. Blender's Python only.
import sys
import bpy

argv = sys.argv[sys.argv.index("--") + 1:]
src, prefix, target = argv[0], argv[1], int(argv[2])
limit = int(argv[3]) if len(argv) > 3 else 99

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=src)
meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
for o in meshes:
    bpy.ops.object.select_all(action="DESELECT")
    o.select_set(True)
    bpy.context.view_layer.objects.active = o
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)


def tris(o):
    return sum(len(p.vertices) - 2 for p in o.data.polygons)


meshes.sort(key=tris, reverse=True)
for i, ob in enumerate(meshes[:limit]):
    before = tris(ob)
    if before > target:
        mod = ob.modifiers.new("dec", "DECIMATE")
        mod.ratio = target / before
        bpy.context.view_layer.objects.active = ob
        bpy.ops.object.modifier_apply(modifier=mod.name)
    xs = [v.co.x for v in ob.data.vertices]
    ys = [v.co.y for v in ob.data.vertices]
    zs = [v.co.z for v in ob.data.vertices]
    cx, cy, cz = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2, min(zs)
    for v in ob.data.vertices:
        v.co.x -= cx
        v.co.y -= cy
        v.co.z -= cz
    ob.location = (0, 0, 0)
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    out = f"{prefix}{i}.glb"
    bpy.ops.export_scene.gltf(filepath=out, export_format="GLB", use_selection=True, export_yup=True,
                              export_image_format="AUTO")
    print(f"plant-split: {ob.name} {before} -> {tris(ob)} triangles, {max(xs)-min(xs):.2f} x {max(ys)-min(ys):.2f} x {max(zs)-min(zs):.2f} -> {out}")
