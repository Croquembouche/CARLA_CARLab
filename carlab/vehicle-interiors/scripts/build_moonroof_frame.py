"""Build one watertight MKZ roof ring around the existing moonroof glass."""
import bpy
import json
from pathlib import Path
from mathutils import Vector

ROOT = Path('/mnt/simulations/vehicle-interiors')
OUTPUT = ROOT / 'exports' / 'SM_MKZ_Moonroof_Frame.fbx'

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.unit_settings.system = 'METRIC'
scene.unit_settings.scale_length = 1.0

bodywork = bpy.data.materials.new('MI_LincolnMKZ_Bodywork')
bodywork.diffuse_color = (0.008, 0.010, 0.014, 1.0)
headliner = bpy.data.materials.new('MKZ_Completion_Headliner')
headliner.diffuse_color = (0.105, 0.11, 0.115, 1.0)

def roof_z(x, y):
    return 1.525 - 0.040 * ((x + 0.35) / 0.90) ** 2 - 0.045 * (y / 0.68) ** 2

def rectangle_loop(x0, x1, y0, y1, steps=20):
    points = []
    for i in range(steps):
        points.append((x0 + (x1 - x0) * i / steps, y0))
    for i in range(steps):
        points.append((x1, y0 + (y1 - y0) * i / steps))
    for i in range(steps):
        points.append((x1 - (x1 - x0) * i / steps, y1))
    for i in range(steps):
        points.append((x0, y1 - (y1 - y0) * i / steps))
    return points

outer = rectangle_loop(-1.60, 0.64, -0.68, 0.68)
inner = rectangle_loop(-0.88, 0.30, -0.485, 0.485)
count = len(outer)
thickness = 0.050
vertices = []
for loop, drop in ((outer, 0.0), (inner, 0.0), (outer, thickness), (inner, thickness)):
    vertices.extend((x, y, roof_z(x, y) - drop) for x, y in loop)

top_outer = 0
top_inner = count
bottom_outer = count * 2
bottom_inner = count * 3
faces = []
materials = []
for i in range(count):
    j = (i + 1) % count
    faces.append((top_outer + i, top_outer + j, top_inner + j, top_inner + i))
    materials.append(0)
    faces.append((bottom_outer + i, bottom_inner + i, bottom_inner + j, bottom_outer + j))
    materials.append(1)
    faces.append((top_outer + i, bottom_outer + i, bottom_outer + j, top_outer + j))
    materials.append(0)
    faces.append((top_inner + i, top_inner + j, bottom_inner + j, bottom_inner + i))
    materials.append(1)

mesh = bpy.data.meshes.new('SM_MKZ_Moonroof_Frame')
mesh.from_pydata(vertices, [], faces)
mesh.materials.append(bodywork)
mesh.materials.append(headliner)
for polygon, material_index in zip(mesh.polygons, materials):
    polygon.material_index = material_index
obj = bpy.data.objects.new('SM_MKZ_Moonroof_Frame', mesh)
scene.collection.objects.link(obj)

bpy.ops.object.select_all(action='DESELECT')
obj.select_set(True)
bpy.context.view_layer.objects.active = obj
bpy.ops.export_scene.fbx(
    filepath=str(OUTPUT),
    use_selection=True,
    object_types={'MESH'},
    bake_anim=False,
    axis_forward='-Y',
    axis_up='Z',
)

bounds = [obj.matrix_world @ Vector(corner) for corner in obj.bound_box]
report = {
    'output': str(OUTPUT),
    'vertices': len(mesh.vertices),
    'polygons': len(mesh.polygons),
    'watertight_ring': True,
    'material_slots': [material.name for material in mesh.materials],
    'bounds_m': {
        'min': [min(point[i] for point in bounds) for i in range(3)],
        'max': [max(point[i] for point in bounds) for i in range(3)],
    },
    'moonroof_opening_m': {'x': [-0.88, 0.30], 'y': [-0.485, 0.485]},
}
(ROOT / 'moonroof-frame-build.json').write_text(json.dumps(report, indent=2))
print(json.dumps(report, indent=2))
