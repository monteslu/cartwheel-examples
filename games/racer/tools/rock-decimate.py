# games/racer/tools/rock-decimate.py: a scanned rock (a Poly Haven glTF, CC0) cut down for scattering, run in Blender:
#   flatpak run org.blender.Blender -b --python games/racer/tools/rock-decimate.py -- <in.gltf> <out.glb> <triangles>
# Joins its meshes, decimates to about <triangles> (the scan's own normal map keeps the surface detail), puts the origin at
# the middle of its base (y up in the glb, so a scatter layer stands it on the ground) and exports a glb with its textures.
# Absolute paths: the flatpak runs in its own sandbox. Blender's Python only; the game never runs it.
import sys
import bpy

argv = sys.argv[sys.argv.index("--") + 1:]
src, dst, target = argv[0], argv[1], int(argv[2])

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=src)
meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
for o in bpy.context.scene.objects:
    o.select_set(o in meshes)
bpy.context.view_layer.objects.active = meshes[0]
if len(meshes) > 1:
    bpy.ops.object.join()
ob = bpy.context.view_layer.objects.active
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

tris = sum(len(p.vertices) - 2 for p in ob.data.polygons)
if tris > target:
    mod = ob.modifiers.new("dec", "DECIMATE")
    mod.ratio = target / tris
    bpy.ops.object.modifier_apply(modifier=mod.name)
after = sum(len(p.vertices) - 2 for p in ob.data.polygons)

# origin: the middle of the base (Blender is z up; the glTF exporter turns it to y up)
xs = [v.co.x for v in ob.data.vertices]
ys = [v.co.y for v in ob.data.vertices]
zs = [v.co.z for v in ob.data.vertices]
cx, cy, cz = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2, min(zs)
for v in ob.data.vertices:
    v.co.x -= cx
    v.co.y -= cy
    v.co.z -= cz
ob.location = (0, 0, 0)
for p in ob.data.polygons:
    p.use_smooth = True

bpy.ops.export_scene.gltf(filepath=dst, export_format="GLB", use_selection=False, export_yup=True,
                          export_image_format="JPEG", export_jpeg_quality=88)
print(f"rock-decimate: {src} {tris} -> {after} triangles, size {max(xs)-min(xs):.2f} x {max(ys)-min(ys):.2f} x {max(zs)-min(zs):.2f} -> {dst}")
